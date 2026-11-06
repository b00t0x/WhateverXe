#!/usr/bin/env python3
"""Replay actual host admission: no PCI reads before successful publication."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
adapter=(r/'WhateverXe/kern_xelp_device.hpp').read_text();adapter=adapter[adapter.index('namespace XELP {'):adapter.rindex('#endif')]
s=(r/'WhateverXe/kern_host.cpp').read_text();body=s[s.index('void XeHost::processKernel'):s.index('bool XeHost::wrapPanelSetDisplay')]
code=r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
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
struct KernelPatcher {
 enum class Error {NoError}; Error getError(){return Error::NoError;}
 static constexpr int KernelID=0;
 uintptr_t solveSymbol(int,const char *){return 0;}
 template<class T>T solveSymbol(size_t,const char*,mach_vm_address_t,size_t){return T{};}
 struct RouteRequest {template<class T>RouteRequest(const char*,T,uintptr_t&) {}};
 bool routeMultiple(size_t,RouteRequest*,unsigned,mach_vm_address_t,size_t){return true;}
 void routeBlock(uintptr_t,const uint8_t*,size_t){}
 void clearError(){}
};
namespace XeDataAccess {bool processKext(KernelPatcher&,size_t,mach_vm_address_t,size_t){return false;}void processKernel(KernelPatcher &){assert(published);events.push_back(7);}}
struct Graphics {size_t loadIndex=4;bool off=false;void switchOff(){off=true;}} graphics;
struct {size_t loadIndex=6;}surface;struct {size_t loadIndex=5;}backlight;struct {size_t loadIndex=7;}accelFamily;
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
 static XeHost *callbackHost;bool systemWakeRecovery=false;
 bool surfaceSeen=false,graphicsSeen=false,accelSeen=false;
 struct PendingDonor {size_t index;uintptr_t address;size_t size;};PendingDonor pendingDonors[2]{};unsigned pendingDonorCount=0;
 bool dependenciesSeen() const {return surfaceSeen && (!systemWakeRecovery || (graphicsSeen && accelSeen));}
 void flushDonors(KernelPatcher&);
 uintptr_t sleepFBCount=0,sleepFBAt=0;uint32_t repeatedSleep=0,sleepNotifications=0,wakeNotifications=0;
 IOSimpleLock *sleepScopeLock=nullptr;
 struct SleepEntry {IOService *fb=nullptr;bool pending=false;};SleepEntry sleepEntries[3]{};
 struct SleepScope {SleepScope *next=nullptr;uintptr_t thread=0;IOService *fb=nullptr;bool reached=false,skipped=false;};SleepScope *sleepScopes=nullptr;
 uintptr_t orgDisplayChange=0;
 static IOReturn wrapDisplayChange(void*,void*,IOFramebuffer*,int,void*);
 static uint32_t pendingSleepDisplays(void*);
 uintptr_t orgWSAAAttribute=0;
 static IOReturn wrapWSAAAttribute(IOFramebuffer*,uint32_t,uint32_t);
 bool active=false,backlightEnabled=false;enum {FB_DETECT,FB_RESET,FB_COPY,FB_ZEROFILL,FB_NONE,FB_TOTAL};uint32_t resetFramebuffer=FB_NONE;
 IOPCIDevice *backlightGPU=nullptr;
 struct vc_info{};vc_info consoleVinfo;bool gotConsoleVinfo=false;
 struct XE {
 bool defer=false;bool isIntelDonor(size_t i)const{return defer && (i==1 || i==2);}
 void setHostSurfaceABI(unsigned,unsigned){}
 void setHostFramebufferEvent(uintptr_t){}
 void setHostSleepReady(bool){}
 void setHostWakeRecoveryReady(bool){}
 std::vector<IOService*> owned;bool ownedDisplay13Framebuffer(IOService *f)const{return std::find(owned.begin(),owned.end(),f)!=owned.end();}
 IOFramebuffer *degraded=nullptr; bool failedDSIWakeFor(IOFramebuffer *f) const {return f && f==degraded;}
 void processKernel(KernelPatcher &,DeviceInfo *){events.push_back(8);}
 void processKext(KernelPatcher&,size_t index,mach_vm_address_t,size_t){events.push_back(100+index);}
 } xe;
 void processKernel(KernelPatcher &);
 void processKext(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static void wrapPanelSetDisplay(){}static void wrapFramebufferInit(){}
 uintptr_t orgPanelSetDisplay=0,orgFramebufferInit=0;uint8_t *gIOFBVerboseBootPtr=nullptr;
};
'''.replace('// DEVICE_ADAPTER',adapter)+body+r'''
XeHost *XeHost::callbackHost;
static IOReturn vendor(IOFramebuffer*,uint32_t,uint32_t){return 42;}
int main(){KernelPatcher patcher;IOPCIDevice gpu,mei;
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
// Already-loaded notifications may arrive in any host dependency order.
// Neither missing contracts nor a late callback may silently skip donor setup.
unsigned order[]={1,2,4,6,7};do {
 XeHost host;host.active=host.systemWakeRecovery=host.xe.defer=true;events.clear();
 std::vector<int> received;
 for(auto id:order){
  if(id==1 || id==2)received.push_back(100+id);
  host.processKext(patcher,id,0,0);
  if(!host.dependenciesSeen())assert(events.empty());
  else assert(events==received);
 }
 assert(events==received && events.size()==2 && host.pendingDonorCount==0);
}while(std::next_permutation(order,order+5));
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
