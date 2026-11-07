#!/usr/bin/env python3
"""Exercise the production allocation adapter with real DBUF ownership."""
from pathlib import Path
from production_source import read_source
import re, subprocess, tempfile
r = Path(__file__).resolve().parents[1]
s = read_source(r/'WhateverXe/kern_xe.cpp')
guard = re.search(r'bool XE::dsiSharedWriterStopped\(.*?\n\}', s, re.S).group()
body = re.search(r'void XE::adlnSetupDBUFPipe\(.*?\n\}', s, re.S).group()
pipewm = re.search(r'void XE::wrapAlderLakeNPipeWMSetup\(.*?\n\}', s, re.S).group()
code = (r / 'tests/display13_dbuf_setup_fixture.cpp').read_text().replace('// PRODUCTION_SETUP', guard+'\n'+body+'\n'+pipewm)
with tempfile.TemporaryDirectory(prefix='dbuf-setup-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-I' + str(r / 'WhateverXe'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)

# The ownership fixture above emulates hardware power acknowledgements. Also
# exercise the real MMIO writer: a permissive fake must not hide an adapter
# which silently drops a newly required register write.
adapter = re.search(r'struct Display13FBDBUFIO \{.*?\n\};', s, re.S).group()
code = r'''
#include "kern_display13_dbuf.hpp"
#include <cassert>
#include <vector>
struct IOMemoryMap {
 std::vector<uint32_t> words=std::vector<uint32_t>(0x1000000/4);
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(words.data());}
 unsigned getLength(){return 0x1000000;}
 uint32_t &reg(unsigned r){return words[r/4];}
};
void IODelay(unsigned){}
ADAPTER
int main(){
 using namespace Display13DBUF;
 for(unsigned pipe=0;pipe<3;++pipe){
  IOMemoryMap map;Display13FBDBUFIO io{&map};State s;
  s.begun=true;s.selected=1U<<pipe;s.capacity=7;s.mbus=0x1C000700;
  const auto plan=plannedLayout(7,pipe);
  s.pipe[pipe].primary=plan.primary;s.pipe[pipe].cursor=plan.cursor;
  map.reg(0x45404)=0xFC0F;map.reg(0x4438C)=s.mbus;
  const auto creditReg=0x7003C+0x1000*pipe;
  map.reg(creditReg)=0xB1038C04;
  assert(captureCredits(io,s)==Status::Complete && s.creditSaved==(1U<<pipe));
  s.restorePending=1U<<pipe;
  map.reg(creditReg)=0xD1038804; // Lost writable allocation; preserve unrelated high fields.
  assert(restoreAllocation(io,s,pipe)==Status::Complete && !s.restorePending);
  assert(map.reg(creditReg)==0xD1038C04);
  assert(hardwareMatches(io,s,pipe,plan.primary,plan.cursor));
  for(unsigned other=0;other<3;++other)if(other!=pipe)
   assert(!io.read(0x7027C+other*0x1000) && !io.read(0x7017C+other*0x1000));
  // Timing, plane enables, surfaces and interrupt status are not DBUF writes.
  for(unsigned reg:{0x70008U,0x71180U,0x7219CU,0x44470U}){
   io.write(reg,0xFFFFFFFF);assert(io.read(reg)==0);
  }
  for(auto reg:regs){io.write(reg,0x80010000);assert(io.read(reg)==0x80010000);}
 }
}
'''.replace('ADAPTER', adapter)
with tempfile.TemporaryDirectory(prefix='dbuf-mmio-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-I' + str(r / 'WhateverXe'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
