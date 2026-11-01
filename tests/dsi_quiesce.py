#!/usr/bin/env python3
"""Fault injection for the DSI host shutdown core; no simulated cleanup proof."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'tests/dsi_boot.cpp').read_text();io=s[s.index('struct IO {'):s.index('Boot capture(')]
code=r'''
#include "kern_dsi_quiesce.hpp"
#include "kern_xelp_memory_layout.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace XELP::DSI;
IO_FIXTURE
struct Sim:IO {
 uint32_t drop=UINT32_MAX,elapsed=0,failStage=0;bool frozen=false,unbound=false,losePower=false,pendingAfterStop=false;
 bool ready(){return !unbound;}
 void write(uint32_t reg,uint32_t value){writes.push_back(reg);if(reg!=drop){regs[reg]=value;if(reg==0x6B400 && !(value&(1U<<31)))regs[0x6B0D8]=0;}}
 void delayUs(unsigned us){elapsed+=us;
  if(losePower){regs[0x45404]=0;return;}
  if(!frozen && us>=1000)++regs[0x70040];
  if(!(regs[0x7B008]&(1U<<31)) && failStage!=1){regs[0x7B008]&=~(1U<<30);if(pendingAfterStop)regs[0x6B0D0]=0;}
  if((regs[0x6B0D8]&1) && failStage!=2)regs[0x6B0D8]=(regs[0x6B0D8]&~1U)|(1U<<16);
  if(!(regs[0x64000]&(1U<<31)) && failStage!=3)regs[0x64000]|=0x80;
 }
};
struct Fixture {
 Sim io;Profile p=profile();Readout link;
 Fixture(bool cursor=false){
  io.regs[0x42084]=1U<<30;io.regs[0x6B0D0]=0x01001040;
  io.regs[0x6001C]=0x0437077F;io.regs[0x6B010]=io.regs[0x6B00C];io.regs[0x70040]=100;
  io.regs[0x70080]=cursor?7:0;io.regs[0x70084]=cursor?0x200000:0;
  // A live external pipe must survive the target-only stop.
  io.regs[0x71008]=0xC0000000;io.regs[0x61400]=0x90000000;io.regs[0x71180]=0x94001000;
  io.regs[0x7119C]=0x30000000;io.regs[0x64100]=0x80000000;io.regs[0x164280]|=1U<<10;
  link=decodeReadout([&](uint32_t r,uint32_t &v){v=io.read(r);return true;},38400);assert(link.matches(p));
 }
 Quiesce::Result run(){return Quiesce::run(io,link,p);}
};
int main(){
 for(bool cursor:{false,true}){Fixture f(cursor);const auto mux=f.io.regs[0x164280];const auto r=f.run();
  assert(r.complete() && r.writes==f.io.writes.size() && r.writes==(cursor?11U:8U));
  assert(r.frameAfter>r.frameBefore && f.io.regs[0x7B008]==0 && !(f.io.regs[0x6B400]&(1U<<31)));
  Display13Transcoder::Bindings map;map.dsi0Pipe=0;assert(Display13FBIRQ::consumersStopped(f.io,1,map));
  assert(f.io.regs[0x164280]==mux && f.io.regs[0x46014]==0xCC000000 && f.io.regs[0x45404]==0xFC0F);
  assert(f.io.regs[0x71180]==0x94001000 && f.io.regs[0x7119C]==0x30000000 && f.io.regs[0x64100]==0x80000000);
  for(auto reg:f.io.writes)assert(reg==0x70080 || reg==0x70084 || reg==0x70088 || reg==0x70180 || reg==0x7019C ||
   reg==0x7B008 || reg==0x6B0D8 || reg==0x6B400 || reg==0x164280 || reg==0x64000);
  // Host stopped is not an IRQ/clock/panel teardown proof, and cannot be
  // silently replayed using the now-stale active-link snapshot.
  const auto count=f.io.writes.size();assert(!f.run().complete() && f.io.writes.size()==count);
 }
 for(auto reg:{0x6B0D0U,0x6B0D8U,0x6B000U,0x70780U,0x70B08U,0x6001CU}){Fixture f;
  f.io.regs[reg]^=reg==0x70780?1U<<31:1;
  assert(!f.run().complete() && f.io.writes.empty());
 }
 {Fixture f;f.io.unbound=true;assert(!f.run().complete() && f.io.writes.empty());}
 for(auto reg:{0x70180U,0x7B008U,0x6B0D8U,0x6B400U,0x164280U,0x64000U}){Fixture f;f.io.drop=reg;
  const auto r=f.run();assert(!r.complete() && r.error!=Quiesce::Error::None && !f.io.writes.empty());
  assert(f.io.regs[0x46014]==0xCC000000 && f.io.regs[0x45404]==0xFC0F); // no PLL/power release on uncertainty
 }
 for(unsigned stage=1;stage<=3;++stage){Fixture f;f.io.failStage=stage;const auto r=f.run();
  assert(!r.complete() && r.error==Quiesce::Error::Timeout);
  assert(r.stage==(stage==1?Quiesce::Stage::Transcoder:stage==2?Quiesce::Stage::ULPS:Quiesce::Stage::Buffer));
 }
 {Fixture f;f.io.pendingAfterStop=true;const auto r=f.run();assert(!r.complete() && r.stage==Quiesce::Stage::Commands && r.error==Quiesce::Error::Commands);assert(!f.io.regs[0x6B0D8]);}
 {Fixture f;f.io.frozen=true;const auto r=f.run();assert(!r.complete() && r.stage==Quiesce::Stage::Plane && r.error==Quiesce::Error::Timeout);
  assert(f.io.regs[0x7B008]==f.link.conf);}
 {Fixture f;f.io.losePower=true;const auto r=f.run();assert(!r.complete() && r.error==Quiesce::Error::Ownership);
  assert(f.io.writes==std::vector<uint32_t>({0x70180,0x7019C}));}
 puts("PASS DSI host quiesce: cursor/plane/frame then transcoder/ULPS/port, peer preservation, FIFO precondition, bounded failures and no resource release");
}
'''.replace('IO_FIXTURE',io)
with tempfile.TemporaryDirectory(prefix='dsi-quiesce-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
