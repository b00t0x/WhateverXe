#!/usr/bin/env python3
"""Fault injection for retiring the retained DSI PLL after actual host stop."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'tests/dsi_boot.cpp').read_text();fixture=s[s.index('struct IO {'):s.index('Boot capture(')]
code=r'''
#include "kern_dsi_clock.hpp"
#include "kern_dsi_boot.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace XELP::DSI;
FIXTURE
struct Sim:IO {
 unsigned dropped=0,pllWrites=0,ackFailure=0;bool held=true;
 bool ready(){return held;}
 void write(uint32_t reg,uint32_t value){writes.push_back(reg);if(reg==0x46014 && ++pllWrites==dropped)return;regs[reg]=value;if(reg==0x6B400 && !(value&(1U<<31)))regs[0x6B0D8]=0;}
 void delayUs(unsigned us){
  if(us>=1000)++regs[0x70040];
  if(!(regs[0x7B008]&(1U<<31)))regs[0x7B008]&=~(1U<<30);
  if(regs[0x6B0D8]&1)regs[0x6B0D8]=(regs[0x6B0D8]&~1U)|(1U<<16);
  if(!(regs[0x64000]&(1U<<31)))regs[0x64000]|=0x80;
  if(!(regs[0x46014]&(1U<<31)) && ackFailure!=1)regs[0x46014]&=~(1U<<30);
  if(!(regs[0x46014]&(1U<<27)) && ackFailure!=2)regs[0x46014]&=~(1U<<26);
 }
};
struct Case {
 Sim io;Readout link;Quiesce::Result host;XELPComboClock::ClockAllocation allocation;
 Case(){
  io.regs[0x42084]=1U<<30;io.regs[0x6B0D0]=0x01001040;io.regs[0x6001C]=0x0437077F;
  io.regs[0x6B010]=io.regs[0x6B00C];io.regs[0x70040]=100;
  io.regs[0x164280]&=~(1U<<11);io.regs[0x46010]=0xCC000000; // another live PLL
  link=decodeReadout([&](uint32_t r,uint32_t &v){v=io.read(r);return true;},38400);assert(link.complete());
  host=Quiesce::run(io,link,profile());assert(host.complete());io.writes.clear();
  allocation.external[0]=123;allocation.binding[0]=2;
 }
 Clock::Result run(bool irq=true){return Clock::retire(io,link,host,irq,allocation,123);}
 void retained(){assert(allocation.external[0]==123 && allocation.binding[0]==2);}
};
int main(){
 {Case c;const auto cfg0=c.io.regs[0x16428C],mux=c.io.regs[0x164280];const auto r=c.run();
  assert(r.complete() && r.writes==2 && r.after==0 && r.waitUs<=2000);
  assert(c.io.writes==std::vector<uint32_t>({0x46014,0x46014}) && !c.allocation.external[0] && !c.allocation.binding[0]);
  assert(c.io.regs[0x46010]==0xCC000000 && c.io.regs[0x16428C]==cfg0 && c.io.regs[0x164280]==mux && c.io.regs[0x45404]==0xFC0F);
  assert(!c.run().complete() && c.io.writes.size()==2);
 }
 for(unsigned drop:{1U,2U}){Case c;c.io.dropped=drop;const auto r=c.run();assert(!r.complete() && r.writes==drop && r.waitUs>=1000);c.retained();}
 for(unsigned ack:{1U,2U}){Case c;c.io.ackFailure=ack;const auto r=c.run();assert(!r.complete() && r.writes==ack && r.waitUs>=1000);c.retained();}
 for(auto reg:{0x45404U,0x7B008U,0x6B400U,0x70180U,0x70080U,0x64000U,0x7B808U,0x6BC00U,0x164280U,0x16428CU,0x164290U}){
  Case c;c.io.regs[reg]^=(reg==0x7B008 || reg==0x7B808 || reg==0x6B400 || reg==0x6BC00 || reg==0x64000 || reg==0x70180)?1U<<31:1;
  assert(!c.run().complete() && c.io.writes.empty());c.retained();
 }
 {Case c;assert(!c.run(false).complete() && c.io.writes.empty());c.retained();}
 {Case c;c.host.error=Quiesce::Error::Timeout;assert(!c.run().complete() && c.io.writes.empty());c.retained();}
 {Case c;c.io.held=false;assert(!c.run().complete() && c.io.writes.empty());c.retained();}
 {Case c;c.io.missing=0x46014;assert(!c.run().complete() && c.io.writes.empty());c.retained();}
 {Case c;c.io.regs[0x46014]=0x88000000;assert(!c.run().complete() && c.io.writes.empty());c.retained();}
 for(bool software:{false,true}){Case c;if(software)c.allocation.binding[1]=2;else c.io.regs[0x164280]|=1U<<2;
  assert(c.run().status==Clock::Status::Peer && c.io.writes.empty());c.retained();}
 puts("PASS DSI PLL retirement: real host stop required, exclusive owner, peer exclusion, ordered unlock/power ACK, lost-write/timeouts retain allocation, no power-well/peer writes");
}
'''.replace('FIXTURE',fixture)
with tempfile.TemporaryDirectory(prefix='dsi-clock-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
