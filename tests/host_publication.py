#!/usr/bin/env python3
"""Replay actual host admission: no PCI reads before successful publication."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
adapter=(r/'WhateverXe/kern_xelp_device.hpp').read_text();adapter=adapter[adapter.index('namespace XELP {'):adapter.rindex('#endif')]
s=(r/'WhateverXe/kern_host.cpp').read_text();body=s[s.index('void XeHost::init'):s.index('bool XeHost::wrapPanelSetDisplay')]
header=(r/'WhateverXe/kern_host.hpp').read_text()
host_state=header[header.index(' bool active'):header.index(' void processLoadedDependency')]
xe_source=(r/'WhateverXe/kern_xe.cpp').read_text()
provider_classifier='\n'.join(re.search(r'bool XE::'+name+r'\(.*?\n\}',xe_source,re.S)[0]
                            for name in ['isGraphicsStack','requiresIntelProvider','isIntelProvider','isIntelFramebuffer'])
surface_setter=re.search(r'void XE::setHostSurfaceABI\(.*?\n\}',xe_source,re.S)[0]
code=r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <map>
#include <string>
#include <array>
#define arrsize(x) (sizeof(x)/sizeof((x)[0]))
#define SYSLOG(...) ((void)0)
#include "kern_xelp_platform.hpp"
#include "kern_host_power_contract.hpp"
#include "kern_xelp_native_placement.hpp"
size_t hostInstructionSize(const uint8_t*,size_t){return 0;}
size_t hostFunctionExtent(uintptr_t,uintptr_t,size_t){return 0;}
using mach_vm_address_t=uintptr_t;
void clock_get_uptime(uint64_t *value){*value=0;}
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}void setProperty(const char*,bool){}void setProperty(const char*,const void*,size_t){}};
std::vector<int> events;bool publish=true,published=false;uint32_t identity=0x9a498086;uint8_t revision=0;
struct Object {virtual ~Object()=default;};
struct OSData:Object {uint32_t value=0;void *getBytesNoCopy(){return &value;}size_t getLength(){return 4;}};
OSData fake;Object customModel;bool hasModel=false;unsigned modelWrites=0;Object *deviceProperty=nullptr;
struct IOPCIDevice {
 const char *name="GFX0";unsigned renames=0;
 uint32_t configRead32(int){assert(published);events.push_back(2);return identity;}
 uint8_t configRead8(int){assert(published);events.push_back(3);return revision;}
 uint16_t configRead16(int){assert(published);events.push_back(4);return 0x300;}
 const char *getName(){return name;}Object *getProperty(const char *key){return !strcmp(key,"device-id")?deviceProperty:!strcmp(key,"model")&&hasModel?&customModel:nullptr;}
 void setProperty(const char *,const char *){}void setProperty(const char *key,const void *value,unsigned size){if(!strcmp(key,"model")){assert(value && size>1);++modelWrites;}}void setProperty(const char *,uint64_t,unsigned){}
};
#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
struct DeviceInfo {
 enum class FirmwareVendor{Apple,Other};FirmwareVendor firmwareVendor=FirmwareVendor::Other;
 IOPCIDevice *videoBuiltin=nullptr,*managementEngine=nullptr;bool reportedFramebufferIsConnectorLess=false;
 static DeviceInfo value;static DeviceInfo *create(){return &value;}static void deleter(DeviceInfo *){events.push_back(9);}
};DeviceInfo DeviceInfo::value;
namespace WIOKit {bool awaitPublishing(IOPCIDevice *){events.push_back(1);published=publish;return publish;}void renameDevice(IOPCIDevice *device,const char *name){device->name=name;++device->renames;events.push_back(5);}}
// DEVICE_ADAPTER
#define KMOD_MAX_NAME 64
struct kmod_info_t {const char *name;uintptr_t address=0x1000;size_t size=0x1000;kmod_info_t *next=nullptr;};
kmod_info_t *loadedHead=nullptr;bool validImage=true;unsigned imageUpdates=0;
std::map<std::string,uintptr_t> symbols;
static XELPNative::SurfaceABI tglHostSurfaceABI[2]{};
struct KernelPatcher {
 struct KextInfo {static constexpr size_t Unloaded=0;const char *id=nullptr;size_t loadIndex=0;};
 enum class Error {NoError,BadImage};Error error=Error::NoError;Error getError(){return error;}
 static constexpr int KernelID=0;
 uintptr_t solveSymbol(int,const char *name){return !strcmp(name,"_kmod") && loadedHead ? reinterpret_cast<uintptr_t>(&loadedHead):0;}
 size_t updateRunningInfo(size_t,uintptr_t,size_t size){++imageUpdates;error=validImage?Error::NoError:Error::BadImage;return size;}
 template<class T>T solveSymbol(size_t,const char *name,mach_vm_address_t,size_t){return (T)symbols[name];}
 struct RouteRequest {template<class T>RouteRequest(const char*,T,uintptr_t&) {}};
 bool routeMultiple(size_t,RouteRequest*,unsigned,mach_vm_address_t,size_t){return true;}
 void routeBlock(uintptr_t,const uint8_t*,size_t){}
 void clearError(){error=Error::NoError;}
};
bool PE_parse_boot_argn(const char*,void*,size_t){return false;}
struct {
 using PatcherCallback=void(*)(void*,KernelPatcher&);
 std::vector<std::pair<PatcherCallback,void*>> kernelCallbacks;
 void onPatcherLoadForce(PatcherCallback callback,void *user){kernelCallbacks.push_back({callback,user});}
 template<class F>void onKextLoadForce(void*,unsigned,F,void*){}
 void onKextLoadForce(KernelPatcher::KextInfo*){}
} lilu;
namespace XeDataAccess {
 void init(){}
 KernelPatcher::KextInfo sandbox{"com.apple.security.sandbox",8};unsigned installs=0;
 KernelPatcher::KextInfo &dependency(){return sandbox;}
 bool processKext(KernelPatcher&,size_t index,mach_vm_address_t,size_t){assert(index==8);++installs;return true;}
 void processKernel(KernelPatcher &){assert(published);events.push_back(7);}
}
struct Graphics:KernelPatcher::KextInfo {Graphics(size_t i=4){loadIndex=i;id="com.apple.iokit.IOGraphicsFamily";}bool off=false;void switchOff(){off=true;}} graphics;
KernelPatcher::KextInfo surface{"com.apple.iokit.IOSurface",6},backlight{"com.apple.driver.AppleBacklight",5},accelFamily{"com.apple.iokit.IOAcceleratorFamily2",7},kextAppleIntelTGLCompat{"org.b00t0x.AppleIntelTGLCompat",3};
KernelPatcher::KextInfo kextIntelTGL{"com.apple.driver.AppleIntelTGLGraphics",2},kextIntelTGLFb{"com.apple.driver.AppleIntelTGLGraphicsFramebuffer",1};
using IOSimpleLock=int;int scopeLock=0;uintptr_t fakeThread=1;
IOSimpleLock *IOSimpleLockAlloc(){return &scopeLock;}
int IOSimpleLockLockDisableInterrupt(IOSimpleLock*){return 0;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock*,int){}
void *current_thread(){return reinterpret_cast<void*>(fakeThread);}
using IOService=IOPCIDevice;using IOReturn=int;using IOFramebuffer=IOPCIDevice;
constexpr uint32_t kIOWindowServerActiveAttribute=1,kIOWSAA_StateMask=15,kIOWSAA_Accelerated=1,kIOWSAA_Unaccelerated=0;
constexpr IOReturn kIOReturnSuccess=0,kIOReturnNotReady=-1;
#define FunctionCast(fn,addr) reinterpret_cast<decltype(&fn)>(addr)
struct XeHost {
 static XeHost *callbackHost;
 HOST_STATE
 void processLoadedDependency(KernelPatcher&,KernelPatcher::KextInfo&);
 uintptr_t sleepFBCount=0,sleepFBAt=0;uint32_t repeatedSleep=0,sleepNotifications=0,wakeNotifications=0;
 IOSimpleLock *sleepScopeLock=nullptr;
 struct SleepEntry {IOService *fb=nullptr;bool pending=false;};SleepEntry sleepEntries[3]{};
 struct SleepScope {SleepScope *next=nullptr;uintptr_t thread=0;IOService *fb=nullptr;bool reached=false,skipped=false;};SleepScope *sleepScopes=nullptr;
 uintptr_t orgDisplayChange=0;
 static IOReturn wrapDisplayChange(void*,void*,IOFramebuffer*,int,void*);
 static uint32_t pendingSleepDisplays(void*);
 uintptr_t orgWSAAAttribute=0;
 static IOReturn wrapWSAAAttribute(IOFramebuffer*,uint32_t,uint32_t);
 bool backlightEnabled=false;enum {FB_DETECT,FB_RESET,FB_COPY,FB_ZEROFILL,FB_NONE,FB_TOTAL};uint32_t resetFramebuffer=FB_NONE;
 IOPCIDevice *backlightGPU=nullptr;
 struct vc_info{};vc_info consoleVinfo;bool gotConsoleVinfo=false;
 struct XE {
 void init(){}
 void captureFirmware(IOPCIDevice*,const XELP::Profile&){events.push_back(10);}
 bool defer=false;bool isIntelDonor(size_t i)const{return defer && (i==1 || i==2);}
 bool isIntelProvider(size_t index)const;
 bool isGraphicsStack(size_t index)const;
 bool requiresIntelProvider()const;
 bool isIntelFramebuffer(size_t)const;Graphics frame{1};Graphics *currentFramebuffer=&frame;
 bool requireProvider=false,providerReady=false,providerOK=true;unsigned rejectedDonors=0;
 bool tglRequested=false,display13FramebufferRequested=false,adlpLoadOnly=false;
 void setHostSurfaceABI(unsigned,unsigned);
 void setHostFramebufferEvent(uintptr_t){}
 void setHostSleepReady(bool){}
 void setHostWakeRecoveryReady(bool){}
 std::vector<IOService*> owned;bool ownedDisplay13Framebuffer(IOService *f)const{return std::find(owned.begin(),owned.end(),f)!=owned.end();}
 IOFramebuffer *degraded=nullptr; bool failedDSIWakeFor(IOFramebuffer *f) const {return f && f==degraded;}
 void processKernel(KernelPatcher &,DeviceInfo *){events.push_back(8);}
 void processKext(KernelPatcher&,size_t index,mach_vm_address_t,size_t){
  if(index==3){providerReady=providerOK;return;}
  if(requireProvider && isIntelFramebuffer(index) && !providerReady)++rejectedDonors;
  events.push_back(100+index);
 }
 } xe;
 void init();
 void processKernel(KernelPatcher &);
 void captureFirmware();
 void processKext(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static void wrapPanelSetDisplay(){}static void wrapFramebufferInit(){}
 uintptr_t orgPanelSetDisplay=0,orgFramebufferInit=0;uint8_t *gIOFBVerboseBootPtr=nullptr;
};
'''.replace('// DEVICE_ADAPTER',adapter).replace('HOST_STATE',host_state)+provider_classifier.replace('XE::','XeHost::XE::')+surface_setter.replace('XE::','XeHost::XE::')+body+r'''
XeHost *XeHost::callbackHost;
static IOReturn vendor(IOFramebuffer*,uint32_t,uint32_t){return 42;}
int main(){KernelPatcher patcher;IOPCIDevice gpu,mei;
// The early callback copies read-only firmware facts. GPU configuration,
// hooks and native admission still wait for a Graphics image. Publication
// failure cannot be bypassed even for the passive capture.
for(bool available:{false,true}) {
 XeHost host;events.clear();publish=available;published=false;
 DeviceInfo::value.videoBuiltin=&gpu;DeviceInfo::value.managementEngine=&mei;
 gpu.name="GFX0";mei.name="HECI";modelWrites=0;lilu.kernelCallbacks.clear();host.init();
 for(auto callback:lilu.kernelCallbacks)callback.first(callback.second,patcher);
 assert(!host.kernelProcessed && !host.active);
 assert(gpu.name==std::string("GFX0") && mei.name==std::string("HECI") && !modelWrites);
 assert(std::count(events.begin(),events.end(),10)==unsigned(available));
 for(auto event:events)assert(event!=4 && event!=5 && event!=7 && event!=8 && event<100);
}
// Host-only notifications must not discover the GPU or mutate firmware state.
for(unsigned id:{0U,4U,5U,6U,7U,8U,99U}) {
 XeHost host;events.clear();published=false;
 host.processKext(patcher,id,0,0);
 assert(events.empty() && !host.kernelProcessed && !host.active);
}
// Either driver or its provider can be first in Lilu's loaded-image walk.
for(unsigned id:{1U,2U,3U}) {
 XeHost host;events.clear();publish=true;published=false;identity=0x9a498086;
 DeviceInfo::value.videoBuiltin=&gpu;DeviceInfo::value.managementEngine=nullptr;
 host.processKext(patcher,id,0,0);
 assert(host.kernelProcessed && host.active && events.front()==1);
 assert(std::count(events.begin(),events.end(),8)==1);
 host.processKext(patcher,id,0,0);
 assert(std::count(events.begin(),events.end(),8)==1);
}
events.clear();
XeHost exercised;XeHost::callbackHost=&exercised;exercised.orgWSAAAttribute=reinterpret_cast<uintptr_t>(&vendor);
assert(XeHost::wrapWSAAAttribute(&gpu,1,1)==42);exercised.xe.degraded=&gpu;
assert(XeHost::wrapWSAAAttribute(&gpu,1,0x101)==kIOReturnNotReady && XeHost::wrapWSAAAttribute(&gpu,1,0)==kIOReturnNotReady);
assert(XeHost::wrapWSAAAttribute(&mei,1,1)==42 && XeHost::wrapWSAAAttribute(&gpu,2,1)==42 && XeHost::wrapWSAAAttribute(&gpu,1,3)==42);
// Replay mixed DarkWake with every subscriber ordering and awake subset.
alignas(8) uint8_t accel[0xd00]{},machine[0x140]{};
*reinterpret_cast<uint8_t **>(accel+0x378)=machine;
exercised.sleepFBCount=reinterpret_cast<uintptr_t>(+[](void*)->uint32_t{return 3;});
exercised.sleepScopeLock=&scopeLock;
IOPCIDevice fb[3],foreign;exercised.xe.owned={fb,fb+1,fb+2};
for(unsigned i=0;i<3;++i)reinterpret_cast<IOService**>(machine)[i]=fb+i;
exercised.sleepFBAt=reinterpret_cast<uintptr_t>(+[](void *m,unsigned i)->IOService*{assert(i<3);return static_cast<IOService**>(m)[i];});
static uint8_t *testAccel;testAccel=accel;static unsigned cleanups,hardware;static bool reject;
exercised.orgDisplayChange=reinterpret_cast<uintptr_t>(+[](void*,void*,IOFramebuffer*,int event,void *argument)->IOReturn{
 auto machine=*reinterpret_cast<uint8_t**>(testAccel+0x378);auto &depth=*reinterpret_cast<uint32_t*>(machine+0x10c);
 if(reject)return -1;
 if(event==3 && !argument){depth+=XeHost::pendingSleepDisplays(testAccel);++cleanups;}
 if(event==4 && !argument){assert(depth>=3);depth-=3;}
 if(argument)++hardware;
 return 0;
});
unsigned ordering[]={0,1,2};do{for(unsigned mask=0;mask<8;++mask){
 for(auto &entry:exercised.sleepEntries)entry={};exercised.repeatedSleep=0;
 auto &depth=*reinterpret_cast<uint32_t*>(machine+0x10c);depth=0;cleanups=hardware=0;
 auto notify=[&](unsigned i,int event){assert(XeHost::wrapDisplayChange(nullptr,nullptr,fb+i,event,nullptr)==0);assert(XeHost::wrapDisplayChange(nullptr,nullptr,fb+i,event,reinterpret_cast<void*>(1))==0);};
 for(auto i:ordering)notify(i,3);assert(depth==9);
 for(unsigned cycle=0;cycle<4;++cycle){
  for(auto i:ordering)if(mask&(1U<<i))notify(i,4);
  assert(depth==9-3*__builtin_popcount(mask));
  // Peers can wake the accelerator while one subscriber remains parked.
  *reinterpret_cast<uint32_t*>(accel+0xc78)=0;
  for(auto i:ordering)notify(i,3);
  assert(depth==9);
 }
 for(auto i:ordering)notify(i,4);
 assert(depth==0 && exercised.repeatedSleep==4*(3-__builtin_popcount(mask)));
 assert(cleanups==15 && hardware==18+4*__builtin_popcount(mask));
 assert(!exercised.sleepScopes);
}}
while(std::next_permutation(ordering,ordering+3));
// Failed notifications do not establish a completed preparation scope.
for(auto &entry:exercised.sleepEntries)entry={};reject=true;
assert(XeHost::wrapDisplayChange(nullptr,nullptr,fb,3,nullptr)==-1);reject=false;
assert(XeHost::wrapDisplayChange(nullptr,nullptr,fb,3,nullptr)==0);
assert(*reinterpret_cast<uint32_t*>(machine+0x10c)==3);
// Other threads and direct calls cannot inherit an enclosing skip decision.
XeHost::SleepScope outer;outer.thread=2;outer.fb=fb;exercised.sleepScopes=&outer;
assert(XeHost::pendingSleepDisplays(accel)==3);exercised.sleepScopes=nullptr;
assert(XeHost::pendingSleepDisplays(accel)==3);
// A foreign nested subscriber on this thread is a barrier, not the owner
// of the outer subscriber's pending preparation.
outer.thread=1;outer.fb=fb;exercised.sleepScopes=&outer;
const auto before=*reinterpret_cast<uint32_t*>(machine+0x10c);
assert(XeHost::wrapDisplayChange(nullptr,nullptr,&foreign,3,nullptr)==0);
assert(*reinterpret_cast<uint32_t*>(machine+0x10c)==before+3);
assert(exercised.sleepScopes==&outer && !outer.reached);
assert(XeHost::wrapDisplayChange(nullptr,nullptr,&foreign,4,nullptr)==0);
assert(*reinterpret_cast<uint32_t*>(machine+0x10c)==before);
exercised.sleepScopes=nullptr;
// The real host caller must read enough bytes and publish both consumer ABIs
// through the real setter, not a fixture setter that silently discards them.
// Synthesized leaf getters follow the retained IOSurface field contracts.
for(bool original:{false,true})for(unsigned padding:{0U,17U,34U})for(bool framed:{false,true})
for(unsigned bad=0;bad<4;++bad){
 using A=XELPNative::SurfaceABI;
 std::array<uint8_t,512> image{};symbols.clear();
 const char *names[]={"__ZNK9IOSurface14getPixelFormatEv","__ZNK9IOSurface13getPlaneCountEv",
  "__ZNK9IOSurface12getSurfaceIDEv","__ZNK9IOSurface14getYCbCrMatrixEv"};
 const std::vector<uint8_t> old[]={{0x8b,0x87,0xa4,0,0,0},{0x8b,0x87,0xb0,0,0,0},
  {0x8b,0x47,0x0c},{0x8b,0x87,0xb4,0,0,0}};
 const std::vector<uint8_t> recent[]={{0x8b,0x87,0x80,0,0,0},{0x8b,0x87,0x98,0,0,0},
  {0x8b,0x47,0x10},{0x48,0x8b,0x87,0x78,1,0,0,0x0f,0xb6,0x40,0x6a}};
 for(unsigned i=0;i<4;++i){
  std::vector<uint8_t> bytes(padding,0x90);if(framed)bytes.insert(bytes.end(),{0x55,0x48,0x89,0xe5});
  const auto &load=original?old[i]:recent[i];bytes.insert(bytes.end(),load.begin(),load.end());
  if(framed)bytes.push_back(0x5d);bytes.push_back(0xc3);
  // Wrong ABI field, independently for the common and consumer-only getters.
  if((bad==1 && i==0)||(bad==2 && i==1)||(bad==3 && i==2))bytes[padding+(framed?4:0)+2]^=1;
  memcpy(image.data()+128*i,bytes.data(),bytes.size());
  symbols[names[i]]=reinterpret_cast<uintptr_t>(image.data()+128*i);
 }
 XeHost host;host.kernelProcessed=true;host.active=true;host.processKext(patcher,6,reinterpret_cast<uintptr_t>(image.data()),image.size());
 const auto fb=(bad==1 || bad==3)?A::Unknown:original?A::Original:A::RelocatedFields;
 const auto accel=(bad==1 || bad==2)?A::Unknown:original?A::Original:A::Attachment6A;
 assert(tglHostSurfaceABI[0]==fb && tglHostSurfaceABI[1]==accel);
}
symbols.clear();
// A donor imports loaded hosts, but a KC host can have no load notification.
// All native dependencies exist before each donor callback. Notification order
// must not defer entry guards past that callback or require an unrelated provider.
kmod_info_t modules[]={
 {surface.id,0x1000,0x1000,modules+1},
 {kextAppleIntelTGLCompat.id,0x2000,0x1000,modules+2},
 {graphics.id,0x3000,0x1000,modules+3},
 {accelFamily.id,0x4000,0x1000,modules+4},
 {backlight.id,0x5000,0x1000,modules+5},
 {XeDataAccess::dependency().id,0x6000,0x1000,nullptr}};
loadedHead=modules;
// Dependencies notified before Graphics activation must be recovered, including
// the Data-access sandbox and Display12 console/backlight hooks. A late duplicate
// notification must not install a second trampoline.
for(unsigned first:{1U,2U,3U}) {
 XeHost host;host.xe.defer=host.xe.tglRequested=true;host.resetFramebuffer=XeHost::FB_COPY;
 events.clear();published=false;XeDataAccess::installs=0;
 DeviceInfo::value.videoBuiltin=&gpu;publish=true;identity=0x9a498086;
 for(unsigned id:{4U,5U,6U,7U,8U})host.processKext(patcher,id,0,0);
 assert(events.empty() && !XeDataAccess::installs);
 host.processKext(patcher,first,0,0);
 host.processKext(patcher,1,0,0);
 assert(host.dataAccessSeen && host.backlightSeen && host.graphicsSeen && host.surfaceSeen);
 assert(XeDataAccess::installs==1);
 for(unsigned id:{4U,5U,8U})host.processKext(patcher,id,0,0);
 assert(XeDataAccess::installs==1 && std::count(events.begin(),events.end(),8)==1);
}
unsigned order[]={1,2,3,4,6,7};do {for(bool display13:{false,true})for(bool validProvider:{false,true}) {
 XeHost host;host.kernelProcessed=true;host.active=host.xe.defer=true;events.clear();
 host.systemWakeRecovery=display13;
 host.xe.tglRequested=!display13;host.xe.display13FramebufferRequested=display13;
 host.xe.providerOK=validProvider;host.xe.requireProvider=true;
 std::vector<int> expected;
 for(auto id:order){
  if(id==1 || id==2)expected.push_back(100+id);
  host.processKext(patcher,id,0,0);
  assert(events==expected);
  if(id==1 || id==2){assert(host.surfaceSeen);if(display13)assert(host.graphicsSeen && host.accelSeen);}
 }
 assert(events==expected && events.size()==2);
 assert(host.xe.rejectedDonors==(validProvider?0U:1U));
 assert(host.providerSeen && host.xe.providerReady==validProvider);
 }
}while(std::next_permutation(order,order+6));
// No notifications for any dependency: both lifecycle installers still run
// within the donor callbacks, after resolving loaded hosts through Lilu.
for(bool display13:{false,true}){
 XeHost host;host.kernelProcessed=true;host.active=host.xe.defer=true;host.xe.tglRequested=!display13;host.xe.display13FramebufferRequested=display13;
 host.systemWakeRecovery=display13;host.xe.requireProvider=true;events.clear();
 host.processKext(patcher,2,0,0);assert((events==std::vector<int>{102}));
 assert(host.surfaceSeen && !host.providerSeen);
 host.processKext(patcher,1,0,0);assert((events==std::vector<int>{102,101}));
 assert(host.providerSeen && !host.xe.rejectedDonors);
}
// Missing/invalid metadata must not be used to fabricate an observed ABI.
// The installer is still dispatched, so its existing fail-closed guard is
// installed instead of leaving the native unpatched lifecycle reachable.
for(unsigned failure=0;failure<4;++failure){
 XeHost host;host.kernelProcessed=true;host.active=host.xe.defer=true;host.xe.tglRequested=true;host.xe.requireProvider=true;events.clear();
 loadedHead=failure==0?nullptr:modules;validImage=failure!=1;
 auto savedSize=modules[0].size;auto savedIndex=surface.loadIndex;
 if(failure==2)modules[0].size=0;if(failure==3)surface.loadIndex=0;
 host.processKext(patcher,2,0,0);assert((events==std::vector<int>{102}) && !host.surfaceSeen);
 modules[0].size=savedSize;surface.loadIndex=savedIndex;
}
validImage=true;loadedHead=nullptr;
for(bool loadOnly:{false,true})for(bool requested:{false,true}) {
 XeHost host;host.xe.adlpLoadOnly=loadOnly;host.xe.display13FramebufferRequested=requested;
 assert(host.xe.requiresIntelProvider()==(requested && !loadOnly));
 assert(host.xe.isIntelProvider(3)==(requested && !loadOnly));
 assert(!host.xe.isIntelProvider(99));
}

// The native loader may probe/start a donor as soon as this callback returns.
// No later host notification is available to repair a missed installation.
// Dependencies near the end of a valid kmod list are still loaded dependencies.
for(unsigned prefix:{0U,510U,511U,512U,600U}){
 std::vector<kmod_info_t> peers(prefix);std::vector<std::vector<char>> ids(prefix,std::vector<char>(64));
 for(unsigned i=0;i<prefix;++i){snprintf(ids[i].data(),64,"org.example.dependency.%u",i);peers[i]={ids[i].data(),uintptr_t(0x10000+i*0x1000),0x1000,i+1<prefix?&peers[i+1]:modules};}
 loadedHead=prefix?peers.data():modules;
 XeHost host;host.kernelProcessed=true;host.active=host.xe.defer=host.xe.tglRequested=true;host.xe.requireProvider=true;events.clear();
 host.processKext(patcher,1,0,0);
 assert(host.surfaceSeen && host.providerSeen && host.xe.providerReady);
 // This is the boundary oracle, not a copy of dependenciesSeen or queue state.
 assert((events==std::vector<int>{101}));
}
loadedHead=nullptr;
const char *names[]={nullptr,"IMEI","HECI","MEI"};
for(unsigned firmware=0;firmware<2;++firmware)for(unsigned me=0;me<5;++me)for(unsigned test=0;test<6;++test){
 mei.name=names[me%4];mei.renames=0;gpu.name="GFX0";
 DeviceInfo::value.managementEngine=me==4?nullptr:&mei;
 DeviceInfo::value.firmwareVendor=firmware?DeviceInfo::FirmwareVendor::Apple:DeviceInfo::FirmwareVendor::Other;
 events.clear();published=false;publish=test!=1;identity=(test==2 || test>=4)?0x12348086:0x9a498086;deviceProperty=test>=4?&fake:nullptr;fake.value=test==4?0x46D2:0xFFFF;graphics.off=false;
 hasModel=test==3;modelWrites=0;
 DeviceInfo::value.videoBuiltin=test==0?nullptr:&gpu;XeHost host;host.processKernel(patcher);
 assert(host.backlightEnabled==((test==3 || test==4) && !firmware));
 const bool normalize=(test==3 || test==4) && me!=4;
 assert(mei.renames==(normalize && me!=1?1U:0U));
 if(normalize)assert(mei.name && !strcmp(mei.name,"IMEI"));
 else assert(mei.name==names[me%4]);
 assert(events.back()==9);assert(modelWrites==(test==4?1U:0U));
 if(test<2){assert(!host.active&&graphics.off);assert(events.size()==(test==0?1:2));}
 else if(test==2 || test==5){assert(!host.active&&graphics.off);assert(events.size()==4);}
 else {assert(host.active&&!graphics.off);assert(events.front()==1);assert(events[events.size()-2]==8);}
}
for(unsigned rev:{0U,12U,255U})for(bool apple:{false,true}) {
 DeviceInfo::value.videoBuiltin=&gpu;DeviceInfo::value.managementEngine=nullptr;
 DeviceInfo::value.firmwareVendor=apple?DeviceInfo::FirmwareVendor::Apple:DeviceInfo::FirmwareVendor::Other;
 publish=true;published=false;deviceProperty=nullptr;identity=0x46908086;revision=rev;events.clear();
 XeHost host;host.processKernel(patcher);assert(host.active);
 assert(host.backlightEnabled==!apple);assert(host.backlightGPU==(!apple?&gpu:nullptr));
}
for(unsigned rev:{0U,12U,255U}) {
 DeviceInfo::value.videoBuiltin=&gpu;DeviceInfo::value.managementEngine=nullptr;
 publish=true;published=false;deviceProperty=nullptr;identity=0x4C8A8086;revision=rev;events.clear();graphics.off=false;
 XeHost host;host.processKernel(patcher);assert(!host.active && graphics.off && !host.backlightEnabled);
 for(auto e:events)assert(e!=8); // Unsupported family never dispatches the GPU.
}
for(unsigned rev:{0U,12U,255U})for(bool override:{false,true}) {
 DeviceInfo::value.videoBuiltin=&gpu;DeviceInfo::value.managementEngine=nullptr;
 publish=true;published=false;deviceProperty=override?&fake:nullptr;fake.value=0x9A49;identity=0x46A68086;revision=rev;events.clear();graphics.off=false;
 XeHost host;host.processKernel(patcher);
 assert(host.active && !graphics.off && host.backlightEnabled==(DeviceInfo::value.firmwareVendor!=DeviceInfo::FirmwareVendor::Apple));
 assert(events[events.size()-2]==8);
 const auto profile=XELP::identify(identity,revision,0x9A498086);
 assert(profile.displayAdmitted() && profile.admitted());
 // Execute the real outer dispatcher; neither FB hooks nor Accelerator
 // dispatch must remain connected to the selected mobile Display13 backend.
 host.processKext(patcher,1,0,0);assert(events.back()==101);
 host.processKext(patcher,2,0,0);assert(events.back()==102);
}
puts("PASS actual host admission: absent/unpublished/unsupported GPUs never enter Xe; published PCI reads precede selected-driver dispatch");}
'''
with tempfile.TemporaryDirectory(prefix='wxe-host-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
