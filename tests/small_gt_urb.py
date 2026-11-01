#!/usr/bin/env python3
"""Replay production URB patch selection and its native store on CPU, no GPU."""
import hashlib,re,subprocess,sys,tempfile
from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('static const uint8_t urbFind[]');b=s.index('auto gpuInfo=',a)
body=s[a:b]
if len(sys.argv)>1:
    donor=Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(donor).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
    old=bytes(int(x,0) for x in re.search(r'urbFind\[\] \{([^}]+)',body)[1].split(','))
    assert donor[0x28781:0x28781+len(old)]==old
code=r'''#include "kern_xelp_platform.hpp"
#include "kern_xelp_gt_resources.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <initializer_list>
void check(unsigned id,unsigned rev,unsigned dss,unsigned eu,unsigned media) {
 auto xeProfile=XELP::identify((id<<16)|0x8086,rev);
 bool display12SmallGTDetected=XELP::display12SmallGT(xeProfile.gt);
 XELP::SmallGTResources display12SmallGT;
 uint32_t fuses[]={1,dss,eu,0xf0,media};
 assert(xeProfile.admitted() && display12SmallGTDetected && XELP::decodeSmallGT(fuses,display12SmallGT));
 bool tglGT2Detected=false;XELP::GTResources tglGT2Resources;
 bool adlnDetected=false;unsigned adlnNativeSubslices=0,adlnL3Banks=0;
 BODY
 assert(urbReplacement==urbReplaceAlderLakeN);
 auto program=(unsigned char *)mmap(nullptr,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(program!=MAP_FAILED);memset(program,0xcc,4096);
 const unsigned char prefix[]={0x41,0x57,0x49,0x89,0xff}; // save R15, destination from RDI
 const unsigned char done[]={0x41,0x5f,0xc3};
 memcpy(program,prefix,5);memcpy(program+5,urbReplacement,sizeof(urbFind));
 memcpy(program+5+sizeof(urbFind),done,3);memcpy(program+5+94,done,3);
 unsigned char state[0x1200];
 for(unsigned slice:{0U,1U,2U})for(unsigned ss:{0U,2U,4U,6U,10U,12U}) {
  memset(state,0xa5,sizeof(state));
  reinterpret_cast<void(*)(void*,unsigned,unsigned)>(program)(state,slice,ss);
  unsigned result;memcpy(&result,state+0x1164,4);
  assert(result==((slice==1 && ss==display12SmallGT.nativeSubslices)?4U:0xa5a5a5a5U));
  for(unsigned i=0;i<sizeof(state);++i)if(i<0x1164 || i>=0x1168)assert(state[i]==0xa5);
 }
 munmap(program,4096);
}
int main() {
 // Three GT1 identities and 16/32-EU HX identities use fuse-derived topology.
 for(auto id:{0x9a60U,0x9a68U,0x9a70U,0x4688U,0x468bU,0xa788U,0xa78bU})
 for(auto rev:{0U,1U,4U,12U,255U})for(auto video:{0xe00feU,0xe00faU}) {
  check(id,rev,1,0,video); // 16 EU, one DSS, two native subslices
  check(id,rev,3,0xc0,video); // 24 EU, two DSS
  check(id,rev,0x30,0,video); // 32 EU, sparse DSS mask
 }
 XELP::SmallGTResources resource;
 assert(!XELP::decodeSmallGT(nullptr,resource));
 for(auto banks:{0U,0xe0U,0xfcU,0xffU}) {
  uint32_t f[]={1,3,0,banks,0xe00fe};assert(!XELP::decodeSmallGT(f,resource));
 }
 puts("PASS TGL GT1/HX production patch selection, fuse shapes and 3780 native URB stores/rejections");
}'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='small-gt-urb-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
