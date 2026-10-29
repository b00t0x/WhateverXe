#!/usr/bin/env python3
"""Exercise the production surface/retirement adapters on the stop fixture's BAR."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
functions='\n'.join(re.search(r'bool XE::'+name+r'\(.*?\n\}',s,re.S)[0] for name in
 ('switchDSINativeSurface','dsiArmedBackingOwned','retireDSINativeBacking'))
state=re.search(r'struct DSIPendingBackingOwner \{.*?\n\};',s,re.S)[0]
# Share the established real power/IRQ/clock fixture; no implementation bodies
# are rewritten. Allocation/PTE proof and CPU mapping have their own adapter test.
q=(root/'tests/dsi_quiesce_adapter.py').read_text()
q=q.split("code=r'''",1)[1].split("'''.replace",1)[0]
prelude=q[:q.index('struct XE {')]
prelude=prelude.replace('#include "kern_dsi_boot.hpp"','#include "kern_dsi_boot.hpp"\n#include "kern_dsi_surface.hpp"\n#include "kern_dsi_test_image.hpp"\n#include "kern_dsi_native_backing.hpp"\n#include "kern_display13_clock.hpp"')
b=(root/'tests/dsi_boot.cpp').read_text();prelude=prelude.replace('FIXTURE',b[b.index('struct IO {'):b.index('Boot capture(')])
prelude=prelude.replace('bool armedOwner=false,failNativeRetire=false;unsigned nativeRetires=0;IOService *pendingFB=nullptr;bool dsiPendingTarget(IOService *fb){return fb && pendingFB==fb;}',state+'\nDSIPendingBackingOwner dsiPendingBacking;\nbool dsiPendingTarget(IOService *fb){return fb && dsiPendingBacking.framebuffer==fb;}')
prelude=prelude[:prelude.index('void tick(unsigned us)')]+r'''
uint64_t elapsed=0;unsigned gpuDepth=1;bool failPaint=false,failDiscard=false;unsigned discards=0;
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
uint64_t receipt[24]{};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *name,const void *p,unsigned n){assert(!strcmp(name,"WEXDSISurface") && n==sizeof(receipt));memcpy(receipt,p,n);}};
struct XE {
 bool dsiNativeWriterHooksReady=true,dsiResourcesReady=true,dsiCacheHooksReady=true,dsiBackingHooksReady=true;
 bool dsiStopAttempted=false,dsiIRQParked=false,dsiDDIReleased=false;
 Profile dsiProfile;Boot dsiBoot;Quiesce::Result dsiStop;Clock::Result dsiClockStop;Session dsiSession;Session::Cleanup dsiCleanup;
 bool paintDSINativeBacking(unsigned phase){assert(phase==1 && locks==std::vector<unsigned>{1} && gpuDepth==1);
  if(failPaint)return false;dsiPendingBacking.image.digest=0x686999c5;return true;}
 bool discardDSINativeBacking(IOService *fb){assert(locks==std::vector<unsigned>{1} && !gpuDepth && !dsiPendingBacking.armed && dsiPendingTarget(fb));
  ++discards;if(failDiscard)return false;dsiPendingBacking={};return true;}
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
  o.image.complete=true;o.image.digest=0xf89939c5;elapsed=0;gpuDepth=1;failPaint=failDiscard=false;discards=0;memset(receipt,0,sizeof(receipt));
''')
code=prelude+functions+'\n'+case+r'''
 bool run(){locks={1};bool result=xe.switchDSINativeSurface();assert(locks==std::vector<unsigned>{1});locks.clear();dsiPendingBacking.allocating=false;gpuDepth=0;return result;}
 bool retire(){locks={1};bool r=xe.retireDSINativeBacking(&fb,&path);assert(locks==std::vector<unsigned>{1});locks.clear();return r;}
 void stopped(){xe.dsiStop.stage=Quiesce::Stage::Complete;xe.dsiStop.error=Quiesce::Error::None;xe.dsiIRQParked=xe.dsiDDIReleased=true;xe.dsiClockStop.status=Clock::Status::Complete;}
 ~Case(){assert(locks.empty());dsiPendingBacking={};}
};
int main(){
 {Case c;assert(c.run());assert(receipt[11]==unsigned(Surface::Status::Complete) && receipt[12]==17 && receipt[20]);
  assert(receipt[10]==0xf89939c5 && receipt[17]==0x686999c5 && receipt[19]>receipt[18]);
  assert(c.map.reg(0x7019C)==c.record.gpu && c.map.reg(0x701AC)==c.record.gpu && c.map.reg(0x70188)==34);
  assert(c.xe.dsiSession.state==SessionState::Streaming && dsiPendingBacking.armed && c.xe.dsiArmedBackingOwned(&c.fb,&c.path));
  assert(!c.retire() && !discards);c.stopped();assert(c.retire() && discards==1 && !dsiPendingTarget(&c.fb));
  assert(c.xe.dsiSession.state==SessionState::Released && c.xe.dsiCleanup.retiredBacking==reinterpret_cast<uintptr_t>(&c.xe.dsiBoot));}
 for(unsigned fail=0;fail<3;++fail){Case c;
  if(fail==0)c.map.freeze=true;else if(fail==1)failPaint=true;else display13FBIRQ.state.enabled=false;
  const auto before=c.map.bar;assert(!c.run());
  if(fail==2){assert(!dsiPendingBacking.armed && c.map.bar==before && c.xe.dsiSession.state==SessionState::Unbound);continue;}
  assert(dsiPendingBacking.armed && c.xe.dsiSession.state==SessionState::Faulted && !c.retire() && !discards);
  c.stopped();failDiscard=true;assert(!c.retire() && dsiPendingBacking.armed && c.xe.dsiSession.state==SessionState::Released);
  assert(c.xe.dsiArmedBackingOwned(&c.fb,&c.path));failDiscard=false;assert(c.retire() && discards==2);}
 puts("PASS production DSI surface: actual MMIO allowlist/WM/latch, second image frame, GPU/owner/IRQ locking, retained partial switches and stopped cleanup retry");
}
'''
with tempfile.TemporaryDirectory(prefix='dsi-surface-adapter-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
