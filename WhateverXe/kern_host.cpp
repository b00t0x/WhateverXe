// Copyright © 2018 vit9696. Derived from WhateverGreen (BSD-3-Clause).
#include "kern_host.hpp"
#include "kern_donor.hpp"
#include <mach/kmod.h>
#include "kern_host_power_contract.hpp"
#include "kern_xelp_native_placement.hpp"
#include <Headers/kern_disasm.hpp>
#include "kern_xelp_device.hpp"
#include "kern_xe_data_access.hpp"
#include "kern_backlight_profile.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_iokit.hpp>
#include <IOKit/pci/IOPCIDevice.h>
static size_t hostInstructionSize(const uint8_t *bytes,size_t length) {
 uint8_t code[32] {};memcpy(code,bytes,length<sizeof(code)?length:sizeof(code));
 Disassembler::hde_t ins {};
 const auto n=Disassembler::hdeDisasm(reinterpret_cast<uintptr_t>(code),&ins);
 return !(ins.flags&F_ERROR) && n<=length ? n : 0;
}
static size_t hostFunctionExtent(mach_vm_address_t fn,mach_vm_address_t base,size_t size) {
 if(fn<base || fn-base>=size)return 0;
 const auto remaining=size-(fn-base);
 return HostPowerContract::extent(reinterpret_cast<const uint8_t *>(fn),remaining<2048?remaining:2048,hostInstructionSize);
}
XeHost *XeHost::callbackHost;
struct FramebufferViewer : public IOFramebuffer {
 static IOMemoryMap *&getVramMap(IOFramebuffer *fb) { return static_cast<FramebufferViewer *>(fb)->fVramMap; }
};
static const char *graphicsPath[] { "/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" };
static KernelPatcher::KextInfo graphics { "com.apple.iokit.IOGraphicsFamily",graphicsPath,arrsize(graphicsPath),{true},{},KernelPatcher::KextInfo::Unloaded };
static const char *accelPath[] { "/System/Library/Extensions/IOAcceleratorFamily2.kext/Contents/MacOS/IOAcceleratorFamily2" };
static KernelPatcher::KextInfo accelFamily { "com.apple.iokit.IOAcceleratorFamily2",accelPath,arrsize(accelPath),{true},{},KernelPatcher::KextInfo::Unloaded };
static const char *backlightPath[] { "/System/Library/Extensions/AppleBacklight.kext/Contents/MacOS/AppleBacklight" };
static KernelPatcher::KextInfo backlight { "com.apple.driver.AppleBacklight",backlightPath,arrsize(backlightPath),{true},{},KernelPatcher::KextInfo::Unloaded };
static const char *surfacePath[] { "/System/Library/Extensions/IOSurface.kext/Contents/MacOS/IOSurface" };
static KernelPatcher::KextInfo surface { "com.apple.iokit.IOSurface",surfacePath,arrsize(surfacePath),{true},{},KernelPatcher::KextInfo::Unloaded };
void XeHost::init() {
 callbackHost=this;
 // Copy the firmware tables while the boot firmware still owns them. This
 // performs no GPU initialization; native admission remains image-triggered.
 lilu.onPatcherLoadForce([](void *u,KernelPatcher &){static_cast<XeHost *>(u)->captureFirmware();},this);
 PE_parse_boot_argn("gfxrst",&resetFramebuffer,sizeof(resetFramebuffer));
 if(resetFramebuffer>=FB_TOTAL)resetFramebuffer=FB_DETECT;
 lilu.onKextLoadForce(nullptr,0,[](void *u,KernelPatcher &p,size_t i,mach_vm_address_t a,size_t s){static_cast<XeHost *>(u)->processKext(p,i,a,s);},this);
 lilu.onKextLoadForce(&graphics);
 lilu.onKextLoadForce(&accelFamily);
 lilu.onKextLoadForce(&backlight);
 lilu.onKextLoadForce(&surface);
 xe.init();
 XeDataAccess::init();
}
void XeHost::captureFirmware() {
 auto info=DeviceInfo::create();if(!info)return;
 auto gpu=OSDynamicCast(IOPCIDevice,info->videoBuiltin);
 if(gpu && WIOKit::awaitPublishing(gpu))xe.captureFirmware(gpu,XELP::deviceProfile(gpu));
 DeviceInfo::deleter(info);
}
void XeHost::processKernel(KernelPatcher &patcher) {
 kernelProcessed=true;
 auto info=DeviceInfo::create();if(!info)return;
 auto gpu=OSDynamicCast(IOPCIDevice,info->videoBuiltin);
 // Match WEG ordering: PCI config reads require a published IOPCIDevice.
 const bool published=gpu && WIOKit::awaitPublishing(gpu);
 const auto profile=published ? XELP::deviceProfile(gpu) : XELP::Profile {};
 // The host must dispatch framebuffer patches and accelerator rejection
 // hooks even when the selected profile deliberately has no Render backend.
 active=profile.displayAdmitted();

 const auto display=XELP::displayAdapter(profile.family);
 systemWakeRecovery=active && (display==XELP::DisplayAdapter::ADLP || display==XELP::DisplayAdapter::ADLN);
 backlightEnabled=active && (display==XELP::DisplayAdapter::TGL || display==XELP::DisplayAdapter::ADLS ||
  display==XELP::DisplayAdapter::ADLP || display==XELP::DisplayAdapter::ADLN) &&
  info->firmwareVendor!=DeviceInfo::FirmwareVendor::Apple;
 backlightGPU=backlightEnabled ? gpu : nullptr;
 if(published) {
  uint32_t selection[] {1,profile.identity,profile.revision,profile.effectiveIdentity,
   static_cast<uint32_t>(profile.family),static_cast<uint32_t>(profile.display),
   static_cast<uint32_t>(profile.gt),active};
  gpu->setProperty("WhateverXeDeviceSelection",selection,sizeof(selection));
  if(!gpu->getProperty("model"))if(const auto name=XELP::model(profile))gpu->setProperty("model",const_cast<char *>(name),strlen(name)+1);
 }
 // Capture the unmodified firmware/GT state before admitting a new backend.
 if(!active && (profile.family==XELP::Family::AlderLakeS ||
                profile.family==XELP::Family::AlderLakeP))xe.processKernel(patcher,info);
 if(active) {
  XeDataAccess::processKernel(patcher);
  // Xe admission reads physical PCI identity. Do not install WEG's ID spoof hooks.
  // WEG normalizes the ME device discovered by Lilu for Apple's name matching.
  if(auto mei=info->managementEngine) {
   const auto name=mei->getName();
   if(!name || strcmp(name,"IMEI"))WIOKit::renameDevice(mei,"IMEI");
  }
  if(info->firmwareVendor!=DeviceInfo::FirmwareVendor::Apple) {
   if(strcmp(gpu->getName(),"IGPU"))WIOKit::renameDevice(gpu,"IGPU");
   if(!gpu->getProperty("built-in")){uint8_t value=0;gpu->setProperty("built-in",&value,1);}
   if(resetFramebuffer==FB_DETECT && !info->reportedFramebufferIsConnectorLess)resetFramebuffer=FB_COPY;
  }
  gpu->setProperty("WhateverXePhysicalGGC",gpu->configRead16(0x50),32);
  xe.processKernel(patcher,info);
  if(resetFramebuffer==FB_COPY || resetFramebuffer==FB_ZEROFILL) {
   auto v=reinterpret_cast<vc_info *>(patcher.solveSymbol(KernelPatcher::KernelID,"_vinfo"));
   if(v){consoleVinfo=*v;gotConsoleVinfo=true;}else patcher.clearError();
  }
 }
 if(!active || resetFramebuffer==FB_DETECT){resetFramebuffer=FB_NONE;if(!systemWakeRecovery)graphics.switchOff();}
 DeviceInfo::deleter(info);
}
void XeHost::processLoadedDependency(KernelPatcher &patcher,KernelPatcher::KextInfo &info) {
 if(info.loadIndex==KernelPatcher::KextInfo::Unloaded)return;
 // A donor's OSBundleLibraries have loaded before its load callback. Some
 // KC hosts (including IOSurface on Monterey) do not deliver a Lilu callback.
 // Use the same named kmod list as Lilu, then its image/symbol machinery;
 // never infer an ABI from an OS/build name or scan arbitrary memory.
 auto list=reinterpret_cast<kmod_info_t **>(patcher.solveSymbol(KernelPatcher::KernelID,"_kmod"));
 patcher.clearError();if(!list)return;
 // XNU owns this finite list. Match Lilu's traversal; a module-count cap
 // must not reject a valid dependency merely because it is late in the list.
 for(auto module=*list;module;module=module->next) {
  if(strncmp(module->name,info.id,KMOD_MAX_NAME))continue;
  if(!module->address || module->size<32)return;
  const auto size=patcher.updateRunningInfo(info.loadIndex,module->address,module->size);
  const bool ready=patcher.getError()==KernelPatcher::Error::NoError;
  patcher.clearError();if(ready)processKext(patcher,info.loadIndex,module->address,size);
  return;
 }
}
void XeHost::processKext(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
 // Without Graphics, leave firmware scanout/GT state untouched. A real stack
 // image is the activation trigger, still before its native probe/start.
 if(!kernelProcessed) {
  if(!xe.isGraphicsStack(index))return;
  processKernel(patcher);
 }
 if(!active)return;
 if(xe.isIntelProvider(index)) {
  // Donor imports exist before their native start, but Lilu's already-loaded
  // kmod walk need not notify their provider first. Resolve its exports before
  // the one-shot framebuffer installer consumes them. Seen is not ABI-ready:
  // an incompatible provider still reaches the existing rejection guards.
  xe.processKext(patcher,index,address,size);
  providerSeen=true;
  return;
 }
 if(surface.loadIndex==index) {
  if(surfaceSeen)return;
  const char *names[]={"__ZNK9IOSurface14getPixelFormatEv","__ZNK9IOSurface13getPlaneCountEv",
   "__ZNK9IOSurface12getSurfaceIDEv","__ZNK9IOSurface14getYCbCrMatrixEv"};
  const uint8_t *functions[4] {};size_t lengths[4] {};
  for(unsigned i=0;i<4;++i) {
   const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,names[i],address,size);patcher.clearError();
   if(fn>=address && fn-address<size) {
    functions[i]=reinterpret_cast<const uint8_t *>(fn);
    const auto remaining=size-(fn-address);lengths[i]=remaining<64?remaining:64;
   }
  }
  const auto fb=XELPNative::surfaceABI(functions,lengths,true);
  const auto accel=XELPNative::surfaceABI(functions,lengths,false);
  xe.setHostSurfaceABI(static_cast<unsigned>(fb),static_cast<unsigned>(accel));
  surfaceSeen=true;
  uint32_t state[]={static_cast<uint32_t>(fb),static_cast<uint32_t>(accel)};
  IORegistryEntry::getRegistryRoot()->setProperty("WEXHostSurfaceABI",&state,sizeof(state));
  return;
 }
 if(accelFamily.loadIndex==index) {
  if(!systemWakeRecovery || accelSeen)return;
  // Only the first loop of system_will_sleep(false) opens mode changes.
  // Leave its subsequent orphan cleanup and the hardware sleep phase native.
  const auto sym=[&](const char *name) {
   auto v=patcher.solveSymbol<mach_vm_address_t>(index,name,address,size);patcher.clearError();return v;
  };
  sleepFBCount=sym("__ZNK21IOAccelDisplayMachine19getFramebufferCountEv");
  const auto sleep=sym("__ZN22IOGraphicsAccelerator217system_will_sleepEib");
  const auto mode=sym("__ZN21IOAccelDisplayMachine24display_mode_will_changeEj");
  const auto sleepLength=hostFunctionExtent(sleep,address,size);
  const auto modeLength=hostFunctionExtent(mode,address,size);
  const auto site=HostPowerContract::sleepSite(reinterpret_cast<const uint8_t *>(sleep),sleepLength,sleep,sleepFBCount,hostInstructionSize);
  bool ready=false;
  if(!sleepScopeLock)sleepScopeLock=IOSimpleLockAlloc();
  KernelPatcher::RouteRequest notification("__ZN18IOAccelDisplayPipe22display_change_handlerEPvP13IOFramebufferiS0_",
      wrapDisplayChange,orgDisplayChange);
  const bool notifications=sleepScopeLock && patcher.routeMultiple(index,&notification,1,address,size);
  patcher.clearError();
  if(notifications && sleepFBCount && site.valid && HostPowerContract::sleepFields(
     reinterpret_cast<const uint8_t *>(sleep),sleepLength,reinterpret_cast<const uint8_t *>(mode),modeLength,hostInstructionSize,site.owner)) {
   // Pass this accelerator in rdi. The host compiler may keep it in rbx or
   // r14; the matched count query establishes which register owns it.
   uint8_t code[]={0x48,0x89,0xdf,0xe8,0,0,0,0,0x90,0x90,0x90,0x90,0x90};
   code[0]=0x48|(site.owner>=8 ? 4 : 0);code[2]=0xC7|((site.owner&7)<<3);
   const auto target=sleep+site.offset;
   const auto distance=static_cast<int64_t>(reinterpret_cast<uintptr_t>(pendingSleepDisplays)-(target+8));
   if(distance==static_cast<int32_t>(distance)) {
    const auto rel=static_cast<int32_t>(distance);memcpy(code+4,&rel,4);
    patcher.routeBlock(target,code,site.length);
    ready=patcher.getError()==KernelPatcher::Error::NoError && !memcmp(reinterpret_cast<void *>(target),code,site.length);patcher.clearError();
   }
  }
  IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIDarkWakeReady",ready);
  xe.setHostSleepReady(ready);accelSeen=true;
  return;
 }
 if(backlight.loadIndex==index) {
  if(backlightSeen)return;
  backlightSeen=true;
  if(backlightEnabled) {
   KernelPatcher::RouteRequest route("__ZN15AppleIntelPanel10setDisplayEP9IODisplay",wrapPanelSetDisplay,orgPanelSetDisplay);
   const bool ready=patcher.routeMultiple(index,&route,1,address,size);
   IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeBacklightProfileReady",ready);
   patcher.clearError();
  }
  return;
 }
 if(index==XeDataAccess::dependency().loadIndex) {
  if(!dataAccessSeen)dataAccessSeen=XeDataAccess::processKext(patcher,index,address,size);
  return;
 }
 if(graphics.loadIndex==index) {
  if(graphicsSeen)return;
  graphicsSeen=true;
  if(systemWakeRecovery) {
   const char *sym="__ZN13IOFramebuffer16setWSAAAttributeEjj";
   // Lilu resolves the symbol, verifies the image range, and builds the
   // entrance trampoline. This wrapper needs no private implementation ABI.
   KernelPatcher::RouteRequest route(sym,wrapWSAAAttribute,orgWSAAAttribute);
   const bool ready=patcher.routeMultiple(index,&route,1,address,size);patcher.clearError();
   xe.setHostWakeRecoveryReady(ready);
   // Bound the host function by its own image, not by the Intel donor.
   const auto event=patcher.solveSymbol<mach_vm_address_t>(index,"__ZN13IOFramebuffer11handleEventEiPv",address,size);
   patcher.clearError();
   const auto eventLength=hostFunctionExtent(event,address,size);
   const bool paging=eventLength && HostPowerContract::paging(reinterpret_cast<const uint8_t *>(event),eventLength,hostInstructionSize);
   xe.setHostFramebufferEvent(paging?event:0);
   IORegistryEntry::getRegistryRoot()->setProperty("WEXHostPagingContractReady",paging);
   IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIWakeRecoveryReady",ready);
  }
  if(resetFramebuffer==FB_NONE)return;
  gIOFBVerboseBootPtr=patcher.solveSymbol<uint8_t *>(index,"__ZL16gIOFBVerboseBoot",address,size);
  if(gIOFBVerboseBootPtr){KernelPatcher::RouteRequest route("__ZN13IOFramebuffer6initFBEv",wrapFramebufferInit,orgFramebufferInit);patcher.routeMultiple(index,&route,1,address,size);}
  patcher.clearError();return;
 }
 if(xe.isIntelDonor(index)) {
  if(!dataAccessSeen)processLoadedDependency(patcher,XeDataAccess::dependency());
  if(!surfaceSeen)processLoadedDependency(patcher,surface);
  if(xe.isIntelFramebuffer(index) && xe.requiresIntelProvider() && !providerSeen)
   processLoadedDependency(patcher,kextAppleIntelTGLCompat);
  if(!graphicsSeen && (systemWakeRecovery || resetFramebuffer!=FB_NONE))processLoadedDependency(patcher,graphics);
  if(systemWakeRecovery && !accelSeen)processLoadedDependency(patcher,accelFamily);
  if(backlightEnabled && !backlightSeen)processLoadedDependency(patcher,backlight);
  IORegistryEntry::getRegistryRoot()->setProperty("WEXHostDependenciesSeen",dependenciesSeen(index));
  // Always install the donor's probe/start guards in this callback. Returning
  // with queued work lets OSKext enter the unpatched native lifecycle. Unknown
  // host ABIs remain rejected by the existing donor readiness gates.
 }
 xe.processKext(patcher,index,address,size);
}
// Observe the subscriber that owns a sleep scope. Native system_will_sleep
// ignores its event-number argument and loops over every display pipe; a
// controller-wide asleep flag cannot identify which subscriber already woke.
IOReturn XeHost::wrapDisplayChange(void *pipe,void *reference,IOFramebuffer *fb,int event,void *argument) {
 auto h=callbackHost;
 const bool tracked=(event==3 || event==4) && !argument && h->xe.ownedDisplay13Framebuffer(fb);
 if((event!=3 && event!=4) || argument)
  return FunctionCast(wrapDisplayChange,h->orgDisplayChange)(pipe,reference,fb,event,argument);
 SleepScope scope;scope.thread=reinterpret_cast<uintptr_t>(current_thread());scope.fb=tracked && event==3 ? fb : nullptr;
 auto saved=IOSimpleLockLockDisableInterrupt(h->sleepScopeLock);
 scope.next=h->sleepScopes;h->sleepScopes=&scope;
 IOSimpleLockUnlockEnableInterrupt(h->sleepScopeLock,saved);
 const auto result=FunctionCast(wrapDisplayChange,h->orgDisplayChange)(pipe,reference,fb,event,argument);
 saved=IOSimpleLockLockDisableInterrupt(h->sleepScopeLock);
 // The list holds stack records only during a synchronous native callback.
 // Another thread or a nested subscriber must never inherit this decision.
 auto link=&h->sleepScopes;
 while(*link && *link!=&scope)link=&(*link)->next;
 if(*link)*link=scope.next;
 SleepEntry *entry=nullptr;
 if(tracked)for(auto &item:h->sleepEntries)if(item.fb==fb){entry=&item;break;}
 if(tracked && !entry)for(auto &item:h->sleepEntries)if(!item.pending){entry=&item;item.fb=fb;break;}
 if(entry && result==kIOReturnSuccess) {
  if(event==3 && scope.reached){entry->pending=true;++h->sleepNotifications;}
  if(event==4){entry->pending=false;++h->wakeNotifications;}
 }
 uint32_t pending=0;
 for(auto &item:h->sleepEntries)if(item.pending)++pending;
 uint32_t receipt[]={1,h->sleepNotifications,h->wakeNotifications,h->repeatedSleep,pending,
     static_cast<uint32_t>(event),scope.reached,scope.skipped,static_cast<uint32_t>(result)};
 IOSimpleLockUnlockEnableInterrupt(h->sleepScopeLock,saved);
 IORegistryEntry::getRegistryRoot()->setProperty("WEXHostSleepScopes",receipt,sizeof(receipt));
 return result;
}

