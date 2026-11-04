// Copyright © 2018 vit9696. Derived from WhateverGreen (BSD-3-Clause).
#include "kern_host.hpp"
#include "kern_xelp_device.hpp"
#include "kern_xe_data_access.hpp"
#include "kern_backlight_profile.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_iokit.hpp>
#include <IOKit/pci/IOPCIDevice.h>
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
void XeHost::init() {
 callbackHost=this;
 PE_parse_boot_argn("gfxrst",&resetFramebuffer,sizeof(resetFramebuffer));
 if(resetFramebuffer>=FB_TOTAL)resetFramebuffer=FB_DETECT;
 lilu.onPatcherLoadForce([](void *u,KernelPatcher &p){static_cast<XeHost *>(u)->processKernel(p);},this);
 lilu.onKextLoadForce(nullptr,0,[](void *u,KernelPatcher &p,size_t i,mach_vm_address_t a,size_t s){static_cast<XeHost *>(u)->processKext(p,i,a,s);},this);
 lilu.onKextLoadForce(&graphics);
 lilu.onKextLoadForce(&accelFamily);
 lilu.onKextLoadForce(&backlight);
 xe.init();
 XeDataAccess::init();
}
void XeHost::processKernel(KernelPatcher &patcher) {
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
void XeHost::processKext(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
 if(!active)return;
 if(accelFamily.loadIndex==index) {
  if(!systemWakeRecovery)return;
  // Only the first loop of system_will_sleep(false) opens mode changes.
  // Leave its subsequent orphan cleanup and the hardware sleep phase native.
  const auto sym=[&](const char *name) {
   auto v=patcher.solveSymbol<mach_vm_address_t>(index,name,address,size);patcher.clearError();return v;
  };
  const uint8_t before[]={0x48,0x8b,0xbb,0x78,3,0,0,0xe8,0xdc,0xcb,0xfc,0xff,0x41,0xb6,1,0x85,0xc0,0x74,0x37};
  const uint8_t asleep[]={0xf6,0x83,0x78,0x0c,0,0,1};
  const uint8_t nesting[]={0x8b,0x83,0x0c,1,0,0,0x8d,0x48,1,0x89,0x8b,0x0c,1,0,0};
  sleepFBCount=sym("__ZNK21IOAccelDisplayMachine19getFramebufferCountEv");
  sleepFBAt=sym("__ZNK21IOAccelDisplayMachine16getIOFramebufferEj");
  bool ready=false;
  if(size>0x412f0 && sleepFBCount==address+0xde8e && sleepFBAt==address+0xdfc0 &&
     sym("__ZN22IOGraphicsAccelerator217system_will_sleepEib")==address+0x41144 &&
     !memcmp(reinterpret_cast<void *>(address+0x411bc),asleep,sizeof(asleep)) &&
     !memcmp(reinterpret_cast<void *>(address+0xe104),nesting,sizeof(nesting)) &&
     !memcmp(reinterpret_cast<void *>(address+0x412a6),before,sizeof(before))) {
   // Pass the owning accelerator instead of just the display machine.
   uint8_t code[]={0x48,0x89,0xdf,0xe8,0,0,0,0,0x90,0x90,0x90,0x90};
   const auto distance=static_cast<int64_t>(reinterpret_cast<uintptr_t>(pendingSleepDisplays)-(address+0x412ae));
   if(distance==static_cast<int32_t>(distance)) {
    const auto rel=static_cast<int32_t>(distance);memcpy(code+4,&rel,4);
    patcher.routeBlock(address+0x412a6,code,sizeof(code));
    ready=!memcmp(reinterpret_cast<void *>(address+0x412a6),code,sizeof(code));patcher.clearError();
   }
  }
  IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIDarkWakeReady",ready);
  // Observe the queue under its existing native gate. No driver locks or
  // live object pointers are retained by a registry serializer.
  const auto idle=sym("__ZN18IOAccelDisplayPipe28transaction_queue_idle_gatedEv");
  const auto framebuffer=sym("__ZNK18IOAccelDisplayPipe14getFramebufferEv");
  const uint8_t idleFields[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x50,0x48,0x89,0xfb,0x8b,0x87,0x38,2,0,0,0x8b,0x8f,0x3c,2,0,0};
  const uint8_t framebufferField[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x87,0x98,0,0,0,0x5d,0xc3};
  const auto within=[&](mach_vm_address_t fn,size_t bytes){return fn>=address && fn-address<=size && bytes<=size-(fn-address);};
  bool queueReady=false;
  if(ready && within(idle,sizeof(idleFields)) && within(framebuffer,sizeof(framebufferField)) &&
     !memcmp(reinterpret_cast<void *>(idle),idleFields,sizeof(idleFields)) &&
     !memcmp(reinterpret_cast<void *>(framebuffer),framebufferField,sizeof(framebufferField))) {
   KernelPatcher::RouteRequest route("__ZN18IOAccelDisplayPipe28transaction_queue_idle_gatedEv",wrapDisplayQueueIdle,orgDisplayQueueIdle);
   queueReady=patcher.routeMultiple(index,&route,1,address,size);patcher.clearError();
  }
  IORegistryEntry::getRegistryRoot()->setProperty("WEXDisplayQueueObserverReady",queueReady);
  return;
 }
 if(backlight.loadIndex==index) {
  if(backlightEnabled) {
   KernelPatcher::RouteRequest route("__ZN15AppleIntelPanel10setDisplayEP9IODisplay",wrapPanelSetDisplay,orgPanelSetDisplay);
   const bool ready=patcher.routeMultiple(index,&route,1,address,size);
   IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeBacklightProfileReady",ready);
   patcher.clearError();
  }
  return;
 }
 if(XeDataAccess::processKext(patcher,index,address,size))return;
 if(graphics.loadIndex==index) {
  if(systemWakeRecovery) {
   const char *sym="__ZN13IOFramebuffer16setWSAAAttributeEjj";
   const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,sym,address,size);
   patcher.clearError();
   const uint8_t prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x68};
   bool ready=false;
   if(size>=0x1cd64+sizeof(prologue) && fn==address+0x1cd64 && !memcmp(reinterpret_cast<void *>(fn),prologue,sizeof(prologue))) {
    KernelPatcher::RouteRequest route(sym,wrapWSAAAttribute,orgWSAAAttribute);
    ready=patcher.routeMultiple(index,&route,1,address,size);patcher.clearError();
   }
   IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIWakeRecoveryReady",ready);
  }
  if(resetFramebuffer==FB_NONE)return;
  gIOFBVerboseBootPtr=patcher.solveSymbol<uint8_t *>(index,"__ZL16gIOFBVerboseBoot",address,size);
  if(gIOFBVerboseBootPtr){KernelPatcher::RouteRequest route("__ZN13IOFramebuffer6initFBEv",wrapFramebufferInit,orgFramebufferInit);patcher.routeMultiple(index,&route,1,address,size);}
  patcher.clearError();return;
 }
 xe.processKext(patcher,index,address,size);
}
void XeHost::recordDisplayQueue(void *pipe,uint32_t serial,uint32_t phase) {
 auto bytes=static_cast<uint8_t *>(pipe);
 auto fb=*reinterpret_cast<IOService **>(bytes+0x98);
 if(!xe.ownedDisplay13Framebuffer(fb))return;
 const auto index=*reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x1DC);
 uint64_t record[54] {1,serial,phase,index};
 clock_get_uptime(record+4);
 record[5]=*reinterpret_cast<uint32_t *>(bytes+0x238);
 record[6]=*reinterpret_cast<uint32_t *>(bytes+0x23c);
 record[7]=*reinterpret_cast<uint64_t *>(bytes+0x248);
 record[8]=*reinterpret_cast<uint64_t *>(bytes+0x250);
 record[9]=*reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x4284);
 record[10]=*reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(fb)+0x199);
 auto accelerator=*reinterpret_cast<uint8_t **>(bytes+0x88);
 if(accelerator) {
  record[11]=*reinterpret_cast<uint32_t *>(accelerator+0xc78);
  auto machine=*reinterpret_cast<uint8_t **>(accelerator+0x378);
  if(machine)record[12]=*reinterpret_cast<uint32_t *>(machine+0x10c);
 }
 record[13]=*reinterpret_cast<uint32_t *>(bytes+0x29c);
 record[14]=*reinterpret_cast<uint32_t *>(bytes+0x2a4);
 record[15]=reinterpret_cast<uintptr_t>(pipe);
 // Embedded event slots only: do not dereference transactions whose lifetime
 // can end when the original action drops its gate to wait.
 memcpy(record+16,bytes+0x118,4*0x48);
 record[52]=*reinterpret_cast<uint8_t *>(bytes+0x280);
 record[53]=*reinterpret_cast<uint8_t *>(bytes+0x299);
 const char *enter[]={"WEXDisplayQueueEnter0","WEXDisplayQueueEnter1","WEXDisplayQueueEnter2"};
 const char *exit[]={"WEXDisplayQueueExit0","WEXDisplayQueueExit1","WEXDisplayQueueExit2"};
 IORegistryEntry::getRegistryRoot()->setProperty((phase==1?enter:exit)[index],record,sizeof(record));
}

