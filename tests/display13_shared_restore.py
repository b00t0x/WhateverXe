#!/usr/bin/env python3
"""Production shared-restore ordering around GGTT mode and engine enable.

Power-well restoration/publication are explicit leaves (tested by power bridge
suite). The actual wrapper, consumer-stop checks and GGTT transaction execute.
Readback failure must prevent engine enable and preserve pending restoration.
"""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
method=re.search(r'bool XE::restoreDisplay13ResourcesLocked\(.*?\n\}',s,re.S)[0]
method=method.replace('Display13Power::restore(', 'modelRestore(').replace('Display13Power::publishObject(', 'modelPublish(')
code=r'''
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_fb_irq.hpp"
#include "kern_display13_dbuf.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
#include <new>
struct IOWorkLoop {};
struct Map {std::vector<uint32_t> regs=std::vector<uint32_t>(0x400000);uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(regs.data());}};
bool reject=false;unsigned enables=0;
struct Display13FBBootIO {
 Map *map;bool readable(uint32_t r){return r<0x1000000;}
 uint32_t read(uint32_t r){return reject && r==0x42084 ? map->regs[r/4]&~(1U<<30):map->regs[r/4];}
};
struct Context {
 void *power=nullptr,*controller=nullptr;IOWorkLoop *workLoop=nullptr;Map *map=nullptr;
 Display13Power::NativeBridge bridge;Display13Transcoder::Bindings transcoders;
 bool clockFaulted=false,restorePending=false,engineEnabled=false,clockStopped=true;
 uint32_t restoreEpoch=0,resumeClockHz=0;
 struct {unsigned restorePending=0,selected=1,mbus=0x3,owned=0;}dbuf;
 struct Mode {void notePowerLoss(){}}tcMode[4];bool tcSignalReady[4]{};unsigned tcDPLink[4]{};
 struct {unsigned nativeHz(){return 192000000;}}clock;
}display13FBPower;
struct {void *lock=nullptr;bool powerLost=false,rearmPending=false;}display13FBIRQ;
unsigned IOSimpleLockLockDisableInterrupt(void*){return 0;}void IOSimpleLockUnlockEnableInterrupt(void*,unsigned){}
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 template<class...T>void setProperty(T...){} };
void display13FBPowerRecord(uint64_t*,unsigned,bool,const Display13Power::Result&){}
void adlnPublishPowerRecord(const char*,uint64_t*){}
template<class F>Display13Power::Result modelRestore(Display13FBBootIO&,Display13Power::State &state,bool,F) {
 state.suspended=false;Display13Power::Result result;result.status=Display13Power::Status::Complete;return result;
}
bool modelPublish(Display13Power::NativeBridge&,void*,unsigned,void*,unsigned){return true;}
using mach_vm_address_t=uintptr_t;
struct XE {
 bool display13FBLifecycleHooksReady=true;uintptr_t adlnWorkLoopInGate=0,adlnWorkLoopTryClose=0,adlnWorkLoopOpen=0;
 bool restoreDisplay13ResourcesLocked(void*,bool);
 bool enableDisplay13EngineLocked(void*) {
  ++enables;auto &c=display13FBPower;assert(c.map->regs[0x42084/4]&(1U<<30));
  c.engineEnabled=true;c.clockStopped=false;return true;
 }
}*callbackXE;
METHOD
bool inGate(const IOWorkLoop*){return true;}bool tryGate(IOWorkLoop*){return true;}void openGate(IOWorkLoop*){}
int main(){
 for(unsigned binding:{UINT32_MAX,0U,1U,2U})for(unsigned state=0;state<4;++state)for(bool active:{false,true}) {
  Map map;IOWorkLoop loop;XE xe;callbackXE=&xe;
  alignas(8) uint8_t ctl[0x1c20]{},power[0x80]{};
  *reinterpret_cast<void**>(ctl+0xe00)=&loop;
  *reinterpret_cast<void**>(ctl+0xc40)=power;*reinterpret_cast<void**>(ctl+0xc8)=power;*reinterpret_cast<void**>(power+0x10)=ctl;
  auto &c=display13FBPower;c.~Context();new(&c)Context;c.controller=ctl;c.power=power;c.map=&map;c.workLoop=&loop;
  c.bridge.begun=true;c.bridge.state.suspended=true;c.transcoders.dsi0Pipe=binding;
  xe.adlnWorkLoopInGate=reinterpret_cast<uintptr_t>(inGate);xe.adlnWorkLoopTryClose=reinterpret_cast<uintptr_t>(tryGate);xe.adlnWorkLoopOpen=reinterpret_cast<uintptr_t>(openGate);
  map.regs[0x45404/4]=0xfc0f;map.regs[0x4438c/4]=c.dbuf.mbus;
  map.regs[0x42084/4]=state==1?0x40000123:state==2?UINT32_MAX:0x123;
  const auto pipe=binding<3?binding:2;
  map.regs[c.transcoders.control(pipe)/4]=active?0xc0000000:0;
  if(binding<3)map.regs[0x6B400/4]=((pipe?pipe+4:0)<<12)|(active?0x80000000:0);
  reject=state==3;enables=0;
  const auto ready=xe.restoreDisplay13ResourcesLocked(power,true);
  const bool expected=!active && state<2;
  assert(ready==expected && enables==unsigned(expected) && c.restorePending==!expected);
  if(expected)assert(map.regs[0x42084/4]==0x40000123 && display13FBIRQ.powerLost && display13FBIRQ.rearmPending);
  if(active)assert(map.regs[0x42084/4]==(state==1?0x40000123:state==2?UINT32_MAX:0x123));
  // A later caller retries the same lost-power generation. Busy consumers
  // or transient register failure must not make retry permanently impossible.
  const auto epoch=c.restoreEpoch;
  map.regs[c.transcoders.control(pipe)/4]=0;
  if(binding<3)map.regs[0x6B400/4]&=~0x80000000U;
  reject=false;map.regs[0x42084/4]=0x123;
  if(!ready){assert(xe.restoreDisplay13ResourcesLocked(power,false));assert(c.restoreEpoch==epoch && !c.restorePending);}
  // Ordinary peer wake with retained engine/clock is a no-op, even if the
  // GGTT register would fail readback. No shared reprogramming while live.
  map.regs[c.transcoders.control(pipe)/4]=0xc0000000;
  if(binding<3)map.regs[0x6B400/4]|=0x80000000;
  map.regs[0x42084/4]=0x123;const auto oldEnables=enables;reject=true;
  assert(xe.restoreDisplay13ResourcesLocked(power,true));
  assert(enables==oldEnables && map.regs[0x42084/4]==0x123 && c.restoreEpoch==epoch);
 }
 puts("PASS production shared restore: stopped consumers, preserved bits, failed GGTT readback blocks engine and retains pending");
}
'''
with tempfile.TemporaryDirectory(prefix='shared-restore-') as d:
 p=Path(d);(p/'test.cpp').write_text(code.replace('METHOD',method))
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