uint32_t XeHost::pendingSleepDisplays(void *accelerator) {
 auto h=callbackHost;
 auto bytes=static_cast<uint8_t *>(accelerator);
 auto machine=*reinterpret_cast<uint8_t **>(bytes+0x378);
 const auto count=reinterpret_cast<uint32_t (*)(void *)>(h->sleepFBCount)(machine);
 if(!count || !h->sleepScopeLock)return count;
 const auto thread=reinterpret_cast<uintptr_t>(current_thread());
 auto saved=IOSimpleLockLockDisableInterrupt(h->sleepScopeLock);
 SleepScope *scope=h->sleepScopes;
 while(scope && scope->thread!=thread)scope=scope->next;
 bool duplicate=false;
 if(scope && scope->fb) {
  scope->reached=true;
  for(auto &entry:h->sleepEntries)if(entry.fb==scope->fb && entry.pending){duplicate=true;break;}
  scope->skipped=duplicate;
  if(duplicate)++h->repeatedSleep;
 }
 const auto repeated=h->repeatedSleep;
 IOSimpleLockUnlockEnableInterrupt(h->sleepScopeLock,saved);
 if(!duplicate)return count;
 // Skip only the repeated preparation loop. Orphan cleanup, the hardware
 // sleep phase and every native wake notification still execute normally.
 const auto depth=*reinterpret_cast<volatile uint32_t *>(machine+0x10c);
 uint32_t record[]={repeated,count,depth};
 IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIDarkWakeParked",record,sizeof(record));
 return 0;
}

