#!/usr/bin/env python3
"""Exercise the DSI scanout transaction with real WM and session cores."""
from pathlib import Path
import hashlib,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'tests/dsi_boot.cpp').read_text()
io=s[s.index('struct IO {'):s.index('Boot capture(')]
code=r'''
#include "kern_dsi_boot.hpp"
#include "kern_dsi_surface.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
using namespace XELP::DSI;
IO_FIXTURE
struct Sim:IO {
 uint64_t elapsed=0,clock=614400000;uint32_t drop=UINT32_MAX,drift=0;
 bool frozen=false,oldLive=false,backward=false,unbound=false,loseBinding=false,immediate=false;
 bool ready(){return !unbound && !(loseBinding && !writes.empty());}
 uint64_t nativeClockHz(){return clock;}uint64_t nowUs(){return elapsed;}
 void write(uint32_t r,uint32_t v){writes.push_back(r);if(r!=drop)regs[r]=v;if(r==0x7019C && immediate){++regs[0x70040];regs[0x701AC]=v;}}
 void delayUs(unsigned us){elapsed+=us;if(elapsed==2000){
  if(!frozen)regs[0x70040]=backward?99:regs[0x70040]+1;
  if(!oldLive)regs[0x701AC]=regs[0x7019C];
  if(drift)regs[drift]^=drift==0x42084 ? 1U<<30 : 1;
 }}
};
struct Fixture {
 Sim io;Profile p=profile();Readout link;Session session;
 Surface::Plane current{0x94000008,0x5000,68,0x077F0437,0,0,0};
 Surface::Plane next{0x94001008,0x7000,34,0x077F0437,0x10000000,0,0};
 Surface::Backing backing{201,0x10000000,4352*1920,4352};
 Display13WM::Plan plan;Display13WM::Snapshot snapshot;
 Fixture(){
  link=decodeReadout([&](uint32_t r,uint32_t &v){v=io.read(r);return true;},38400);assert(link.matches(p));
  Capabilities caps;caps.installed=Capabilities::Adoption;assert(session.bind(caps,link,p,1,100,200));
  plan=Display13WM::plan(1080,1126,135255000,2048,Display13WM::decodeLatency(0x63503300,0x00909090,30),true,true,1920);
  assert(plan.status==Display13WM::Status::Complete);
  snapshot.primary=&plan;snapshot.primaryDDB=0x07FF0000;snapshot.primarySurface=next.surface;
  snapshot.nativeClockHz=io.clock;snapshot.pipeLineTime=Display13WM::pipeLineTime(1126,135255000);
  snapshot.pipePixelHz=135255000;snapshot.transcoders.dsi0Pipe=0;
  io.regs[0x42084]=1U<<30;io.regs[0x6001C]=0x0437077F;io.regs[0x6B010]=link.vtotal;
  io.regs[0x7027C]=snapshot.primaryDDB;io.regs[0x70040]=100;
  uint32_t values[7];Surface::values(current,values);
  for(unsigned i=0;i<7;++i)io.regs[Surface::Registers[i]]=values[i];
 }
 Surface::Result run(){return Surface::change(io,session,link,p,current,next,backing,snapshot);}
};
template<class T>void put(void *p,unsigned offset,T v){memcpy(static_cast<uint8_t*>(p)+offset,&v,sizeof(v));}
template<class T>T get(const void *p,unsigned offset){T v;memcpy(&v,static_cast<const uint8_t*>(p)+offset,sizeof(v));return v;}
void native(const char *path){
 std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});assert(!bytes.empty());
 constexpr size_t size=0x2000000;assert(bytes.size()<size);
 auto image=static_cast<uint8_t*>(mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(image!=MAP_FAILED);memcpy(image,bytes.data(),bytes.size());
 for(bool y:{false,true}){
  Fixture f;alignas(8) uint8_t fb[0x4B90]{},controller[0x1C20]{},plane[0x1C8]{},display[0x3730]{};
  put(plane,0x68,fb);put(fb,0x1D0,controller);put(fb,0x4330,f.next.surface);put(fb,0x4334,uint32_t(f.backing.pitch));
  put(fb,0x4A18,uint8_t(y));put(plane,0x100,f.current.control);put(plane,0x104,f.current.color);
  put(plane,0x118,f.current.stride);put(display,0xF8,uint32_t(f.p.mode.width));put(display,0xFC,uint32_t(f.p.mode.height));
  reinterpret_cast<void(*)(void*,void*)>(image+0x1F50)(plane,display);
  f.next={get<uint32_t>(plane,0x100),get<uint32_t>(plane,0x104),get<uint32_t>(plane,0x118),
   get<uint32_t>(plane,0x11C),get<uint32_t>(plane,0x120),0,get<uint32_t>(plane,0x110)};
  assert(((f.next.control>>10)&7)==(y?4U:1U));
  if(y)assert(f.run().status==Surface::Status::Complete);
  else assert(!Surface::target(f.next,f.p,f.backing)); // X is outside the shared WM contract
 }
 assert(!munmap(image,size));puts("PASS exact donor setupPlane cache feeds the DSI Y surface transaction; X rejected before MMIO");
}
int main(int argc,char **argv){
 {Fixture f;f.current.size=f.io.regs[0x70190]=0x04370437;
  assert(f.run().status==Surface::Status::Input);
  const auto result=Surface::change(f.io,f.session,f.link,f.p,f.current,f.next,f.backing,f.snapshot,true);
  assert(result.status==Surface::Status::Complete && f.io.regs[0x70190]==0x077F0437);
 }

 {Fixture f;const auto result=f.run();assert(result.status==Surface::Status::Complete && result.writes==17);
  assert(result.latch.status==Display13WM::Status::Complete && result.latch.frameBefore==100 && result.latch.frameAfter==101);
  assert(f.session.state==SessionState::Streaming && f.session.currentBacking==201 && f.session.retiredBacking==200 && f.session.lease==100);
  assert(f.io.writes.back()==0x7019C && f.io.writes[f.io.writes.size()-2]==0x70180);
  for(auto r:f.io.writes)assert((r>=0x70240 && r<=0x70268) || r==0x45270 ||
   r==0x70188 || r==0x7018C || r==0x70190 || r==0x701A4 || r==0x701CC || r==0x70180 || r==0x7019C);
  assert(sourceUnchanged(f.io,f.link) && f.io.regs[0x70008]==0 && f.io.regs[0x42084]==(1U<<30));
 }
 for(unsigned problem=0;problem<19;++problem){Fixture f;
  switch(problem){
   case 0:f.backing.pitch+=128;break;case 1:f.backing.bytes-=4096;break;case 2:f.next.control|=1U<<15;break;
   case 3:f.next.control^=0x1400;break;case 4:f.next.offset=1;break;case 5:f.next.surface+=4096;break;
   case 6:f.plan.yTiled=false;break;case 7:f.snapshot.primarySurface=0;break;case 8:f.snapshot.primaryDDB=0;break;
   case 9:f.io.regs[0x42084]=0;break;case 10:f.io.regs[0x70080]=7;break;case 11:f.io.regs[0x70780]=1U<<31;break;
   case 12:f.io.regs[0x70B08]=1;break;case 13:f.io.regs[0x6001C]^=1;break;case 14:f.io.regs[0x701AC]=4096;break;
   case 15:f.io.unbound=true;break;case 16:f.p.mode.height=0;break;
   case 17:f.p.mode.height=40000;break;case 18:f.snapshot.nativeClockHz+=1;break;
  }
  assert(f.run().status!=Surface::Status::Complete && f.io.writes.empty() && f.session.state==SessionState::FirmwareActive);
 }
 for(auto drop:{0x70240U,0x45270U,0x70188U,0x701CCU,0x70180U,0x7019CU}){
  Fixture f;f.io.drop=drop;const auto r=f.run();assert(r.status!=Surface::Status::Complete && !f.io.writes.empty());
  assert(f.session.state==SessionState::Faulted && f.session.currentBacking==200 && f.session.pendingBacking==201 && !f.session.takeCleanup().lease);
 }
 for(unsigned failure=0;failure<7;++failure){Fixture f;
  switch(failure){case 0:f.io.frozen=true;break;case 1:f.io.oldLive=true;break;case 2:f.io.backward=true;break;
   case 3:f.io.drift=0x6B008;break;case 4:f.io.loseBinding=true;break;case 5:f.io.drift=0x42084;break;case 6:f.io.immediate=f.io.frozen=true;break;}
  const auto r=f.run();assert(r.status!=Surface::Status::Complete && f.session.state==SessionState::Faulted);
  assert(f.session.currentBacking==200 && f.session.pendingBacking==201 && !f.session.takeRetiredBacking());
 }
 if(argc==2)native(argv[1]);
 puts("PASS DSI surface: fixed native Y geometry, real WM programming/latch, link preservation, live/frame proof, failed writes/latch retain both backings");
}
'''.replace('IO_FIXTURE',io)
donor=None
if len(sys.argv)==2:
 donor=Path(sys.argv[1]).resolve()
 assert hashlib.sha256(donor.read_bytes()).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
with tempfile.TemporaryDirectory(prefix='dsi-surface-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1',*(['-arch','x86_64'] if donor else ['-fsanitize=address,undefined']),'-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),*([str(donor)] if donor else [])],check=True)
