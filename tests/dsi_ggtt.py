#!/usr/bin/env python3
"""Exercise shared GGTT preparation against the actual GOP certificate."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'tests/dsi_boot.cpp').read_text()
io=s[s.index('struct IO {'):s.index('Boot capture(')]
code=r'''#include "kern_dsi_ggtt.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace XELP::DSI;
IO_FIXTURE
struct Probe:IO {
 bool loseWrite=false,sourceDrift=false;
 void write(uint32_t reg,uint32_t value){assert(reg==0x42084);writes.push_back(reg);
  if(!loseWrite)regs[reg]=value;if(sourceDrift)regs[0x6B000]^=1;}
};
Boot certify(Probe &io){auto boot=captureBoot(io,AlderLakeN::readDisplay(io,true),profile(),io.memory,true);
 assert(verifyBootPrefix(io,boot,io.memory));return boot;}
int main(){
 for(bool already:{false,true}){
  Probe io;io.regs[0x42084]=0x20|(already?(1U<<30):0);auto b=certify(io);auto before=io.regs;GGTTMode mode;
  assert(prepareGGTT(io,b,io.memory,mode) && mode.ready() && b.complete());
  assert(mode.before==before[0x42084] && mode.after==((1U<<30)|0x20) && mode.writes==!already);
  assert(io.writes.size()==!already && bootUnchanged(io,b,io.memory));
  before[0x42084]|=1U<<30;assert(io.regs==before);
  assert(!prepareGGTT(io,b,io.memory,mode) && mode.ready() && io.writes.size()==!already);
 }
 for(auto reg:{0x6B000U,0x70180U,0x7019CU,0x71008U,0x70B08U,0x42084U}){
  Probe io;auto b=certify(io);GGTTMode mode;io.regs[reg]^=reg==0x71008?0x80000000U:1;
  assert(!prepareGGTT(io,b,io.memory,mode) && !mode.ready() && io.writes.empty());
 }
 for(bool lost:{false,true}){
  Probe io;auto b=certify(io);GGTTMode mode;io.loseWrite=lost;io.sourceDrift=!lost;
  assert(!prepareGGTT(io,b,io.memory,mode) && !mode.ready() && !b.usable());
  assert(mode.writes==1 && io.writes==std::vector<uint32_t>{0x42084});
  assert(mode.status==(lost?GGTTMode::Status::Readback:GGTTMode::Status::Changed));
  assert(!prepareGGTT(io,b,io.memory,mode) && mode.writes==1);
 }
 puts("PASS shared DSI GGTT mode: linear GOP preserved, exact one-bit write, already configured, source/peer exclusion, readback/drift failure and no retry");
}
'''.replace('IO_FIXTURE',io)
with tempfile.TemporaryDirectory(prefix='dsi-ggtt-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
