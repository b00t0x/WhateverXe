#!/usr/bin/env python3
"""Exercise the production surface/retirement adapters on the stop fixture's BAR."""
from pathlib import Path
from production_source import read_source
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
functions='\n'.join(re.search(r'bool XE::'+name+r'\(.*?\n\}',s,re.S)[0] for name in
 ('switchDSINativeSurface','dsiArmedBackingOwned','retireDSINativeBacking','publishDSINativeBacking','publishDSINativeMode'))
state=re.search(r'struct DSIPendingBackingOwner \{.*?\n\};',s,re.S)[0]
published=re.search(r'static bool dsiPublishedTarget\(.*?\n\}',s,re.S)[0]
# Share the established real power/IRQ/clock fixture; no implementation bodies
# are rewritten. Allocation/PTE proof and CPU mapping have their own adapter test.
q=(root/'tests/dsi_quiesce_adapter.py').read_text()
q=q.split("code=r'''",1)[1].split("'''.replace",1)[0]
prelude='#define DSI_SURFACE_REPLAY\n'+q[:q.index('struct XE {')]
prelude=prelude.replace('return Display13Clock::Variant::RPLU;', 'return Display13Clock::Variant::ADLP;')
prelude=prelude.replace('#include "kern_dsi_boot.hpp"','#include "kern_dsi_boot.hpp"\n#include "kern_dsi_surface.hpp"\n#include "kern_dsi_native_backing.hpp"')
b=(root/'tests/dsi_boot.cpp').read_text();prelude=prelude.replace('FIXTURE',b[b.index('struct IO {'):b.index('Boot capture(')])
prelude=prelude.replace('bool armedOwner=false,failNativeRetire=false;unsigned nativeRetires=0;IOService *pendingFB=nullptr;bool dsiPendingTarget(IOService *fb){return fb && pendingFB==fb;}',state+'\nDSIPendingBackingOwner dsiPendingBacking;\nbool dsiPendingTarget(IOService *fb){return fb && dsiPendingBacking.framebuffer==fb;}')
prelude=prelude.replace('#include "kern_dsi_native_backing.hpp"','#include "kern_dsi_native_backing.hpp"\n#include "kern_dsi_native_mode.hpp"')
prelude=prelude.replace('bool clockFaulted=false,memoryFaulted=false;', 'bool clockFaulted=false,memoryFaulted=false,watermarkFaulted=false;')
prelude=prelude.replace('bool destroyingPath=false,wmProgramming=false,wmCommitting=false;', 'bool destroyingPath=false,wmProgramming=false,wmCommitting=false;unsigned wmPending=0;uint64_t watermarkClockHz=0;Display13WM::Latch wmLatch;')
prelude=prelude.replace('struct {uintptr_t record=0;}scanout;','Display13FBMemory::SurfaceReadout scanout;')
prelude=prelude.replace('struct {bool ready=true;}dsiFixedMode;','struct {bool ready=true;AppleAdapter::Timing timing;}dsiFixedMode;')
prelude=prelude.replace(' void retain(){',' uint32_t callPlatformFunction(const char*,bool,void*,void*,void*,void*);\n void retain(){')
prelude=prelude[:prelude.index('void tick(unsigned us)')]+published+r'''
uint64_t elapsed=0;unsigned gpuDepth=1;bool failDiscard=false;unsigned discards=0;
template<class T>void put(void *p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void *current_thread(){return reinterpret_cast<void*>(1);}
void clock_get_uptime(uint64_t *v){*v=elapsed*1000;}void absolutetime_to_nanoseconds(uint64_t t,uint64_t *v){*v=t;}
void tick(unsigned us){assert(locks==std::vector<unsigned>({1,3}) && gpuDepth==1);elapsed+=us;
 if(!activeMap->freeze && us>=1000)++activeMap->reg(0x70040);
 activeMap->reg(0x701AC)=activeMap->reg(0x7019C);
}
void IOSleep(unsigned ms){tick(ms*1000);}void IODelay(unsigned us){tick(us);}
struct Display13FBClockIO:Display13FBBootIO {};
bool adlnSharedBinding(void *controller,void *owner){return controller==display13FBPower.controller && owner && mappingBound;}
bool failSlotReturn=false,failCache=false,failLogical=false,failModeDBUF=false;unsigned slotReturns=0,cpuInitial=0,cpuCaps=0;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
bool dsiModeCoherent(IOService*){return dsiFixedMode.ready;}
uint32_t OSObject::callPlatformFunction(const char *name,bool wait,void *arg,void *b,void *c,void *d) {
 assert(locks==std::vector<unsigned>{1} && !gpuDepth && !strcmp(name,"EFITilingMode") && wait && !b && !c && !d);
 assert(*static_cast<uint32_t*>(arg)==2);return 0;
}
void compute(void *fb,const AppleAdapter::Timing*) {
 assert(locks==std::vector<unsigned>({1,3}) && gpuDepth==1);
 for(auto off:{0x430CU,0x4320U,0x4328U})put(fb,off,1080U);
 for(auto off:{0x4310U,0x4324U,0x432CU})put(fb,off,1920U);
 put(fb,0x4314,8355840U);put(fb,0x4334,4352U);put(fb,0x4338,1U);
 auto path=Display13FBMemory::singlePath<void*>(fb);
 put(path,0xF0,uint64_t(0));put(path,0x100,uint64_t(0));
 put(path,0xF8,1080U);put(path,0xFC,1920U);put(path,0x108,1080U);put(path,0x10C,1920U);
}
void freeSlot(IOService *fb) {
 assert(locks==std::vector<unsigned>{1} && gpuDepth==1 && !Display13FBMemory::field<void*>(fb,0x4348));
 ++slotReturns;if(failSlotReturn)return;
 auto controller=display13FBPower.controller;put(controller,0xDF0,0U);put(controller,0xDF4,uint8_t(0));put(fb,0x4288,uint8_t(0xFF));
}
uint32_t logical(IOService*){assert(!gpuDepth && locks==std::vector<unsigned>{1});return failLogical?1:0;}
void initial(IOService *fb,uint32_t arg){assert(!gpuDepth && locks==std::vector<unsigned>{1} && !arg);++cpuInitial;
 put(fb,0x4000,0U);put(fb,0x4008,UINT32_MAX);put(fb,0x400C,UINT32_MAX);put(fb,0x4200,1U);}
void caps(IOService *fb){assert(!gpuDepth && locks==std::vector<unsigned>{1});++cpuCaps;put(fb,0x402C,1U);}
uint64_t receipt[18]{};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *name,const void *p,unsigned n){if(!strcmp(name,"WEXDSISurface")){assert(n==sizeof(receipt));memcpy(receipt,p,n);}else assert(n==96);}};
struct XE {
 bool dsiNativeWriterHooksReady=true,dsiResourcesReady=true,dsiCacheHooksReady=true,dsiBackingHooksReady=true;
 bool dsiStopAttempted=false,dsiIRQParked=false,dsiDDIReleased=false;
 Profile dsiProfile;Boot dsiBoot;Quiesce::Result dsiStop;Clock::Result dsiClockStop;Session dsiSession;Session::Cleanup dsiCleanup;
 bool discardDSINativeBacking(IOService *fb){assert(locks==std::vector<unsigned>{1} && !gpuDepth && !dsiPendingBacking.armed && dsiPendingTarget(fb));
  ++discards;if(failDiscard)return false;dsiPendingBacking={};return true;}
 uintptr_t dsiComputeGeometry=reinterpret_cast<uintptr_t>(compute),orgAlderLakeNScanoutFree=reinterpret_cast<uintptr_t>(freeSlot);
 uintptr_t dsiModeCPU[3]{reinterpret_cast<uintptr_t>(logical),reinterpret_cast<uintptr_t>(initial),reinterpret_cast<uintptr_t>(caps)};
 static void wrapAlderLakeNScanoutFree(IOService*){}
 bool updateDSIBootCache(IOService*,IOService*,OSObject*){assert(locks==std::vector<unsigned>{1});return !failCache;}
 bool dsiNativeUpdateAllowed(IOService*){return dsiPendingBacking.modeReady && !display13FBPower.memoryFaulted;}
 static void adlnSetupDBUFPipe(IOService *controller,IOService *fb,unsigned pipe,bool optimized) {
  assert(!gpuDepth && locks==std::vector<unsigned>{1} && controller==display13FBPower.controller && dsiPublishedTarget(fb) && !pipe && !optimized);
  assert(dsiPendingBacking.modeReady);
  auto path=Display13FBMemory::singlePath<OSObject *>(fb);
  assert(Display13FBMemory::field<uint64_t>(path,0x118)==dsiFixedMode.timing.pixelClock && dsiFixedMode.timing.pixelClock==135255000);
  auto &c=display13FBPower;
  if(failModeDBUF){c.dbuf.faulted=true;return;}
  c.dbuf.pipe[0].primary={40,2048};c.dbuf.pipe[0].cursor={0,40};c.pipe[0].wmPending=0;
  c.pipe[0].wmLatch.status=Display13WM::Status::Complete;c.pipe[0].watermarkClockHz=358400000;
 }
 bool publishDSINativeBacking();bool publishDSINativeMode(IOService*);
 bool releaseDSILogical(IOService *fb){return !Display13FBMemory::field<uintptr_t>(fb,0x4340);}
 bool switchDSINativeSurface();bool dsiArmedBackingOwned(IOService*,OSObject*);bool retireDSINativeBacking(IOService*,OSObject*);
};
'''
prelude=prelude.replace('Display13DBUF::State dbuf;','Display13DBUF::State dbuf;Display13WM::Latency latency;')
# Reuse just initialization, leaving stop tests and stop adapter out of this TU.
case=q[q.index('struct Case {'):q.index(' bool run(){')]
case=case.replace('IOWorkLoop loop;','IOWorkLoop loop;IOService accelerator,pipe;OSObject mapping,memory;NativeBacking::Record record;')
case=case.replace('xe.adlnTopologyController=&controller;','').replace('put(&controller,0xE00,&loop);put(&controller,0x90,&irqLock);',r'''
  put(&controller,0xE00,&loop);put(&controller,0x90,&irqLock);
  put(&path,0x32B0,&fb);put(&path,0x3640,0U);put(&pipe,0x88,&accelerator);
  map.reg(0x46070)=0xC0000010;map.reg(0x46000)=612;map.reg(0x46008)=0;
  map.reg(0x701CC)=0x5000;map.reg(0x701AC)=map.reg(0x7019C)=0;
  c.latency=Display13WM::decodeLatency(0x63503300,0x00909090,30);
  c.dbuf.pipe[0].primary={0,2048};map.reg(0x7027C)=0x07FF0000;
  xe.dsiBoot.status=BootStatus::Complete;xe.dsiBoot.control=map.reg(0x70180);
  xe.dsiBoot.stride=map.reg(0x70188);xe.dsiBoot.size=map.reg(0x70190);xe.dsiBoot.surface=0;
  NativeBacking::request(xe.dsiProfile,3,record);record.refs=1;record.map=reinterpret_cast<uintptr_t>(&mapping);
  record.memory=reinterpret_cast<uintptr_t>(&memory);record.gpu=0x10000000;record.pitch=4352;
  put(&pipe,0x1298,record.memory);put(&pipe,0x12A0,record.map);
  auto &o=dsiPendingBacking;o={};o.framebuffer=&fb;o.accelerator=&accelerator;o.pipe=&pipe;
  o.pinnedMap=&mapping;o.pinnedMemory=&memory;o.record=&record;o.thread=1;o.allocating=o.allocated=true;
  auto &p=o.proof;p.status=Display13FBMemory::SurfaceStatus::Complete;p.record=reinterpret_cast<uintptr_t>(&record);
  p.refs=p.kind=1;p.map=record.map;p.memory=record.memory;p.gpu=record.gpu;p.bytes=8355840;p.pitch=4352;p.verifiedPages=2040;
  assert(AppleAdapter::makeTiming(xe.dsiProfile,dsiFixedMode.timing));
  put(&fb,0x4A00,1U);put(&fb,0x4A08,&path);put(&controller,0xDF5,uint8_t(3));put(&controller,0xDF4,uint8_t(1));put(&controller,0xDF0,1U);
  put(&controller,0x1A10,1U);put(&fb,0x4288,uint8_t(0));failSlotReturn=failCache=failLogical=failModeDBUF=false;slotReturns=cpuInitial=cpuCaps=0;
  o.initialized=true;elapsed=0;gpuDepth=1;failDiscard=false;discards=0;memset(receipt,0,sizeof(receipt));
''')
code=prelude+functions+'\n'+case+r'''
 bool run(){locks={1};bool result=xe.switchDSINativeSurface();assert(locks==std::vector<unsigned>{1});locks.clear();dsiPendingBacking.allocating=false;gpuDepth=0;return result;}
 bool retire(){locks={1};bool r=xe.retireDSINativeBacking(&fb,&path);assert(locks==std::vector<unsigned>{1});locks.clear();return r;}
 void stopped(){xe.dsiStop.stage=Quiesce::Stage::Complete;xe.dsiStop.error=Quiesce::Error::None;xe.dsiIRQParked=xe.dsiDDIReleased=true;xe.dsiClockStop.status=Clock::Status::Complete;}
 ~Case(){assert(locks.empty());dsiPendingBacking={};}
};
int main(){
 {Case c;dsiPendingBacking.initialized=false;const auto before=c.map.bar;assert(!c.run());
  assert(c.map.bar==before && !dsiPendingBacking.armed);}
 {Case c;assert(c.run());assert(receipt[0]==2 && receipt[10]==unsigned(Surface::Status::Complete) && receipt[11]==17);
  assert(receipt[17]>receipt[16]);
  assert(c.map.reg(0x7019C)==c.record.gpu && c.map.reg(0x701AC)==c.record.gpu && c.map.reg(0x70188)==34);
  assert(c.xe.dsiSession.state==SessionState::Streaming && dsiPendingBacking.armed && c.xe.dsiArmedBackingOwned(&c.fb,&c.path));
  assert(!c.retire() && !discards);c.stopped();assert(c.retire() && discards==1 && !dsiPendingTarget(&c.fb));
  assert(c.xe.dsiSession.state==SessionState::Released && c.xe.dsiCleanup.retiredBacking==reinterpret_cast<uintptr_t>(&c.xe.dsiBoot));}
 for(unsigned fail=0;fail<2;++fail){Case c;
  if(fail==0)c.map.freeze=true;else display13FBIRQ.state.enabled=false;
  const auto before=c.map.bar;assert(!c.run());
  if(fail==1){assert(!dsiPendingBacking.armed && c.map.bar==before && c.xe.dsiSession.state==SessionState::Unbound);continue;}
  assert(dsiPendingBacking.armed && c.xe.dsiSession.state==SessionState::Faulted && !c.retire() && !discards);
  c.stopped();failDiscard=true;assert(!c.retire() && dsiPendingBacking.armed && c.xe.dsiSession.state==SessionState::Released);
  assert(c.xe.dsiArmedBackingOwned(&c.fb,&c.path));failDiscard=false;assert(c.retire() && discards==2);}
 {Case c;assert(c.run());auto &o=dsiPendingBacking;o.published=o.modeReady=true;
  put(&c.fb,0x4348,&c.record);display13FBPower.pipe[0].publishedScanout=reinterpret_cast<uintptr_t>(&c.record);
  assert(!c.retire() && dsiPublishedTarget(&c.fb));c.stopped();failDiscard=true;
  assert(!c.retire() && dsiPublishedTarget(&c.fb) && !o.modeReady && o.armed);
  failDiscard=false;assert(c.retire() && !Display13FBMemory::field<void*>(&c.fb,0x4348) && !display13FBPower.pipe[0].publishedScanout);}
 for(unsigned failure=0;failure<5;++failure) {Case c;assert(c.run());
  put(&c.fb,0x4348,uintptr_t(0));put(&c.fb,0x4288,uint8_t(0));
  failSlotReturn=failure==1;failCache=failure==2;failLogical=failure==3;failModeDBUF=failure==4;
  locks={1};gpuDepth=1;dsiPendingBacking.allocating=true;
  const bool published=c.xe.publishDSINativeBacking();dsiPendingBacking.allocating=false;gpuDepth=0;
  assert(published==(failure==0 || failure>=3) && slotReturns==1);
  const bool ready=c.xe.publishDSINativeMode(&c.fb);
  assert(ready==(failure==0));assert(cpuInitial==(failure==0 || failure==4) && cpuCaps==(failure==0 || failure==4));
  if(!failure)assert(dsiPublishedTarget(&c.fb) && dsiPendingBacking.modeReady && Display13FBMemory::field<uint32_t>(&c.fb,0x4330)==c.record.gpu);
  locks.clear();
 }
 puts("PASS production DSI surface: actual MMIO allowlist/WM/latch, GPU/owner/IRQ locking, retained partial switches and stopped cleanup retry");
}
'''
with tempfile.TemporaryDirectory(prefix='dsi-surface-adapter-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
