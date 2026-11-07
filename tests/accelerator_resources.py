#!/usr/bin/env python3
"""Replay physical selection, fuse qualification, reporting and native probe.

Resource expectations are explicit Gen12 capacity examples, including a retained
physical 80-EU/six-bank readout. EU and cache populations are independent. A
failed ownership/readout prerequisite must still prevent native entry.
"""
import re, subprocess, tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=(root/'WhateverXe/kern_xe.cpp').read_text()
selector=re.search(r'void XE::selectProfile\(.*?\n\}',source,re.S)[0]
qualification=source.split('            const bool readoutReady=')[1].split('            if(tglGT2Detected)')[0]
qualification='const bool readoutReady='+qualification
probe=re.search(r'IOService \*XE::wrapTGLAcceleratorProbe\(.*?\n\}',source,re.S)[0]
start=re.search(r'bool XE::wrapTGLAcceleratorStart\(.*?\n\}',source,re.S)[0]
start_guard=start.split('{',1)[1].split(' if (callbackXE->adlnDetected')[0]
aggregate=re.search(r'tglAcceleratorReady = ([^;]+);',source)[0]
resources=re.search(r'const bool resourcesReady=.*?;',source,re.S)
# Explicit fixture prerequisites: a new production requirement must not become
# true just because its name appeared in the aggregate. These flags model the
# other installers; their producers are outside this resource/probe test.
prerequisites = """adlnObserved auxAlignOK blitInitOK ccsPSReady ccsReady
completionCacheOK contextCacheOK copyOK cpsOK dmaWriteOK ffModeOK flushOK
ggttOK gucOK hangOK hizReady hostOK iosCCSOK observed ok recoveryOK scratchOK
semaphoreTLBOK skuReady surfaceABIReady tglMMIOUCReady tlbPreparserOK urbReady""".split()
code=r'''
#include "kern_xelp_gt_resources.hpp"
#include "kern_alderlake_n.hpp"
#include "kern_xelp_platform.hpp"
#include "kern_xelp_os.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using SInt32=int;using mach_vm_address_t=uintptr_t;
struct IOService {virtual ~IOService()=default;};
struct IOPCIDevice:IOService {uint32_t configRead32(unsigned){return 0x9a498086;}};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 template<class...T>void setProperty(T...){};};
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
#define FunctionCast(fn,p) reinterpret_cast<decltype(&fn)>(p)
bool selected=true;
bool xelpSelectedDevice(IOPCIDevice *p){return p && selected;}
bool xelpDisplay13RenderDevice(IOPCIDevice *p){return p && selected;}
bool checkKernelArgument(const char*){return false;}
template<class...T>void tglObserveAccelerator(T...){}
const char *version="Darwin Kernel Version 25.6.73: arbitrary-metadata/RELEASE_X86_64";
int kextIntelTGL,kextIntelTGLFb;
bool adlnNativeRCSProfileRequested=false,display13TraceEnabled=false,display13Offline=false;
unsigned nativeCalls=0;
IOService *nativeProbe(IOService *that,IOService*,SInt32*){++nativeCalls;return that;}
struct XE {
 static XE *callbackXE;
 bool tglGT2Detected=false,tglGT2Ready=false,display12SmallGTDetected=false,display12SmallGTReady=false;
 bool adlsDisplaySelected=false,adlsDisplayHooksReady=true,adlsBootConsumersFailed=false;
 bool adlnDetected=false,tglAcceleratorRequested=true,tglAcceleratorReady=false;
 XELP::GTResources tglGT2Resources,display12SmallGT;
 XELP::Profile xeProfile;
 bool tglRequested=false,tglInactivePorts=false,tglPG1Poll=false,tglClockOrderRequested=false;
 bool tglTrainingLeaseRequested=false,tglProgramEnabled=false,adlnRequested=false,adlnKernelReady=false;
 bool adlnNativeRequested=false,adlpDisplaySelected=false,adlpRenderRequested=false,display13FramebufferRequested=false;
 int *currentGraphics=nullptr,*currentFramebuffer=nullptr;
 struct {bool available=false;}modDVMTCalcFix;
 void selectProfile(const XELP::Profile&);
 OPERANDS
 uintptr_t orgTGLAcceleratorProbe=reinterpret_cast<uintptr_t>(&nativeProbe);
 void qualify(AlderLakeN::GTReadout gt,uint32_t edram){QUALIFICATION}
 void report(){RESOURCES AGGREGATE}
 static IOService *wrapTGLAcceleratorProbe(IOService*,IOService*,SInt32*);
 static bool startResourceGate(){START_GUARD return true;}
};
XE *XE::callbackXE;
SELECTOR
PROBE
struct Capacity {bool small;uint32_t fuses[5];unsigned dss,eus,banks;};
// The cache-disable mask is not derived from the EU count. Keep both TGL
// 80-EU layouts, and independent donor-supported capacities as separate input.
const Capacity inputs[]={
 {false,{1,0x1f,0,0x88,0xe00fa},5,80,6},
 {false,{1,0x1f,0,0,0xe00fa},5,80,8},
 {false,{1,0x07,0,0xc0,0xe00fe},3,48,6},
 {false,{1,0x3f,0,0,0xe00fa},6,96,8},
 {false,{1,0x3f,0xc0,0xf0,0xe00fa},6,72,4},
 {true,{1,0x03,0,0xf0,0xe00fa},2,32,4},
 {true,{1,0x03,0xc0,0xf0,0xe00fe},2,24,4},
 {true,{1,0x01,0,0xf0,0xe00fa},1,16,4}
};
int main(){IOPCIDevice pci;IOService accelerator;SInt32 score=0;
 for(const auto &input:inputs)for(unsigned failure=0;failure<8;++failure){
  XE xe;XE::callbackXE=&xe;
  xe.selectProfile(XELP::identify(((input.small?0x9a60U:0x9a49U)<<16)|0x8086,1));
  assert(xe.tglGT2Detected==!input.small && xe.display12SmallGTDetected==input.small);
  AlderLakeN::GTReadout gt;gt.status=AlderLakeN::GTStatus::ProfileMismatch;
  gt.requestedMask=gt.releasedMask=3;memcpy(gt.fuses,input.fuses,sizeof(gt.fuses));
  if(failure==1)gt.status=AlderLakeN::GTStatus::AckTimeout;
  if(failure==2)gt.releasedMask=1;
  if(failure==3)gt.releaseFailedMask=2;
  if(failure==4)gt.fuses[0]=UINT32_MAX;
  xe.qualify(gt,failure==5?1U:0U);
  const bool capacityReady=failure==0 || failure>=6;
  const auto &capacity=input.small?xe.display12SmallGT:xe.tglGT2Resources;
  assert((input.small?xe.display12SmallGTReady:xe.tglGT2Ready)==capacityReady);
  if(capacityReady)assert(capacity.dss==input.dss && capacity.eus==input.eus &&
     capacity.nativeSubslices==2*input.dss && capacity.l3Banks==input.banks && capacity.urbKiB==64*input.banks);
  if(failure==6)xe.surfaceABIReady=false;
  selected=failure!=7;nativeCalls=0;xe.report();
  // Admission reporting must include pre-probe resource rejection. This
  // caught the misleading Ready=true report independently of decoder tests.
  assert(xe.tglAcceleratorReady==(capacityReady && failure!=6));
  const auto result=XE::wrapTGLAcceleratorProbe(&accelerator,&pci,&score);
  assert((result==&accelerator)==(failure==0));
  assert(nativeCalls==(failure==0?1U:0U));
  assert(XE::startResourceGate()==capacityReady);
 }
 puts("PASS independent capacities reach native probe; failed ownership/readout is rejected and reported");
}
'''
for key,value in {'SELECTOR':selector,'OPERANDS':'\n'.join('bool '+x+'=true;' for x in prerequisites),
                  'QUALIFICATION':qualification,'RESOURCES':resources[0] if resources else '',
                  'AGGREGATE':aggregate,'START_GUARD':start_guard,'PROBE':probe}.items():
    code=code.replace(key,value)
with tempfile.TemporaryDirectory(prefix='wex-accelerator-resources-') as t:
    p=Path(t);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
