#!/usr/bin/env python3
"""Replay physical TGL GT2 admission and fuse-derived production URB stores."""
import hashlib,re,subprocess,sys,tempfile
from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('static const uint8_t urbFind[]');b=s.index('auto gpuInfo=',a)
body=s[a:b]
qa=s.index('            const bool readoutReady=')
qb=s.index('            if(tglGT2Detected)',qa)
qualification=s[qa:qb]
if len(sys.argv)>1:
    donor=Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(donor).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
    old=bytes(int(x,0) for x in re.search(r'urbFind\[\] \{([^}]+)',body)[1].split(','))
    assert donor[0x28781:0x28781+len(old)]==old
code=r'''#include "kern_xelp_platform.hpp"
#include "kern_xelp_gt_resources.hpp"
#include "kern_alderlake_n.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <initializer_list>
void check(unsigned id,unsigned rev,unsigned dss,unsigned banks,unsigned media) {
 auto xeProfile=XELP::identify((id<<16)|0x8086,rev);
 bool display12SmallGTDetected=false;
 XELP::SmallGTResources display12SmallGT;
 uint32_t fuses[]={1,dss,0,banks,media};
 assert(xeProfile.admitted() && XELP::tigerLakeGT2Device(xeProfile.identity));
 bool tglGT2Detected=true;XELP::GTResources tglGT2Resources;
 assert(XELP::decodeTGLGT2(fuses,tglGT2Resources));
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
  assert(result==((slice==1 && ss==tglGT2Resources.nativeSubslices)?tglGT2Resources.l3Banks:0xa5a5a5a5U));
  for(unsigned i=0;i<sizeof(state);++i)if(i<0x1164 || i>=0x1168)assert(state[i]==0xa5);
 }
 munmap(program,4096);
}
int main() {
 for(auto status:{AlderLakeN::GTStatus::Complete,AlderLakeN::GTStatus::ProfileMismatch,AlderLakeN::GTStatus::Unavailable,AlderLakeN::GTStatus::ExistingOwner,AlderLakeN::GTStatus::AckTimeout})
 for(unsigned mask=0;mask<8;++mask)for(unsigned release=0;release<8;++release)for(unsigned failed=0;failed<4;++failed) {
  AlderLakeN::GTReadout gt;gt.status=status;gt.requestedMask=mask;gt.releasedMask=release;gt.releaseFailedMask=failed;
  const uint32_t fuses[]={1,63,0,0,0xe00fa};memcpy(gt.fuses,fuses,sizeof(fuses));
  uint32_t edram=0;bool display12SmallGTDetected=false,display12SmallGTReady=false,tglGT2Detected=true,tglGT2Ready=false;
  XELP::GTResources display12SmallGT,tglGT2Resources;
  QUALIFICATION
  assert(tglGT2Ready==((status==AlderLakeN::GTStatus::Complete || status==AlderLakeN::GTStatus::ProfileMismatch) && mask==3 && release==3 && failed==0));
 }
 for(auto id:{0x9a40U,0x9a49U,0x9a78U,0x9a59U,0x9ac0U,0x9ac9U,0x9ad9U,0x9af8U})
 for(auto rev:{0U,1U,2U,3U,4U,255U})for(auto video:{0xe00feU,0xe00faU}) {
  check(id,rev,7,0xc0,video); // 48 EU, three DSS, six banks
  check(id,rev,0x3e,0,video); // 80 EU, five DSS, eight banks
  check(id,rev,0x3f,0,video); // 96 EU, six DSS, eight banks
 }
 XELP::GTResources resource;
 assert(!XELP::decodeTGLGT2(nullptr,resource));
 for(unsigned slice=0;slice<4;++slice)for(unsigned dss=0;dss<64;++dss)
 for(unsigned eu:{0U,0xc0U,0xf0U,0xffU})for(unsigned banks:{0U,0xc0U,0xf0U,0xffU}) {
  uint32_t f[]={slice,dss,eu,banks,0xe00fa};unsigned count=__builtin_popcount(dss);
  bool expected=slice==1 && eu==0 && ((count==3 && banks==0xc0) || ((count==5 || count==6) && banks==0));
  assert(XELP::decodeTGLGT2(f,resource)==expected);
 }
 for(unsigned i=0;i<5;++i){uint32_t f[]={1,63,0,0,0xe00fa};f[i]=~0U;assert(!XELP::decodeTGLGT2(f,resource));}
 puts("PASS all eight TGL GT2 IDs, fuse-derived URB native stores, unsupported topology rejection");
}'''.replace('BODY',body).replace('QUALIFICATION',qualification)
with tempfile.TemporaryDirectory(prefix='small-gt-urb-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