IOReturn XeHost::wrapWSAAAttribute(IOFramebuffer *fb,uint32_t attr,uint32_t value) {
 // IOGraphics sends WillExitDefer under its global gate before calling the
 // vendor attribute setter. Reject a failed scanout before queue-idle can
 // sleep in that gate. No flip is acknowledged or GPU allocation released.
 const auto state=value&kIOWSAA_StateMask;
 if(attr==kIOWindowServerActiveAttribute &&
    (state==kIOWSAA_Accelerated || state==kIOWSAA_Unaccelerated) &&
    callbackHost->xe.failedDSIWakeFor(fb))return kIOReturnNotReady;
 return FunctionCast(wrapWSAAAttribute,callbackHost->orgWSAAAttribute)(fb,attr,value);
}

bool XeHost::wrapPanelSetDisplay(IOService *panel,IOService *display) {
 // Add only the missing UID-19 profile for an actual internal 16-bit PWM
 // display. Keep Apple's/custom profiles and the native selection algorithm.
 // No string patch, product-ID list, ACPI path or model-specific table.
 auto provider=panel ? panel->getProvider() : nullptr;
 auto uid=provider ? OSDynamicCast(OSString,provider->getProperty("_UID")) : nullptr;
 auto product=display ? OSDynamicCast(OSNumber,display->getProperty("DisplayProductID")) : nullptr;
 bool owned=false;
 IOFramebuffer *framebuffer=nullptr;
 for(auto entry=display;entry;entry=entry->getProvider()) {
  // IOGraphicsFamily is loaded later than this Lilu plugin. A direct
  // IOFramebuffer::metaClass import is not available in our dependency set.
  if(!framebuffer)framebuffer=static_cast<IOFramebuffer *>(entry->metaCast("IOFramebuffer"));
  if(entry==callbackHost->backlightGPU){owned=true;break;}
 }
 // setDisplay precedes publication of IODisplayParameters. Query the same
 // three-word (current,min,max) attribute as AppleIntelPanelA itself.
 uintptr_t pwm[3] {};
 const bool internal=display && display->metaCast("AppleBacklightDisplay");
 const bool range=owned && internal && framebuffer &&
  framebuffer->getAttributeForConnection(0,'bklt',pwm)==kIOReturnSuccess;
 OSDictionary *heldProfiles=nullptr;
 if(range && uid && product && pwm[1]<=UINT32_MAX && pwm[2]<=UINT32_MAX &&
    XeBacklightProfile::eligible(uid->isEqualTo("19") ? 19 : 0,pwm[1],pwm[2],internal)) {
  auto original=OSDynamicCast(OSDictionary,panel->getProperty("ApplePanels"));
  if(original && !panel->getProperty("ApplePanel")) {
   char key[32];snprintf(key,sizeof(key),"F19T%04x",product->unsigned32BitValue());
   if(!original->getObject(key) && !original->getObject("F19")) {
    auto copy=OSDictionary::withDictionary(original);
    auto curve=OSData::withBytes(XeBacklightProfile::curve,sizeof(XeBacklightProfile::curve));
    if(copy && curve && copy->setObject(key,curve) && panel->setProperty("ApplePanels",copy)) {
     // Native removes ApplePanels before retaining its selected OSData in
     // ApplePanel. Keep the candidate dictionary alive across that handoff.
     heldProfiles=copy;
     panel->setProperty("WhateverXeBacklightProfile",key);
    }
    if(curve)curve->release();
    if(copy && !heldProfiles)copy->release();
   }
  }
 }
 uint32_t inputs[]={1,owned,uid && uid->isEqualTo("19"),range ? static_cast<uint32_t>(pwm[1]) : ~0U,
  range ? static_cast<uint32_t>(pwm[2]) : 0,0};
 const bool result=FunctionCast(wrapPanelSetDisplay,callbackHost->orgPanelSetDisplay)(panel,display);
 if(heldProfiles)heldProfiles->release();
 if(panel) {
  inputs[5]=result;
  panel->setProperty("WhateverXeBacklightMatch",inputs,sizeof(inputs));
 }
 return result;
}
void XeHost::wrapFramebufferInit(IOFramebuffer *fb) {
	bool backCopy = callbackHost->gotConsoleVinfo && callbackHost->resetFramebuffer == FB_COPY;
	bool zeroFill  = callbackHost->gotConsoleVinfo && callbackHost->resetFramebuffer == FB_ZEROFILL;
	auto &info = callbackHost->consoleVinfo;

	// Copy back usually happens in a separate call to frameBufferInit
	// Furthermore, v_baseaddr may not be available on subsequent calls, so we have to copy
	if (backCopy && info.v_baseaddr) {
		// Note, this buffer is left allocated and never freed, yet there actually is no way to free it.
		callbackHost->consoleBuffer = Buffer::create<uint8_t>(info.v_rowbytes * info.v_height);
		if (callbackHost->consoleBuffer)
			lilu_os_memcpy(callbackHost->consoleBuffer, reinterpret_cast<uint8_t *>(info.v_baseaddr), info.v_rowbytes * info.v_height);
		else
			SYSLOG("wex", "console buffer allocation failure");
		// Even if we may succeed next time, it will be unreasonably dangerous
		info.v_baseaddr = 0;
	}

	uint8_t verboseBoot = *callbackHost->gIOFBVerboseBootPtr;
	// For back copy we need a console buffer and no verbose
	backCopy = backCopy && callbackHost->consoleBuffer && !verboseBoot;

	// Now check if the resolution and parameters match
	if (backCopy || zeroFill) {
		IODisplayModeID mode;
		IOIndex depth;
		IOPixelInformation pixelInfo;

		if (fb->getCurrentDisplayMode(&mode, &depth) == kIOReturnSuccess &&
			fb->getPixelInformation(mode, depth, kIOFBSystemAperture, &pixelInfo) == kIOReturnSuccess) {
			DBGLOG("wex", "fb info 1: %d:%d %u:%u:%u",
				   mode, depth, pixelInfo.bytesPerRow, pixelInfo.bytesPerPlane, pixelInfo.bitsPerPixel);
			DBGLOG("wex", "fb info 2: %u:%u %s %u:%u:%u",
				   pixelInfo.componentCount, pixelInfo.bitsPerComponent, pixelInfo.pixelFormat, pixelInfo.flags, pixelInfo.activeWidth, pixelInfo.activeHeight);

			if (info.v_rowbytes != pixelInfo.bytesPerRow || info.v_width != pixelInfo.activeWidth ||
				info.v_height != pixelInfo.activeHeight || info.v_depth != pixelInfo.bitsPerPixel) {
				backCopy = zeroFill = false;
				DBGLOG("wex", "this display has different mode");
			}
		} else {
			DBGLOG("wex", "failed to obtain display mode");
			backCopy = zeroFill = false;
		}
	}

	// For whatever reason not resetting Intel framebuffer (back copy mode) twice works better.
	if (!backCopy) *callbackHost->gIOFBVerboseBootPtr = 1;
	FunctionCast(wrapFramebufferInit, callbackHost->orgFramebufferInit)(fb);
	if (!backCopy) *callbackHost->gIOFBVerboseBootPtr = verboseBoot;

	// Finish the framebuffer initialisation by filling with black or copying the image back.
	if (FramebufferViewer::getVramMap(fb)) {
		auto src = reinterpret_cast<uint8_t *>(callbackHost->consoleBuffer);
		auto dst = reinterpret_cast<uint8_t *>(FramebufferViewer::getVramMap(fb)->getVirtualAddress());
		if (backCopy) {
			DBGLOG("wex", "attempting to copy...");
			// Here you can actually draw at your will, but looks like only on Intel.
			// On AMD you technically can draw too, but it happens for a very short while, and is not worth it.
			lilu_os_memcpy(dst, src, info.v_rowbytes * info.v_height);
		} else if (zeroFill) {
			// On AMD we do a zero-fill to ensure no visual glitches.
			DBGLOG("wex", "doing zero-fill...");
			memset(dst, 0, info.v_rowbytes * info.v_height);
		}
	}
}

