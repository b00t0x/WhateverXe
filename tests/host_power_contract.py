#!/usr/bin/env python3
"""Replay production host matchers against supplied extracted host functions.

Inputs: directories containing <kext>-<mangled symbol>.bin and
<kext>-symbols.json (symbol -> virtual address), plus Lilu's hde64.c.
No Apple binaries or machine paths are embedded in this test.
"""
import argparse, json, re, subprocess, tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host',type=Path,action='append',required=True)
p.add_argument('--hde-source',type=Path,required=True)
a=p.parse_args()
r=Path(__file__).resolve().parents[1]
source=(r/'WhateverXe/kern_host.cpp').read_text()
decoder=re.search(r'static size_t hostInstructionSize\(.*?\n\}',source,re.S)[0]
decoder=decoder.replace('Disassembler::hde_t','hde64s').replace('Disassembler::hdeDisasm(reinterpret_cast<uintptr_t>(code),','hde64_disasm(code,')
code=r'''
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>
#include <cstdio>
#include "hde64.h"
#include "kern_host_power_contract.hpp"
DECODER
using namespace HostPowerContract;
std::vector<uint8_t> read(const char *path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
 assert(argc==7);auto event=read(argv[1]),sleep=read(argv[2]),mode=read(argv[3]),wsaa=read(argv[4]);
 const auto address=strtoull(argv[5],nullptr,0),count=strtoull(argv[6],nullptr,0);
 auto decode=hostInstructionSize;
 auto ne=extent(event.data(),event.size(),decode),ns=extent(sleep.data(),sleep.size(),decode),nm=extent(mode.data(),mode.size(),decode);
 assert(ne && ns && nm && extent(wsaa.data(),wsaa.size(),decode));
 assert(paging(event.data(),ne,decode));
 const auto site=sleepSite(sleep.data(),ns,address,count,decode);assert(site.valid);
 assert(sleepFields(sleep.data(),ns,mode.data(),nm,decode,site.owner));
 assert(!sleepSite(sleep.data(),ns,address,count+1,decode).valid);
 auto shifted=sleep;shifted.insert(shifted.begin(),17,0x90);
 const auto moved=sleepSite(shifted.data(),ns+17,address-17,count,decode);
 assert(moved.valid && moved.offset==site.offset+17 && moved.owner==site.owner);
 const auto relocated=sleepSite(sleep.data(),ns,address+0x123400,count+0x123400,decode);
 assert(relocated.valid && relocated.offset==site.offset);
 // Register allocation of the loop flag is independent of accelerator owner.
 for(unsigned flag=12;flag<=15;++flag)if(flag!=site.owner){
  auto changed=sleep;assert(changed[site.offset+site.length]==0x41);
  changed[site.offset+site.length+1]=0xB0+(flag-8);
  auto alternative=sleepSite(changed.data(),ns,address,count,decode);
  assert(alternative.valid && alternative.success==flag && alternative.owner==site.owner);
 }
 auto wrong=sleep;wrong[site.offset+3]^=8;
 assert(!sleepSite(wrong.data(),ns,address,count,decode).valid);
 for(size_t n=0;n<site.offset+site.length;++n)assert(!sleepSite(sleep.data(),n,address,count,decode).valid);
 event.insert(event.begin(),17,0x90);assert(paging(event.data(),ne+17,decode));
 for(size_t i=0;i+8<=ne+17;++i)if(event[i]==0x41 && event[i+1]==0x80 && event[i+2]==0x8D && event[i+3]==0x99){
  auto changed=event;changed[i+3]=0x98;assert(!paging(changed.data(),ne+17,decode));
 }
 printf("host operands/call roles, independent flag, relocation and ABI rejection: owner=%u flag=%u site=%zx\n",site.owner,site.success,site.offset);
}
'''.replace('DECODER',decoder)
with tempfile.TemporaryDirectory(prefix='xe-host-contract-') as t:
 d=Path(t);(d/'test.cpp').write_text(code)
 subprocess.run(['clang','-I'+str(a.hde_source.parent),'-c',str(a.hde_source),'-o',str(d/'hde.o')],check=True)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(a.hde_source.parent),'-I'+str(r/'WhateverXe'),str(d/'test.cpp'),str(d/'hde.o'),'-o',str(d/'test')],check=True)
 for host in a.host:
  sleep='__ZN22IOGraphicsAccelerator217system_will_sleepEib'
  count='__ZNK21IOAccelDisplayMachine19getFramebufferCountEv'
  symbols=json.loads((host/'IOAcceleratorFamily2-symbols.json').read_text())
  files=[('IOGraphicsFamily','__ZN13IOFramebuffer11handleEventEiPv'),('IOAcceleratorFamily2',sleep),('IOAcceleratorFamily2','__ZN21IOAccelDisplayMachine24display_mode_will_changeEj'),('IOGraphicsFamily','__ZN13IOFramebuffer16setWSAAAttributeEjj')]
  subprocess.run([str(d/'test'),*[str(host/(k+'-'+s+'.bin')) for k,s in files],str(symbols[sleep]),str(symbols[count])],check=True)