void XeHost::wrapDisplayQueueIdle(void *pipe) {
 auto h=callbackHost;
 const auto serial=__atomic_add_fetch(&h->queueObservation,1,__ATOMIC_RELAXED);
 h->recordDisplayQueue(pipe,serial,1);
 FunctionCast(wrapDisplayQueueIdle,h->orgDisplayQueueIdle)(pipe);
 h->recordDisplayQueue(pipe,serial,2);
}

uint32_t XeHost::pendingSleepDisplays(void *accelerator) {
 auto h=callbackHost;
 auto bytes=static_cast<uint8_t *>(accelerator);
 auto machine=*reinterpret_cast<uint8_t **>(bytes+0x378);
 const auto count=reinterpret_cast<uint32_t (*)(void *)>(h->sleepFBCount)(machine);
 if(!count || !(*reinterpret_cast<volatile uint32_t *>(bytes+0xc78)&1))return count;
 const auto depth=*reinterpret_cast<volatile uint32_t *>(machine+0x10c);
 if(depth<count)return count;
 // Every subscriber in this machine must be parked. FB0 alone cannot
 // establish whether an external display acquired a new wake scope.
 for(unsigned i=0;i<count;++i) {
  auto fb=reinterpret_cast<IOService *(*)(void *,unsigned)>(h->sleepFBAt)(machine,i);
  if(!h->xe.parkedDisplay13Framebuffer(fb))return count;
 }
 // Already parked by a full two-phase notification, with no intervening
 // DidWake. The real resume must close the original scope exactly once.
 uint32_t record[]={__atomic_add_fetch(&h->repeatedSleep,1,__ATOMIC_RELAXED),count,depth};
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

