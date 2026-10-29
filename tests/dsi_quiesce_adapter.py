#!/usr/bin/env python3
"""Exercise the production native DSI stop boundary with real host/IRQ cores."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
body=re.search(r'bool XE::quiesceDSIPath\(.*?\n\}',s,re.S)[0]
b=(root/'tests/dsi_boot.cpp').read_text();fixture=b[b.index('struct IO {'):b.index('Boot capture(')]
code=r'''
#include "kern_dsi_quiesce.hpp"
#include "kern_dsi_boot.hpp"
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_power_bridge.hpp"
#include "kern_xelp_combo_clock.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <new>
using namespace XELP::DSI;
FIXTURE
std::vector<unsigned> locks;
struct IORecursiveLock{unsigned rank;};struct IOLock{};struct IOSimpleLock{};
void IORecursiveLockLock(IORecursiveLock *p){assert(locks.empty() || locks.back()<p->rank);locks.push_back(p->rank);}
void IORecursiveLockUnlock(IORecursiveLock *p){assert(locks.back()==p->rank);locks.pop_back();}
void IOLockLock(IOLock*){assert(!locks.empty() && locks.back()<3);locks.push_back(3);}
void IOLockUnlock(IOLock*){assert(locks.back()==3);locks.pop_back();}
bool IOSimpleLockLockDisableInterrupt(IOSimpleLock*){assert(locks.back()==3);locks.push_back(4);return true;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock*,bool v){assert(v && locks.back()==4);locks.pop_back();}
struct OSObject {alignas(8) uint8_t bytes[0x4B90]{};unsigned refs=1,size=sizeof(bytes);
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}
 void retain(){++refs;}void release(){assert(refs>1);--refs;}};
using IOService=OSObject;
struct IOWorkLoop:OSObject {bool gate=false,busy=false;unsigned closes=0,opens=0;};
bool inGate(const IOWorkLoop *p){assert(locks==std::vector<unsigned>{1});return p->gate;}
bool tryClose(IOWorkLoop *p){assert(locks==std::vector<unsigned>{1});++p->closes;if(p->busy)return false;return p->gate=true;}
void open(IOWorkLoop *p){assert(locks==std::vector<unsigned>{1} && p->gate);p->gate=false;++p->opens;}
struct IOMemoryMap {
 std::vector<uint32_t> bar=std::vector<uint32_t>(0x1000000/4);bool freeze=false,pending=false,failMasterRestore=false,failMasterMask=false;
 unsigned masterWrites=0;bool gtOnly=false;
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(bar.data());}size_t getLength(){return 0x1000000;}
 uint32_t &reg(uint32_t r){return bar[r/4];}
}*activeMap;
struct Display13FBBootIO {
 IOMemoryMap *map;bool readable(uint32_t r){return map && r<0x1000000 && !(r&3);}
 uint32_t read(uint32_t r){assert(locks.back()==3);return map->reg(r);}
};
struct Display13FBIRQIO {
 uintptr_t base;bool readable(uint32_t r){return base && r<0x1000000 && !(r&3);}
 uint32_t read(uint32_t r){assert(locks.back()==4);return *reinterpret_cast<uint32_t*>(base+r);}
 void write(uint32_t r,uint32_t v){assert(locks.back()==4);
  if(r==Display13FBIRQ::Master){++activeMap->masterWrites;
   if((activeMap->masterWrites==1 && activeMap->failMasterMask) || (activeMap->masterWrites==2 && activeMap->failMasterRestore))return;
  }
  auto &reg=*reinterpret_cast<uint32_t*>(base+r);
  if(r==Display13FBIRQ::PipeIIR(0))reg&=~v;else reg=v;
 }
};
struct {IORecursiveLock *lock=nullptr;uintptr_t thread=0;XELPComboClock::ClockAllocation allocation;}comboClockTransaction;
struct {IOLock *lock=nullptr;IOService *controller=nullptr;IOMemoryMap *map=nullptr;IOWorkLoop *workLoop=nullptr;OSObject *dsiPort=nullptr;
 Display13Transcoder::Bindings transcoders;Readout bootDSI;Display13Power::NativeBridge bridge;bool clockFaulted=false,memoryFaulted=false;
 struct {unsigned updating=0;}dbuf;
 struct {IOService *memoryFramebuffer=nullptr;bool destroyingPath=false,wmProgramming=false,wmCommitting=false;}pipe[3];
}display13FBPower;
struct {uintptr_t base=0;IOSimpleLock *lock=nullptr;IOService *controller=nullptr;Display13FBIRQ::State state;
 unsigned handlers=0,softwareHandlers=0,admittedPipes=0,blockedPipes=0;bool quiescing=false;
 Display13FBIRQ::HandlerScopes scopes;}display13FBIRQ;
void adlnFault(bool &fault,unsigned){assert(locks==std::vector<unsigned>({1,2,3}));fault=true;}
bool mappingBound=true;
bool adlnWriterMappingBound(){return mappingBound;}
bool adlnIRQOwner(IOService *p){return p && p==display13FBIRQ.controller;}
namespace TGLFBBinding {enum class Status{Complete,Unavailable};struct Binding{Status status=Status::Complete;unsigned pipe=0,ddi=0;};}
TGLFBBinding::Binding pathBinding;
TGLFBBinding::Binding adlnInspectPathBinding(IOService*,OSObject*,void*,unsigned){return pathBinding;}
void adlnIRQCache(IOService*,Display13FBIRQIO&){assert(locks.back()==4);}
void adlnPublishMemory(const char*,unsigned,void*,size_t){assert(locks.empty());}
void tick(unsigned us){assert(locks==std::vector<unsigned>({1,2,3}));auto &m=*activeMap;
 if(!m.freeze && us>=1000)++m.reg(0x70040);
 if(!(m.reg(0x7B008)&(1U<<31)))m.reg(0x7B008)&=~(1U<<30);
 if(m.reg(0x6B0D8)&1)m.reg(0x6B0D8)|=1U<<16;
 if(!(m.reg(0x64000)&(1U<<31)))m.reg(0x64000)|=0x80;
 if(m.pending){auto c=display13FBPower.controller;*reinterpret_cast<uint32_t*>(c->bytes+0xE08)|=1;}
 if(m.gtOnly){display13FBIRQ.handlers=1;display13FBIRQ.scopes.total=1;}
}
void IOSleep(unsigned ms){tick(ms*1000);}void IODelay(unsigned us){tick(us);}
struct XE {
 uintptr_t adlnWorkLoopInGate=reinterpret_cast<uintptr_t>(inGate),adlnWorkLoopTryClose=reinterpret_cast<uintptr_t>(tryClose),adlnWorkLoopOpen=reinterpret_cast<uintptr_t>(open);
 struct {unsigned dsi=1;operator unsigned()const{return dsi;}}adlnOutputWiring;void *adlnPlatform=nullptr;
 bool dsiResourcesReady=true,dsiStopAttempted=false,dsiIRQParked=false;
 IOService *adlnTopologyController=nullptr;Profile dsiProfile;Boot dsiBoot;Quiesce::Result dsiStop;
 bool quiesceDSIPath(IOService*,IOService*,OSObject*);
};
BODY
template<class T>void put(void *p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
struct Case {
 XE xe;IOService controller,fb;OSObject path,port;IOWorkLoop loop;IOMemoryMap map;
 IORecursiveLock fbLock{1},clockLock{2};IOLock ownerLock;IOSimpleLock irqLock;
 Case(){
  using Power=decltype(display13FBPower);display13FBPower.~Power();new (&display13FBPower) Power;
  display13FBIRQ={};comboClockTransaction={};mappingBound=true;pathBinding={};activeMap=&map;
  IO defaults;for(const auto &v:defaults.regs)map.reg(v.first)=v.second;
  map.reg(0x42084)=1U<<30;map.reg(0x6B0D0)=0x01001040;map.reg(0x6001C)=0x0437077F;map.reg(0x6B010)=map.reg(0x6B00C);
  map.reg(0x45454)=3;map.reg(0x42000)=0x8E380000;map.reg(0x70040)=100;
  // Keep a real peer IRQ/plane image live and make preservation observable.
  map.reg(0x71008)=0xC0000000;map.reg(0x71180)=0x94001000;map.reg(0x61400)=0x90000000;
  map.reg(Display13FBIRQ::Master)=Display13FBIRQ::Enable;
  map.reg(Display13FBIRQ::PipeIIR(0)+4)=Display13FBIRQ::PipeOwned;
  map.reg(Display13FBIRQ::PipeIIR(1)+4)=Display13FBIRQ::PipeOwned;
  auto &c=display13FBPower;auto &irq=display13FBIRQ;auto &clock=comboClockTransaction;
  c.lock=&ownerLock;c.controller=&controller;c.map=&map;c.workLoop=&loop;c.dsiPort=&port;
  c.pipe[0].memoryFramebuffer=&fb;c.transcoders.dsi0Pipe=0;
  xe.adlnTopologyController=&controller;xe.dsiProfile=profile();
  c.bootDSI=decodeReadout([&](uint32_t r,uint32_t &v){v=map.reg(r);return true;},38400);assert(c.bootDSI.matches(xe.dsiProfile));xe.dsiBoot.source=c.bootDSI;
  clock.lock=&clockLock;clock.allocation.external[0]=reinterpret_cast<uintptr_t>(&controller);clock.allocation.binding[0]=2;
  irq.controller=&controller;irq.lock=&irqLock;irq.base=map.getVirtualAddress();irq.admittedPipes=3;
  irq.state.begun=true;irq.state.enabled=true;irq.state.active=3;irq.state.configured=3;irq.state.transcoders=c.transcoders;
  auto &b=c.bridge;auto &s=b.state;b.begun=true;b.selectedPorts=3;s.ready=true;s.allowed=Display13Power::All;
  const auto pg=Display13Power::dependencies(Display13Power::nativeScope(1)),ddi=Display13Power::dependencies(Display13Power::ddiDomain(0));s.borrowed=pg|ddi;
  for(unsigned i=0;i<Display13Power::Count;++i)if(s.borrowed&(1U<<i))s.refs[i]=2;
  b.pgRefs[0]=1;b.pgLease[0].owner=&s;b.pgLease[0].domains=pg;b.ddiRefs[0]=1;b.ddiLease[0].owner=&s;b.ddiLease[0].domains=ddi;
  put(&fb,0x4220,&fbLock);put(&fb,0x4A00,1U);put(&fb,0x4A08,&path);put(&path,0x3644,2U);put(&path,0x3648,&port);put(&port,0x550,1U);
  put(&controller,0xE00,&loop);put(&controller,0x90,&irqLock);
 }
 bool run(){bool r=xe.quiesceDSIPath(&controller,&fb,&path);assert(locks.empty() && loop.refs==1);return r;}
 void retained(){auto &b=display13FBPower.bridge;assert(b.pgRefs[0]==1 && b.ddiRefs[0]==1);
  assert(comboClockTransaction.allocation.external[0]==reinterpret_cast<uintptr_t>(&controller));
  assert(map.reg(0x46014)==0xCC000000 && map.reg(0x45404)==0xFC0F);
  assert(Display13FBMemory::field<uint32_t>(&path,0x3644)==2);
 }
};
int main(){
 for(bool own:{false,true}){Case c;c.loop.gate=own;c.map.gtOnly=true;
  put(&c.controller,0xE08,2U);put(&c.controller,0xE10,1U<<6);
  assert(c.run() && c.xe.dsiStop.complete() && c.xe.dsiIRQParked);
  assert(display13FBPower.memoryFaulted);
  assert(c.xe.dsiStop.writes==8 && display13FBIRQ.state.active==2 && display13FBIRQ.state.enabled);
  assert(!display13FBIRQ.quiescing && !display13FBIRQ.blockedPipes && c.loop.gate==own);
  assert(c.loop.opens==unsigned(!own) && c.loop.closes==unsigned(!own));
  assert(c.map.reg(0x71180)==0x94001000 && c.map.reg(Display13FBIRQ::PipeIIR(1)+4)==Display13FBIRQ::PipeOwned);
  assert(Display13FBMemory::field<uint32_t>(&c.controller,0xE08)==2 && Display13FBMemory::field<uint32_t>(&c.controller,0xE10)==(1U<<6));
  c.retained();const auto before=c.map.bar;assert(!c.run() && c.map.bar==before && c.xe.dsiIRQParked); // one-shot journal
 }
 for(unsigned bad=0;bad<12;++bad){Case c;
  switch(bad){case 0:c.loop.busy=true;break;case 1:mappingBound=false;break;
   case 2:display13FBIRQ.softwareHandlers=1;break;case 3:display13FBIRQ.handlers=1;display13FBIRQ.scopes.total=1;display13FBIRQ.scopes.display=1;break;
   case 4:put(&c.controller,0xE08,1U);break;case 5:put(&c.controller,0xE0C,1U);break;
   case 6:put(&c.path,0x35F0,1U);break;case 7:comboClockTransaction.allocation.external[0]=0;break;
   case 8:display13FBPower.bridge.ddiRefs[0]=0;break;case 9:display13FBIRQ.scopes.total=1;break;
   case 10:put(&c.controller,0xE10,1U);break;case 11:pathBinding.ddi=1;break;}
  const auto before=c.map.bar;assert(!c.run() && c.map.bar==before && !c.xe.dsiStopAttempted && !c.loop.gate);
 }
 {Case c;c.map.failMasterMask=true;const auto before=c.map.bar;assert(!c.run() && c.map.bar==before && !c.xe.dsiStopAttempted);
  assert(display13FBIRQ.state.faulted && !display13FBIRQ.state.enabled);c.retained();}
 {Case c;c.map.freeze=true;assert(!c.run() && c.xe.dsiStopAttempted && !c.xe.dsiIRQParked);
  assert(c.xe.dsiStop.stage==Quiesce::Stage::Plane && display13FBIRQ.state.enabled && display13FBIRQ.state.active==3);c.retained();}
 {Case c;c.map.pending=true;assert(!c.run() && c.xe.dsiStop.complete() && !c.xe.dsiIRQParked);
  assert(display13FBIRQ.state.active==3 && Display13FBMemory::field<uint32_t>(&c.controller,0xE08)==1);c.retained();}
 {Case c;c.map.failMasterRestore=true;assert(!c.run() && c.xe.dsiStop.complete() && !c.xe.dsiIRQParked);
  assert(display13FBIRQ.state.faulted && !display13FBIRQ.state.enabled && !c.map.reg(Display13FBIRQ::Master));c.retained();}
 puts("PASS production DSI quiesce: real host/IRQ cores, FB-gate-clock-owner ordering, no wait under IRQ lock, actual queues/scopes, GT/peer preservation, retained resources and one-shot failure journal");
}
'''.replace('FIXTURE',fixture).replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='dsi-quiesce-adapter-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
