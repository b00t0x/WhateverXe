#!/usr/bin/env python3
"""Exercise production ADL-P load-only selection and both driver boundaries."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('// ADLP_LOAD_ONLY_HOOKS:');b=s.index('// ADLP_LOAD_ONLY_HOOKS_END',a);body=s[a:b]
a=s.index('    // ADLP_LOAD_ONLY_KERNEL:');b=s.index('    // ADLP_LOAD_ONLY_KERNEL_END',a);selection=s[a:b]
a=s.index('    if(xeProfile.mobileDisplay13() && physicalGPU && checkKernelArgument("-wex-adlp-readout")');b=s.index('    if(((display12SmallGTDetected',a);diagnostic=s[a:b]
dispatch=s[s.index('bool XE::processKext('):s.index('    if((tglRequested',s.index('bool XE::processKext('))]
assert dispatch.index('if(adlpLoadOnly)')<dispatch.index('processADLPLoadOnly')
code=r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "kern_xelp_platform.hpp"
#include "kern_xelp_os.hpp"
#include "kern_alderlake_n.hpp"
#include "kern_xelp_gt_resources.hpp"
using mach_vm_address_t=uintptr_t;
#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
#define SYSLOG(...) do{}while(0)
struct IOService{};using SInt32=int32_t;
struct IORegistryEntry {
 static std::map<std::string,bool> props;
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *k,bool v){props[k]=v;}
};std::map<std::string,bool> IORegistryEntry::props;
struct Kext {size_t loadIndex;};Kext kextIntelTGLFb{1},kextIntelTGL{2};
bool arg=true,readout=false;bool checkKernelArgument(const char *s){if(!strcmp(s,"-wex-adlp-readout"))return readout;assert(!strcmp(s,"-wex-adlp-load-only"));return arg;}
const char *version="Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64";
struct KernelPatcher {
 struct RouteRequest {const char *symbol;uintptr_t replacement;
  template<class T>RouteRequest(const char *s,T t):symbol(s),replacement(reinterpret_cast<uintptr_t>(t)){}
 };
 bool wrongSymbol=false;unsigned routed=0;
 template<class T>T solveSymbol(size_t index,const char *s,uintptr_t base,size_t) {
  bool start=strstr(s,"5start")!=nullptr;return base+(index==1?(start?0x3f1fc:0x446bc):(start?0x23ecc:0x238e2))+(wrongSymbol?1:0);
 }
 void clearError(){}
 bool routeMultipleShort(size_t,RouteRequest *routes,size_t count,uintptr_t,size_t) {
  assert(count==2 && strstr(routes[0].symbol,"5start") && strstr(routes[1].symbol,"5probe"));
  // Calling the actual replacements must reject independently of object data.
  assert(!reinterpret_cast<bool(*)(IOService*,IOService*)>(routes[0].replacement)(nullptr,nullptr));
  assert(!reinterpret_cast<IOService*(*)(IOService*,IOService*,SInt32*)>(routes[1].replacement)(nullptr,nullptr,nullptr));
  routed+=2;return true;
 }
};
constexpr unsigned kIOMapInhibitCache=0;
void IODelay(unsigned){assert(false);}
bool allocationFailure=false;unsigned allocations=0,frees=0;
void *IOMalloc(size_t bytes){++allocations;return allocationFailure?nullptr:malloc(bytes);}
void IOFree(void *p,size_t){assert(p);++frees;free(p);}
struct DiagnosticGPU {
 unsigned command=2,maps=0,snapshots=0;bool allocationFailed=false;
 unsigned configRead16(unsigned r){assert(r==4);return command;}
 struct Map {
  std::vector<uint32_t> bar;unsigned releases=0;
  uintptr_t getVirtualAddress(){return bar.empty()?0:reinterpret_cast<uintptr_t>(bar.data());}
  unsigned getLength(){return bar.size()*4;}void release(){++releases;}
 }map;
 Map *mapDeviceMemoryWithRegister(unsigned r,unsigned){assert(r==0x10);++maps;return &map;}
 void setProperty(const char *key,const void*,unsigned bytes){
  if(!strcmp(key,"ADLPScanoutBoot")){assert(bytes==sizeof(AlderLakeN::DisplayReadout));++snapshots;}
 }
 void setProperty(const char*,uint32_t,unsigned){}
 void setProperty(const char *key,bool value){assert(!strcmp(key,"ADLPReadoutAllocationFailed"));allocationFailed=value;}
};
struct XE {
 bool adlpLoadOnly=false,adlpDisplaySelected=false;XELP::Profile xeProfile;unsigned initialized=0;
 DiagnosticGPU *physicalGPU=nullptr;
 void select(){SELECTION ++initialized;}
 void diagnose(){DIAGNOSTIC ++initialized;}
 bool processADLPLoadOnly(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static IOService *rejectADLPFramebufferProbe(IOService *,IOService *,SInt32 *);
 static IOService *rejectADLPAcceleratorProbe(IOService *,IOService *,SInt32 *);
 static bool rejectADLPStart(IOService *,IOService *);
};
BODY
int main(){
 unsigned cases=0;
 for(unsigned physical:{0x46a6U,0x46a3U,0xa7a8U,0x9a49U,0x4680U})for(bool flag:{false,true})for(bool override:{false,true}){
  XE xe;arg=flag;xe.xeProfile=XELP::identify((physical<<16)|0x8086,12,((override?0x9a49:physical)<<16)|0x8086);xe.select();
  bool expected=(physical==0x46a6 || physical==0x46a3 || physical==0xa7a8)&&flag;
  assert(xe.adlpLoadOnly==expected && xe.initialized==!expected);++cases;
 }
 // Failed/unavailable diagnostic mappings must still stop both native drivers.
 for(unsigned physical:{0x46a6U,0xa7a8U,0x4c8aU})for(bool enabled:{false,true})for(bool powered:{false,true}) {
  XE xe;DiagnosticGPU gpu;gpu.command=powered?2:0;xe.physicalGPU=&gpu;readout=enabled;
  xe.xeProfile=XELP::identify((physical<<16)|0x8086,12);xe.diagnose();
  const bool blocked=enabled && physical!=0x4c8a;
  assert(xe.adlpLoadOnly==blocked && xe.initialized==!blocked && gpu.maps==unsigned(blocked&&powered));++cases;
 }
 // A readable diagnostic BAR exercises snapshot allocation success/failure.
 // Either outcome remains load-only, and neither leaks its map or buffer.
 for(bool fail:{false,true}) {
  XE xe;DiagnosticGPU gpu;gpu.map.bar.resize(0x1000000/4);gpu.map.bar[0xA188/4]=1;
  xe.physicalGPU=&gpu;readout=true;allocationFailure=fail;allocations=frees=0;
  xe.xeProfile=XELP::identify(0x46a68086,12);xe.diagnose();
  assert(xe.adlpLoadOnly && !xe.initialized && gpu.map.releases==1);
  assert(allocations==1 && frees==unsigned(!fail) && gpu.snapshots==unsigned(!fail));
  assert(gpu.allocationFailed==fail);++cases;
 }
 readout=false;allocationFailure=false;
 for(unsigned index:{1U,2U})for(unsigned bad=0;bad<4;++bad){
  std::vector<uint8_t> image(0x50000);auto base=reinterpret_cast<uintptr_t>(image.data());
  uint8_t fb[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x10,0x48,0x89,0xf3,0x49,0x89,0xfe};
  uint8_t gt[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28};
  unsigned p=index==1?0x446bc:0x238e2,q=index==1?0x3f1fc:0x23ecc;
  memcpy(image.data()+p,index==1?fb:gt,17);memcpy(image.data()+q,gt,4);
  XE xe;xe.adlpLoadOnly=bad!=1;KernelPatcher patcher;patcher.wrongSymbol=bad==2;
  if(bad==3)image[p+8]^=1;
  IORegistryEntry::props.clear();
  assert(xe.processADLPLoadOnly(patcher,index,base,image.size())==(bad==0));
  assert(patcher.routed==(bad==0?2U:0U));
  if(!bad){assert(IORegistryEntry::props.at(index==1?"ADLPFramebufferProbeRejected":"ADLPAcceleratorProbeRejected"));assert(IORegistryEntry::props.at("ADLPLoadOnlyUnexpectedStart"));}
  ++cases;
 }
 for(unsigned module:{1U,2U}){
  std::vector<uint8_t> image(0x50000);auto base=reinterpret_cast<uintptr_t>(image.data());
  const uint8_t gt[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28};
  memcpy(image.data()+0x238e2,gt,17);memcpy(image.data()+0x23ecc,gt,4);
  XE xe;xe.adlpDisplaySelected=true;KernelPatcher p;
  assert(xe.processADLPLoadOnly(p,module,base,image.size())==(module==2));assert(p.routed==(module==2?2U:0U));
  if(module==2)assert(IORegistryEntry::props.at("ADLPFBOnlyAcceleratorBlocked"));++cases;
 }
 printf("PASS %u production ADLP load-only selection/probe/start checks\n",cases);
}
'''.replace('SELECTION',selection).replace('BODY',body).replace('DIAGNOSTIC',diagnostic)
with tempfile.TemporaryDirectory(prefix='adlp-load-only-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
