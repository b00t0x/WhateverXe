#!/usr/bin/env python3
"""Replay the production DSI resource publication boundary and lock order."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'WhateverXe/kern_xe.cpp').read_text()
body=re.search(r'bool XE::bindDSIResources\(.*?\n\}',source,re.S)[0]
boot=(root/'tests/dsi_boot.cpp').read_text()
io=boot[boot.index('struct IO {'):boot.index('Boot capture(')]
code=r'''
#include "kern_dsi_ggtt.hpp"
#include "kern_display13_power_bridge.hpp"
#include "kern_display13_fb_irq.hpp"
#include "kern_xelp_combo_clock.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace XELP::DSI;
IO_FIXTURE
unsigned lockLevel=0;
struct IORecursiveLock{};struct IOLock{};struct IOSimpleLock{};
void IORecursiveLockLock(IORecursiveLock*){assert(lockLevel==0);lockLevel=1;}
void IORecursiveLockUnlock(IORecursiveLock*){assert(lockLevel==1);lockLevel=0;}
void IOLockLock(IOLock*){assert(lockLevel==1);lockLevel=2;}
void IOLockUnlock(IOLock*){assert(lockLevel==2);lockLevel=1;}
bool IOSimpleLockLockDisableInterrupt(IOSimpleLock*){assert(lockLevel==2);lockLevel=3;return true;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock*,bool saved){assert(lockLevel==3 && saved);lockLevel=2;}
struct IOService {alignas(8) uint8_t bytes[0x1C20]{};IOService *getMetaClass(){return this;}unsigned getClassSize(){return sizeof(bytes);}};
namespace Display13FBMemory {template<class T>T field(const void *p,unsigned o){T v;memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}}
struct Map:IO {
 uint32_t dpt=0;bool loseWrite=false,sourceDrift=false;
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(&dpt)-0x42084;}
 uint32_t read(uint32_t reg){if(reg==0x42084 && regs[reg]!=dpt){writes.push_back(reg);if(loseWrite)dpt=regs[reg];else regs[reg]=dpt;if(sourceDrift)regs[0x6B000]^=1;}return IO::read(reg);}
};
using IOMemoryMap=Map;
void adlnFault(bool &fault,unsigned){assert(lockLevel==3);fault=true;}
struct Display13FBBootIO {
 Map *map;bool readable(uint32_t r){return map && map->readable(r);}
 uint32_t read(uint32_t r){return map->read(r);}
};
struct {IORecursiveLock *lock;uintptr_t thread=0;XELPComboClock::ClockAllocation allocation;} comboClockTransaction;
struct {IOLock *lock;IOService *controller;Map *map;Display13Transcoder::Bindings transcoders;
 Readout bootDSI;Display13Power::NativeBridge bridge;
 bool engineEnabled=false,memoryFaulted=false,clockFaulted=false;struct {bool begun=false;} dbuf;} display13FBPower;
struct {uintptr_t base=0;IOSimpleLock *lock=nullptr;IOService *controller=nullptr;Display13FBIRQ::State state;
 unsigned handlers=0,softwareHandlers=0,admittedPipes=0;bool quiescing=false;} display13FBIRQ;
XELPMemory::Layout xelpMemory;
struct XE {
 bool display13FBIRQReady=true,dsiNativeWriterHooksReady=true,dsiResourcesReady=false;IOService *adlnTopologyController;
 struct {unsigned dsi=1;} adlnOutputWiring;
 Profile dsiProfile;Boot dsiBoot;GGTTMode dsiGGTT;
 bool bindDSIResources(IOService *);
};
BODY
int main(){
 IORecursiveLock clockLock;IOLock ownerLock;IOSimpleLock irqLock;IOService controller;Map map;
 auto ptr=&irqLock;memcpy(controller.bytes+0x90,&ptr,sizeof(ptr));
 auto &c=display13FBPower;auto &irq=display13FBIRQ;auto &clock=comboClockTransaction;
 c.lock=&ownerLock;c.controller=&controller;c.map=&map;clock.lock=&clockLock;
 XE xe;xe.adlnTopologyController=&controller;xe.dsiProfile=profile();xelpMemory=map.memory;
 xe.dsiBoot=captureBoot(map,AlderLakeN::readDisplay(map,true),xe.dsiProfile,map.memory,true);
 assert(verifyBootPrefix(map,xe.dsiBoot,map.memory));
 map.regs[0x45454]=3;map.regs[0x42000]=0x8E380000;
 auto &b=c.bridge;auto &s=b.state;b.begun=true;b.selectedPorts=3;s.ready=true;s.allowed=Display13Power::All;
 const auto pg=Display13Power::dependencies(Display13Power::nativeScope(1));
 const auto ddi=Display13Power::dependencies(Display13Power::ddiDomain(0));s.borrowed=pg|ddi;
 for(unsigned i=0;i<Display13Power::Count;++i)if(s.borrowed&(1U<<i))s.refs[i]=2;
 b.pgRefs[0]=1;b.pgLease[0].owner=&s;b.pgLease[0].domains=pg;
 b.ddiRefs[0]=1;b.ddiLease[0].owner=&s;b.ddiLease[0].domains=ddi;
 const auto rejected=[&]{assert(!xe.bindDSIResources(&controller));assert(!lockLevel && !xe.dsiResourcesReady &&
  !irq.controller && c.transcoders.dsi0Pipe==UINT32_MAX && !clock.allocation.external[0]);};
 xe.dsiNativeWriterHooksReady=false;rejected();xe.dsiNativeWriterHooksReady=true;
 b.ddiRefs[0]=0;rejected();b.ddiRefs[0]=1;
 clock.thread=1;rejected();clock.thread=0;
 c.engineEnabled=true;rejected();c.engineEnabled=false;
 irq.handlers=1;rejected();irq.handlers=0;
 irq.state.begun=true;rejected();irq.state.begun=false;
 irq.softwareHandlers=1;rejected();irq.softwareHandlers=0;
 irq.quiescing=true;rejected();irq.quiescing=false;
 clock.allocation.binding[0]=2;rejected();clock.allocation.binding[0]=0;
 clock.allocation.binding[1]=2;rejected();clock.allocation.binding[1]=0;
 map.regs[0x6B000]^=1;rejected();map.regs[0x6B000]^=1;
 map.regs[0x164280]^=1;rejected();map.regs[0x164280]^=1;
 const auto initialBoot=xe.dsiBoot;
 assert(xe.bindDSIResources(&controller) && !lockLevel && xe.dsiResourcesReady);
 assert(c.transcoders.dsi0Pipe==0 && irq.state.transcoders.dsi0Pipe==0 && c.bootDSI.sameSource(xe.dsiBoot.source));
 assert(irq.controller==&controller && irq.lock==&irqLock && irq.base==map.getVirtualAddress() && irq.admittedPipes==1);
 assert(clock.allocation.external[0]==reinterpret_cast<uintptr_t>(&controller) && clock.allocation.binding[0]==2);
 assert(!xe.bindDSIResources(&controller) && !lockLevel && map.writes==std::vector<uint32_t>{0x42084});
 assert(xe.dsiGGTT.ready() && xe.dsiGGTT.writes==1 && xe.dsiBoot.dpt==(1U<<30));
 assert(b.pgRefs[0]==1 && b.ddiRefs[0]==1); // publication did not acquire/release someone else's lease
 for(bool lost:{false,true}){
  clock.allocation={};irq={};c.transcoders={};c.bootDSI={};c.memoryFaulted=false;
  xe.dsiResourcesReady=false;xe.dsiGGTT={};xe.dsiBoot=initialBoot;
  map=Map{};map.regs[0x45454]=3;map.regs[0x42000]=0x8E380000;
  map.loseWrite=lost;map.sourceDrift=!lost;
  assert(!xe.bindDSIResources(&controller) && !lockLevel && c.memoryFaulted && !xe.dsiResourcesReady);
  assert(!irq.controller && !irq.admittedPipes && c.transcoders.dsi0Pipe==UINT32_MAX && !xe.dsiBoot.usable());
  assert(clock.allocation.external[0]==reinterpret_cast<uintptr_t>(&controller) && b.pgRefs[0]==1 && b.ddiRefs[0]==1);
  assert(map.writes==std::vector<uint32_t>{0x42084} && xe.dsiGGTT.writes==1);
  assert(!xe.bindDSIResources(&controller) && map.writes.size()==1);
 }
 puts("PASS production DSI resources: clock-owner-IRQ lock order, retained leases/source, PLL ownership, GGTT readback, atomic mapping publication and retained leases after write failure");
}
'''.replace('IO_FIXTURE',io).replace('BODY',body)
# No kernel/Apple object contents are loaded; these leaves model publication only.
with tempfile.TemporaryDirectory(prefix='dsi-resources-') as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
