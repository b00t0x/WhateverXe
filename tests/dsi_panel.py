#!/usr/bin/env python3
"""Panel cycle ordering, GPIO side effects and bounded failure; optional saved VBT."""
from pathlib import Path
import subprocess, tempfile, sys
root=Path(__file__).resolve().parents[1]
boot=(root/'tests/dsi_boot.cpp').read_text()
io=boot[boot.index('struct IO {'):boot.index('Boot capture(')]
profile=(root/'tests/dsi_profile.cpp').read_text()
fixture=profile[profile.index('using Bytes='):profile.index('int main(')]
code=r'''
#include "kern_dsi_panel.hpp"
#include "kern_dsi_clock.hpp"
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
 uint32_t expectedDuty=UINT32_MAX;
 bool owned=true,freezeFrame=false,stuckStop=false,stuckStart=false,stuckCommand=false;
 uint32_t drop=UINT32_MAX,elapsed=0,headers=0,payload=0,videoCommandDelay=0,initCommandDelay=0,creditRelease=0;unsigned irqDepth=0;
 std::vector<uint32_t> packets,gpioTrace;
 bool ready(){return owned && (regs[0x45404]&0x403)==0x403;}
 uint32_t read(uint32_t reg){
  if(reg==0x6B0D0 && stuckCommand && headers)return 0;
  if(reg==0x6B0D0 && elapsed<creditRelease)return 0xF40;
  return IO::read(reg);
 }
 void write(uint32_t reg,uint32_t value){
  assert(ready());writes.push_back(reg);
  if(reg==drop)return;
  if(reg==0xC4030){assert(irqDepth);regs[reg]=(value&~0x3333U)|(regs[reg]&0x3333U&~value);return;}
  if(reg==0xC7204 || reg==0xC7304){assert(irqDepth);regs[reg]=value;return;}
  assert(!irqDepth);
  if(reg==0x6B100){++headers;packets.push_back(value);
   if(regs[0x7B008]&(1U<<31))creditRelease=elapsed+videoCommandDelay;
   else if((value&0x3f)==0x39)creditRelease=elapsed+initCommandDelay;
   return;}
  if(reg==0x6B104){++payload;return;}
  if(reg==0x7B008){regs[reg]=(value&~(1U<<30))|(regs[reg]&(1U<<30));return;}
  if(reg==0x6B400 && !(value&(1U<<31)))regs[0x6B0D8]=0;
  regs[reg]=value;
 }
 void delayUs(unsigned us){
  assert(!irqDepth);elapsed+=us;
  if(regs[0x6B0D8]&1)regs[0x6B0D8]=(1U<<16);
  if(!(regs[0x64000]&(1U<<31)))regs[0x64000]|=0x80;
  if(!(regs[0x46014]&(1U<<31)))regs[0x46014]&=~(1U<<30);
  if(!(regs[0x46014]&(1U<<27)))regs[0x46014]&=~(1U<<26);
  auto &conf=regs[0x7B008];
  if(!(conf&(1U<<31)) && !stuckStop)conf&=~(1U<<30);
  if((conf&(1U<<31)) && !stuckStart)conf|=1U<<30;
  if(!freezeFrame && us>=1000 && (conf&0xC0000000U)==0xC0000000U){++regs[0x70040];regs[0x701AC]=regs[0x7019C];}
 }
 bool gpio(unsigned role,unsigned value){
  if(role==2 && value && expectedDuty!=UINT32_MAX)assert(regs[Backlight::Duty]==expectedDuty);
  ++irqDepth;const auto r=Panel::gpio(*this,role,value);--irqDepth;
  if(r.complete)gpioTrace.push_back(role*2+value);return r.complete;
 }
 Sim(){regs[0x42084]=1U<<30;regs[0x6B0D0]=0x1040;regs[0x6001C]=0x0437077F;
  regs[0x6B010]=regs[0x6B00C];regs[0xC4030]=0xA7;regs[0xC7204]=0xABCD0015;
  regs[0x71180]=0x94000000;regs[0x64100]=0x80000000;}
};
struct Fixture {
 Sim io;Bytes vbt;Profile p;Readout link;Panel::Result result;
 std::vector<Bytes> seq=std::vector<Bytes>(12);
 Fixture(){vbt=fixture();p=parse(vbt);
  seq[1]={3,3,0,0,1,0};seq[2]={1,9,0,0x39,5,0,0xB0,1,2,3,4,1,5,0,5,1,0,0x11,0};
  seq[4]={1,5,0,5,1,0,0x10,1,5,0,5,1,0,0x28,0};seq[5]={3,3,0,0,0,0};
  seq[6]={3,3,0,2,1,0};seq[7]={3,3,0,2,0,0};seq[10]={3,3,0,1,1,0};seq[11]={3,3,0,1,0,0};
  for(unsigned id:{1U,2U,4U,5U,6U,7U,10U,11U})p.sequences.sequence[id]={seq[id].data(),seq[id].size()};
  link=decodeReadout([&](uint32_t r,uint32_t &v){v=io.read(r);return true;},38400);assert(link.matches(p));
 }
 void run(){Panel::cycle(io,link,p,0x94000008,0,result);}
};
void stop(Fixture &f,bool success=true) {
 Panel::Shutdown panel;
 const auto host=Quiesce::run(f.io,f.link,f.p,panel);
 if(!success){assert(!host.complete() && !panel.complete);return;}
 assert(host.complete() && panel.complete && !panel.host.pending && !panel.host.faulted);
 assert(f.io.gpioTrace==std::vector<uint32_t>({4,0,2}));
 assert(!(f.io.regs[0x70180]&(1U<<31)) && !f.io.regs[0x7B008] && !f.io.regs[0x6B0D8]);
 XELPComboClock::ClockAllocation allocation;allocation.external[0]=123;allocation.binding[0]=2;
 assert(Clock::retire(f.io,f.link,host,true,allocation,123).complete());
 assert(!allocation.external[0] && !allocation.binding[0] && !f.io.regs[0x46014]);
 assert(f.io.regs[0x71180]==0x94000000 && f.io.regs[0x64100]==0x80000000);
}
int main(int argc,char **argv){
 for(bool stalled:{false,true}){
  Fixture f;f.io.initCommandDelay=stalled?200000:1500;
  f.run();assert(f.result.complete()==!stalled);
  if(stalled)assert(f.result.stage==Panel::Stage::Init && f.result.host.error==HostError::CreditTimeout);
 }
 for(bool stalled:{false,true}) {
  Fixture f;f.io.videoCommandDelay=stalled?200000:1000;
  f.seq[3]={1,5,0,5,1,0,0x11,2,4,0xA0,0x86,1,0,1,5,0,5,1,0,0x29,2,4,0xA0,0x86,1,0,0};
  f.p.sequences.sequence[3]={f.seq[3].data(),f.seq[3].size()};
  f.run();assert(f.result.complete()==!stalled);
  if(stalled)assert(f.result.stage==Panel::Stage::TranscoderOn &&
    f.result.host.error==HostError::CreditTimeout && f.io.gpioTrace.back()!=5);
  else assert(!f.result.host.pending && f.result.host.sequenceId==6);
 }
 for(unsigned role:{0U,1U,2U,5U,6U,7U}) {
  Fixture f;f.io.regs[0xC7304]=0xABCD0015;
  const auto before=f.io.regs;assert(f.io.gpio(role,0));assert(f.io.gpio(role,1));
  const auto reg=role%5==0?0xC4030U:0xC7204U+0x100*(role/5);
  const auto mask=role%5==0?12U<<(4*(role/5)):role%5==1?1U:4U;
  assert((f.io.regs[reg]&~mask)==(before.at(reg)&~mask));
  for(auto item:before)if(item.first!=reg)assert(f.io.regs[item.first]==item.second);
 }
 {Fixture f;f.p.sequences.sequence[1]={};f.p.sequences.sequence[5]={};
  f.seq[6][3]=7;f.seq[7][3]=7;f.io.regs[0xC7304]=4;
  f.run();assert(f.result.complete());
  assert(f.io.gpioTrace==std::vector<uint32_t>({14,2,3,15}));
  assert(f.io.regs[0xC7304]==4 && f.io.regs[0xC4030]==0xA7);
 }
 // Every lifecycle slot can be absent, including a panel with all operations
 // folded outside these slots. Hardware stop/start and frame checks still run.
 for(unsigned mask=0;mask<512;++mask) {
  Fixture f;const unsigned ids[]={1,2,3,4,5,6,7,10,11};
  for(unsigned i=0;i<9;++i)if(mask&(1U<<i))f.p.sequences.sequence[ids[i]]={};
  f.run();assert(f.result.complete() && !f.result.host.pending);
  Panel::Shutdown panel;auto host=Quiesce::run(f.io,f.link,f.p,panel);
  assert(host.complete() && panel.complete && !panel.host.pending);
 }
 for(unsigned id:{1U,2U,3U,4U,5U,6U,7U,10U,11U}) {
  Fixture f;f.p.sequences.sequence[id]={nullptr,1};f.run();assert(!f.result.complete() && f.io.writes.empty());
 }
 {Fixture f;stop(f);}
 for(bool drop:{false,true}){Fixture f;Backlight light;Panel::RetainedHost host;
  f.io.regs[Backlight::Control]=0x80000000;f.io.regs[Backlight::Period]=480;f.io.regs[Backlight::Duty]=154;
  assert(light.capture(f.p,0x80000000,480,154));assert(light.request(32768));
  f.io.expectedDuty=240;if(drop)f.io.drop=Backlight::Duty;
  Panel::cycle(f.io,f.link,f.p,0x94000008,0,f.result,host,nullptr,&light);
  assert(f.result.complete()==!drop);
  if(drop)assert(light.faulted && f.io.gpioTrace.back()!=5);
 }
 {Fixture f;f.io.stuckCommand=true;stop(f,false);assert(f.io.gpioTrace==std::vector<uint32_t>({4}));}
 {Fixture f;f.p.sequences.sequence[11]=f.p.sequences.sequence[4];stop(f,false);assert(f.io.writes.empty());}

 {Fixture f;f.run();assert(f.result.complete());
  assert(f.io.gpioTrace==std::vector<uint32_t>({4,0,2,3,1,5}));
  assert(f.io.packets==std::vector<uint32_t>({0x40001005,0x40002805,0x40000005,0xC0000539,0x40001105,0x40000005}));
  assert(f.io.payload==2 && f.io.regs[0xC4030]==0xA7 && f.io.regs[0xC7204]==0xABCD0015);
  assert(f.io.regs[0x70180]==0x94000008 && f.io.regs[0x7B008]==0xC0000000);
  assert(f.io.regs[0x71180]==0x94000000 && f.io.regs[0x64100]==0x80000000 && f.io.regs[0x46014]==0xCC000000);
  const auto writes=f.io.writes.size();f.run();assert(f.io.writes.size()==writes);
  for(auto reg:f.io.writes)assert(reg==0xC4030 || reg==0xC7204 || reg==0x70180 || reg==0x7019C || reg==0x7B008 || reg==0x6B100 || reg==0x6B104);
 }
 {Fixture f;f.p.sequences.sequence[2].size-=1;f.run();assert(!f.result.complete() && f.io.writes.empty());}
 {Fixture f;f.io.owned=false;f.run();assert(!f.result.complete() && f.io.writes.empty());}
 for(unsigned fault=0;fault<6;++fault){Fixture f;
  if(fault==0)f.io.freezeFrame=true;
  if(fault==1)f.io.stuckStop=true;
  if(fault==2)f.io.stuckCommand=true;
  if(fault==3)f.io.drop=0xC4030;
  if(fault==4)f.io.drop=0xC7204;
  if(fault==5)f.io.stuckStart=true;
  f.run();assert(!f.result.complete());
  assert(f.io.elapsed<1000000); // bounded failure, no repeated power cycles
  assert(f.io.regs[0x46014]==0xCC000000); // retain ownership/clock on failure
  const auto writes=f.io.writes.size();f.run();assert(f.io.writes.size()==writes);
  if(fault<3)assert(f.io.gpioTrace==std::vector<uint32_t>{4}); // no reset after stop/drain failure
  if(fault==5)assert(f.result.stage==Panel::Stage::TranscoderOn && !(f.io.regs[0x70180]&(1U<<31)));
 }
 if(argc==2){Fixture f;std::ifstream in(argv[1],std::ios::binary);assert(in);
  f.vbt=Bytes{std::istreambuf_iterator<char>(in),{}};f.p=parseProfile({f.vbt.data(),f.vbt.size()},true);assert(f.p.valid());
  {Fixture stopped;stopped.vbt=f.vbt;stopped.p=parseProfile({stopped.vbt.data(),stopped.vbt.size()},true);stop(stopped);}
  f.run();assert(f.result.complete() && f.result.host.completedOperations==f.p.sequences.packets+f.p.sequences.delays+f.p.sequences.gpios);
  assert(f.io.gpioTrace==std::vector<uint32_t>({4,0,2,3,1,0,1,5}));
  printf("saved VBT replay: packets=%u payloadWords=%u operations=%u gpio=%zu delayUs=%llu; simulation only\n",f.result.host.headers,f.result.host.payloadWords,f.result.host.completedOperations,f.io.gpioTrace.size(),static_cast<unsigned long long>(f.result.host.waitedUs));
 }
 puts("PASS panel cycle: packet/GPIO order, retained host, restart after drain, HPD W1C preservation, bounded failures and no retry");
}
'''.replace('IO_FIXTURE',io).replace('PROFILE_FIXTURE',fixture)
with tempfile.TemporaryDirectory(prefix='dsi-panel-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),*sys.argv[1:]],check=True)
