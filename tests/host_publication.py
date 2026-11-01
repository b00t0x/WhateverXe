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
#include "kern_xelp_platform.hpp"
using mach_vm_address_t=uintptr_t;
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}void setProperty(const char*,bool){}};
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
 static constexpr int KernelID=0;
 uintptr_t solveSymbol(int,const char *){return 0;}
 template<class T>T solveSymbol(size_t,const char*,mach_vm_address_t,size_t){return nullptr;}
 struct RouteRequest {template<class T>RouteRequest(const char*,T,uintptr_t&) {}};
 bool routeMultiple(size_t,RouteRequest*,unsigned,mach_vm_address_t,size_t){return true;}
 void clearError(){}
};
// The optional observer is inactive in this host-publication fixture.
namespace XeDataAccess {bool processKext(KernelPatcher&,size_t,mach_vm_address_t,size_t){return false;}void processKernel(KernelPatcher &){assert(published);events.push_back(7);}}
struct Graphics {size_t loadIndex=4;bool off=false;void switchOff(){off=true;}} graphics;
struct {size_t loadIndex=5;}backlight;
struct XeHost {
 bool active=false,backlightEnabled=false;enum {FB_DETECT,FB_RESET,FB_COPY,FB_ZEROFILL,FB_NONE,FB_TOTAL};uint32_t resetFramebuffer=FB_NONE;
 IOPCIDevice *backlightGPU=nullptr;
 struct vc_info{};vc_info consoleVinfo;bool gotConsoleVinfo=false;
 struct XE {
 void processKernel(KernelPatcher &,DeviceInfo *){events.push_back(8);}
 void processKext(KernelPatcher&,size_t index,mach_vm_address_t,size_t){events.push_back(100+index);}
 } xe;
 void processKernel(KernelPatcher &);
 void processKext(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static void wrapPanelSetDisplay(){}static void wrapFramebufferInit(){}
 uintptr_t orgPanelSetDisplay=0,orgFramebufferInit=0;uint8_t *gIOFBVerboseBootPtr=nullptr;
};
'''.replace('// DEVICE_ADAPTER',adapter)+body+r'''
int main(){KernelPatcher patcher;IOPCIDevice gpu,mei;
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
