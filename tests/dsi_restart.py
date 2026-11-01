#!/usr/bin/env python3
"""Stopped-host restart with register side effects and optional saved VBT input."""
from pathlib import Path
import subprocess, tempfile, sys
root=Path(__file__).resolve().parents[1]
boot=(root/'tests/dsi_boot.cpp').read_text()
io=boot[boot.index('struct IO {'):boot.index('Boot capture(')]
profile=(root/'tests/dsi_profile.cpp').read_text()
fixture=profile[profile.index('using Bytes='):profile.index('int main(')]
code=r'''
#include "kern_dsi_restart.hpp"
#include "kern_dsi_panel.hpp"
#include "kern_xelp_memory_layout.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>
using namespace XELP::DSI;
IO_FIXTURE
PROFILE_FIXTURE
struct Sim:IO {
 bool owned=true,dropPLLDisable=false,dropPLLPower=false,dropPLLLock=false,dropBuffer=false,dropLink=false;
 bool ulpsRequest=false;
 uint32_t drop=UINT32_MAX,elapsed=0;
 std::vector<uint32_t> gpioTrace;
 bool gpio(unsigned role,unsigned value){const auto r=Panel::gpio(*this,role,value);if(r.complete)gpioTrace.push_back(role*2+value);return r.complete;}
 bool ready(){return owned;}
 void write(uint32_t reg,uint32_t value){
  assert(owned);writes.push_back(reg);if(reg==drop)return;
  if(reg==0x7B008){regs[reg]=(value&~(1U<<30))|(regs[reg]&(1U<<30));return;}
  if(reg==0x1621A0){regs[reg]=value|0x400;return;} // non-field status survives a zero write
  if(reg==0x46014){regs[reg]=(value&~0x44000000U)|(regs[reg]&0x44000000U);return;}
  if(reg==0x64000){regs[reg]=(value&~0x80U)|(regs[reg]&0x80);return;}
  if(reg==0x6B0D8){ulpsRequest=value&1;regs[reg]=(value&~0x30001U)|(regs[reg]&0x30000);return;}
  if(reg==0x6B400 && !(value&(1U<<31))){ulpsRequest=false;regs[0x6B0D8]=0;regs[0x6B030]&=~(1U<<20);}
  if(reg==0x6B030){regs[reg]=(value&~(1U<<20))|(regs[reg]&(1U<<20));return;}
  if(reg==0x162604 || reg==0x162688 || reg==0x162694){
   for(unsigned lane=0;lane<4;++lane)regs[reg+0x200+0x100*lane]=value;
   return;
  }
  regs[reg]=value;
 }
 void delayUs(unsigned us){
  elapsed+=us;
  auto &conf=regs[0x7B008];if(conf&(1U<<31))conf|=1U<<30;else conf&=~(1U<<30);
  if(us>=1000 && (conf&0xC0000000U)==0xC0000000U){++regs[0x70040];regs[0x701AC]=regs[0x7019C];}
  auto &pll=regs[0x46014];
  if(pll&(1U<<27)){if(!dropPLLPower)pll|=1U<<26;}else if(!dropPLLPower)pll&=~(1U<<26);
  if(pll&(1U<<31)){if(!dropPLLLock)pll|=1U<<30;}else if(!dropPLLDisable)pll&=~(1U<<30);
  if(!dropBuffer){if(regs[0x64000]&(1U<<31))regs[0x64000]&=~0x80U;else regs[0x64000]|=0x80;}
  if(ulpsRequest)regs[0x6B0D8]|=1U<<16;
  if(!dropLink && (regs[0x6B400]&(1U<<31)))regs[0x6B030]|=1U<<20;
 }
};
struct Fixture {
 Sim io;Bytes vbt;Profile p;Readout source;Restart::State state;
 XELPComboClock::ClockAllocation allocation;
 Fixture(){vbt=fixture();p=parse(vbt);source=decodeReadout([&](uint32_t r,uint32_t &v){v=io.read(r);return true;},38400);
  assert(source.matches(p));allocation.external[0]=1;allocation.binding[0]=2;
  io.regs[0x7B008]=0;io.regs[0x70180]&=~(1U<<31);io.regs[0x6B0D0]=0x1040;
  io.regs[0x164280]|=1U<<10;io.regs[0x71180]=0x94000000;io.regs[0x64100]=0x80000000;
 }
 bool stop(){return Restart::stop(io,source,p,allocation,1,state);}
 bool start(){return Restart::start(io,allocation,1,state);}
};
struct Lifecycle {
 XELPComboClock::ClockAllocation &allocation;Restart::State &state;bool breakStart=false;
 bool stop(Sim &io,const Readout &source,const Profile &p){return Restart::stop(io,source,p,allocation,1,state);}
 bool start(Sim &io,Readout &source){
  if(breakStart)io.dropPLLPower=true;
  if(!Restart::start(io,allocation,1,state))return false;
  source=Restart::expected(source,state);return source.complete();
 }
};
void panelCycle(Fixture &f,bool fail=false){
 f.io.regs[0x7B008]=0xC0000000;f.io.regs[0x70180]=0x94000008;
 f.io.regs[0x42084]=1U<<30;f.io.regs[0x6001C]=0x0437077F;f.io.regs[0x6B010]=f.io.regs[0x6B00C];
 f.io.regs[0xC7204]=15;f.io.regs[0xC4030]=0x84;
 f.source.mux=f.io.regs[0x164280];
 Panel::Result panel;Lifecycle lifecycle{f.allocation,f.state,fail};
 Panel::cycle(f.io,f.source,f.p,0x94000008,0,panel,lifecycle);
 if(fail){assert(!panel.complete() && panel.stage==Panel::Stage::HostOn && panel.error==Panel::Error::Host);
  assert(!(f.io.regs[0x70180]&(1U<<31)) && !f.io.regs[0x7B008]);return;}
 if(!panel.complete())fprintf(stderr,"panel stage=%u error=%u reg=%x value=%x host=%u seq=%u restartStage=%u error=%u reg=%x value=%x\n",unsigned(panel.stage),unsigned(panel.error),panel.reg,panel.value,unsigned(panel.host.error),panel.host.sequenceId,unsigned(f.state.stage),unsigned(f.state.error),f.state.reg,f.state.value);
 assert(panel.complete() && panel.source.matches(f.p) && panel.source.cfg0!=f.source.cfg0);
 assert(Surface::source(f.io,panel.source) && !Surface::source(f.io,f.source));
 assert(panel.host.completedOperations==f.p.sequences.packets+f.p.sequences.delays+f.p.sequences.gpios);
 assert(f.io.regs[0x70180]==0x94000008 && f.io.regs[0x7B008]==0xC0000000);
 assert(f.allocation.external[0]==1 && f.allocation.binding[0]==2);
}
void retainedCycle(Fixture &f,unsigned fault=0){
 f.io.regs[0x7B008]=0xC0000000;f.io.regs[0x70180]=0x94001000;f.io.regs[0x7019C]=f.io.regs[0x701AC]=0x44E00000;
 f.io.regs[0x42084]=1U<<30;f.io.regs[0x6001C]=0x0437077F;f.io.regs[0x6B010]=f.io.regs[0x6B00C];
 f.io.regs[0xC7204]=15;f.io.regs[0xC4030]=0x84;f.source.mux=f.io.regs[0x164280];
 Panel::Shutdown shutdown;
 const auto stopped=Quiesce::run(f.io,f.source,f.p,shutdown);
 assert(stopped.complete() && shutdown.complete);
 auto count=f.io.writes.size();
 if(fault==1)f.allocation.external[0]=2;
 if(fault==2)f.io.regs[0x7B008]=0xC0000000;
 if(fault==3)f.io.regs[0x16428C]^=1;
 if(fault){assert(!Restart::stopQuiesced(f.io,f.source,f.p,stopped,f.allocation,1,f.state));
  assert(f.io.writes.size()==count && !f.state.stopped);return;}
 assert(Restart::stopQuiesced(f.io,f.source,f.p,stopped,f.allocation,1,f.state));
 assert(f.state.stopped && !f.io.regs[0x46014] && f.allocation.external[0]==1 && f.allocation.binding[0]==2);
 Panel::Result panel;Lifecycle lifecycle{f.allocation,f.state};
 Panel::cycle(f.io,f.source,f.p,0x94001000,0x44E00000,panel,lifecycle,&shutdown);
 if(!panel.complete())fprintf(stderr,"resume stage=%u error=%u reg=%x host=%u restart=%u/%u\n",unsigned(panel.stage),unsigned(panel.error),panel.reg,unsigned(panel.host.error),unsigned(f.state.stage),unsigned(f.state.error));
 assert(panel.complete() && f.state.started && Surface::source(f.io,panel.source));
 assert(f.io.regs[0x70180]==0x94001000 && f.io.regs[0x7019C]==0x44E00000 && f.io.regs[0x701AC]==0x44E00000 && f.io.regs[0x46014]==0xCC000000);
 assert(f.allocation.external[0]==1 && f.allocation.binding[0]==2);
 assert(f.io.regs[0x71180]==0x94000000 && f.io.regs[0x64100]==0x80000000);
}
void successful(Fixture &f){
 assert(f.stop() && f.state.stopped && !f.state.started);
 assert(f.io.regs[0x46014]==0 && f.io.regs[0x6B400]==0x30006 && f.io.regs[0x64000]==0x80 && f.io.regs[0x6B094]==0);
 const auto oldCfg=f.source.cfg0;f.io.regs[0x16428C]=0x12345; // prove source PLL dump is not replayed
 assert(f.start() && f.state.stage==Restart::Stage::Ready);
 assert(f.io.regs[0x16428C]==f.state.config.pll.cfg0() && f.io.regs[0x16428C]!=oldCfg);
 assert(f.io.regs[0x46014]==0xCC000000 && f.io.regs[0x64000]==0x80000000 && f.io.regs[0x6B094]==1);
 assert(f.io.regs[0x6B030]==0x20130304 && f.io.regs[0x6B400]==f.state.config.function);
 assert(f.io.regs[0x6B090]==56 && f.io.regs[0x162190]==56 && (f.io.regs[0x1621A0]&0xFF0000)==0 && (f.io.regs[0x1621A0]&0x400));
 assert(f.io.regs[0x162180]==0xC8690000 && f.io.regs[0x162184]==0x84830083);
 assert(f.io.regs[0x162894]==0xE0080030 && f.io.regs[0x162888]==0x1598);
 for(unsigned lane=0;lane<4;++lane){assert(f.io.regs[0x162804+0x100*lane]==4);
  assert(f.io.regs[0x162890+0x100*lane]==((lane==2?0:1U<<31)|0x3F));}
 assert(f.io.regs[0x70180]==0x14000008 && !f.io.regs[0x7B008]); // video remains off for INIT_OTP
 assert(f.io.regs[0x71180]==0x94000000 && f.io.regs[0x64100]==0x80000000);
 assert(f.allocation.external[0]==1 && f.allocation.binding[0]==2);
 for(auto reg:f.io.writes){if(!Restart::writable(reg,1))fprintf(stderr,"unlisted write %x\n",reg);assert(Restart::writable(reg,1) && reg!=0x46010 && reg!=0x164284);}
 auto count=f.io.writes.size();assert(!f.start() && !f.stop() && f.io.writes.size()==count);
 // Once the separate panel/plane adapter starts video, the decoder must
 // accept the new source with profile-derived PLL/timing/polarity.
 f.io.regs[0x7B008]=0xC0000000;
 auto rebuilt=decodeReadout([&](uint32_t r,uint32_t &v){v=f.io.read(r);return true;},38400);
 assert(rebuilt.matches(f.p) && rebuilt.afeKHz==811529);
}
int main(int argc,char **argv){
 for(unsigned fault=0;fault<7;++fault){Fixture f;
  assert(!Restart::retainedStopped(f.io,f.source,f.allocation,1,f.state));
  assert(f.stop());const auto count=f.io.writes.size();const auto state=f.state;
  if(fault==1)f.allocation.external[0]=2;
  if(fault==2)f.io.regs[0x46014]=0xCC000000;
  if(fault==3)f.io.regs[0x6B400]|=1U<<31;
  if(fault==4)f.io.regs[0x7B008]=0xC0000000;
  if(fault==5)f.allocation.binding[1]=2;
  if(fault==6)f.io.owned=false;
  assert(Restart::retainedStopped(f.io,f.source,f.allocation,1,f.state)==!fault);
  assert(f.io.writes.size()==count && !memcmp(&state,&f.state,sizeof(state)));
  if(!fault){XELPComboClock::ClockSnapshot snapshot;snapshot.layout=XELPComboClock::Layout::Display13;
   snapshot.mux[0]=f.io.regs[0x164280]|(1U<<11);snapshot.enable[1]=0;
   const auto choice=f.allocation.choose(XELPComboClock::Port(1),snapshot,true);
   assert(choice.valid && choice.pll==0); // powered-off DSI PLL1 remains reserved
   assert(f.start());
  }
 }
 for(unsigned clock:{350000U,450000U,750000U}) {
  Fixture f;assert(f.stop());f.p.mode.clockKHz=clock;
  f.state.config=Restart::plan(f.p,38400);assert(f.state.config.valid && f.start());
  assert(f.io.regs[0x6B090]==f.state.config.link.escapeDivider);
  f.io.regs[0x7B008]=0xC0000000;
  auto link=decodeReadout([&](uint32_t r,uint32_t &v){v=f.io.read(r);return true;},38400);
  assert(link.matches(f.p)); // program/read back, not just calculate a PLL
 }
 {Fixture f;successful(f);}
 for(unsigned fault=0;fault<4;++fault){Fixture f;retainedCycle(f,fault);}
 {Fixture f;panelCycle(f);}
 {Fixture f;panelCycle(f,true);}
 for(unsigned fault=0;fault<6;++fault){Fixture f;
  if(fault==0)f.io.owned=false;
  if(fault==1)f.allocation.binding[1]=2;
  if(fault==2)f.io.regs[0x7B008]=0xC0000000;
  if(fault==3)f.io.regs[0x6B0D0]=0;
  if(fault==4)f.io.regs[0x68180]=1U<<31;
  if(fault==5)f.io.regs[0x78000]=1U<<31;
  assert(!f.stop() && f.io.writes.empty() && !f.start());
 }
 for(unsigned fault=0;fault<4;++fault){Fixture f;
  if(fault==0)f.io.dropPLLDisable=true;
  if(fault==1)f.io.dropPLLPower=true;
  if(fault==2)f.io.dropBuffer=true;
  if(fault==3)f.io.drop=0x6B400;
  assert(!f.stop() && f.state.error!=Restart::Error::None && !f.start());
  const auto count=f.io.writes.size();assert(!f.stop() && f.io.writes.size()==count);
  assert(f.allocation.external[0]==1 && f.allocation.binding[0]==2 && f.io.elapsed<=1020);
 }
 for(unsigned fault=0;fault<6;++fault){Fixture f;assert(f.stop());
  if(fault==0)f.io.dropPLLPower=true;
  if(fault==1)f.io.dropPLLLock=true;
  if(fault==2)f.io.dropBuffer=true;
  if(fault==3)f.io.dropLink=true;
  if(fault==4)f.io.drop=0x162694;
  if(fault==5)f.allocation.binding[1]=2;
  assert(!f.start() && f.state.error!=Restart::Error::None);
  const auto count=f.io.writes.size();assert(!f.start() && f.io.writes.size()==count);
  assert(f.allocation.external[0]==1 && f.allocation.binding[0]==2 && !f.io.regs[0x7B008]);
  assert(f.io.elapsed<5000);
 }
 if(argc==2){
  {Fixture f;std::ifstream in(argv[1],std::ios::binary);assert(in);f.vbt=Bytes{std::istreambuf_iterator<char>(in),{}};
   f.p=parseProfile({f.vbt.data(),f.vbt.size()},true);retainedCycle(f);}
  for(bool fail:{false,true}){Fixture f;std::ifstream in(argv[1],std::ios::binary);assert(in);
   f.vbt=Bytes{std::istreambuf_iterator<char>(in),{}};f.p=parseProfile({f.vbt.data(),f.vbt.size()},true);
   panelCycle(f,fail);
   assert(f.io.gpioTrace==std::vector<uint32_t>(fail?std::vector<uint32_t>{4,0,2,3,1,0,1}:std::vector<uint32_t>{4,0,2,3,1,0,1,5}));
  }
  Fixture f;std::ifstream in(argv[1],std::ios::binary);assert(in);f.vbt=Bytes{std::istreambuf_iterator<char>(in),{}};
  f.p=parseProfile({f.vbt.data(),f.vbt.size()},true);assert(f.p.valid());successful(f);
  printf("saved VBT restart plan: init=%u timeout=%u/%u/%u function=%08x cfg0=%08x writes=%u pollUs=%u; simulation only\n",
   f.state.config.init,f.state.config.hsTimeout,f.state.config.lpTimeout,f.state.config.taTimeout,
   f.state.config.function,f.state.config.pll.cfg0(),f.state.writes,f.state.waitUs);
 }
 puts("PASS DSI host restart: exclusive ownership, off/on sequence, profile reconstruction, peer preservation, retained resources on bounded failure");
}
'''.replace('IO_FIXTURE',io).replace('PROFILE_FIXTURE',fixture)
with tempfile.TemporaryDirectory(prefix='dsi-restart-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),*sys.argv[1:]],check=True)
