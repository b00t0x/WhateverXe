#!/usr/bin/env python3
"""Replay the native DKL signal setter through the actual wrapper against Linux tables."""
import os,argparse
from pathlib import Path
import re,struct,json,subprocess,tempfile,hashlib
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('donor',type=Path)
p.add_argument('--linux-tables',type=Path,required=True);args=p.parse_args()
source=(r/'WhateverXe/kern_xe.cpp').read_text()
wrapper=re.search(r'uint32_t XE::wrapTGLSignalLevels\(.*?\n\}',source,re.S).group()
binpath=args.donor
assert hashlib.sha256(binpath.read_bytes()).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
linux=args.linux_tables.read_text();tables=[]
for name in ['hbr','hbr2']:
 block=re.search(r'_tgl_dkl_phy_trans_dp_'+name+r'\[\] = \{(.*?)\n\};',linux,re.S).group(1)
 rows=re.findall(r'\.dkl = \{ (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+)',block);assert len(rows)==10
 tables.append('{'+','.join('{'+','.join(row)+'}' for row in rows)+'}')
header=(r/'WhateverXe/kern_xe.hpp').read_text()
fields=header[header.index('\tbool tglAUXParentReady'):header.index('\tstatic void wrapTGLEnableAUXPower')]
code=r'''
#include <cstdint>
#include <cassert>
#include "kern_tgl_tc_boot.hpp"
#include "kern_tgl_port_map.hpp"
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
template<class...T>void tglTransitionRecord(T...){assert(false);}
struct IOService;
#include <cstdlib>
bool tglQuietTraining(){return std::getenv("TGL_TEST_QUIET") != nullptr;}
#include <cstdio>
#include <cstring>
#include <cassert>
#include <fstream>
#include <vector>
#include <sys/mman.h>
struct IORegistryEntry {
 static IORegistryEntry* getRegistryRoot(){static IORegistryEntry a;return &a;}
 void setProperty(const char*,const void*,size_t){}
};
struct XE {
 bool adlsDisplaySelected=false;struct {bool valid=false;unsigned dp=0,panel=0;}adlsWiring;
FIELDS
 // This fixture exercises the legacy DKL path. Combo dispatch is a boundary;
 // accidentally entering DP-alt must fail rather than be mocked successful.
 static bool tglProgramComboSignal(void*,uint32_t,uint32_t,void*,uint32_t&){return false;}
 static bool tglDPAltFia(void*,TGLFiaPort&){assert(false);return false;}
 static uint32_t wrapTGLPortReadReg(void*,uint32_t){assert(false);return ~0U;}
 static bool tglDekelAccess(void*,uint32_t,bool,uint32_t&){assert(false);return false;}
 bool tglDekelReady=true;
 uintptr_t orgTGLSignalLevels;
 static uint32_t wrapTGLSignalLevels(void*,uint32_t,uint32_t,void*);
};
XE object;XE* callbackXE=&object;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
WRAPPER
uint32_t regs[0x50],mask=15;
uint32_t getmask(void*){return mask;}
uint32_t rd(void*,uint32_t i){assert(i<0x50);return regs[i];}
void wr(void*,uint32_t i,uint32_t v){assert(i<0x50);regs[i]=v;}
void jump(uint8_t* p,uintptr_t dst){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&dst,8);p[10]=0xff;p[11]=0xe0;}
unsigned calls;void *seenPort,*seenOverride;uint32_t seenSwing,seenPre;
uint32_t passthrough(void* p,uint32_t s,uint32_t e,void* o){calls++;seenPort=p;seenSwing=s;seenPre=e;seenOverride=o;return 0xabcdef01;}
int main(int argc,char**argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});
 size_t size=4*1024*1024;assert(b.size()<size);
 auto mem=(uint8_t*)mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);assert(mem!=MAP_FAILED);memcpy(mem,b.data(),b.size());
 jump(mem+0x607c0,(uintptr_t)rd);jump(mem+0x60876,(uintptr_t)wr);
 object.orgTGLSignalLevels=(uintptr_t)(mem+0x60ae0);
 object.tglTCWiring=TGLTCWiring::fromMasks(0x10,0,0);
 alignas(8) uint8_t port[0x1000]={};uintptr_t vt[128]={};vt[0x1a0/8]=(uintptr_t)getmask;
 *(uintptr_t**)port=vt;uint32_t cfg[8]={4,0,1,1};*(uint32_t**)(port+0x548)=cfg;*(uint32_t*)(port+0x550)=4;
 const uint32_t oracle[2][10][3]={TABLES};unsigned checked=0;
 for(uint32_t rate:{6u,10u,20u}){*(uint32_t*)(port+0x128)=rate;unsigned idx=0;
  for(uint32_t swing=0;swing<4;swing++)for(uint32_t pre=0;pre<4-swing;pre++,idx++){
   for(auto&v:regs)v=0xa5fabcdf;
   assert(XE::wrapTGLSignalLevels(port,swing,pre,nullptr)==0);
   auto t=oracle[rate>10][idx];uint32_t expected=(0xa5fabcdf&0xfffc00f8)|t[0]|(t[1]<<13)|(t[2]<<8);
   for(unsigned i:{0x42u,0x43u,0x45u,0x46u})assert(regs[i]==expected);
   assert(regs[0x44]==(0xa5fabcdf&~4u));assert(regs[0x47]==(0xa5fabcdf&~4u));checked++;
  }
 }
 // Target guards must retain every original argument and original result.
 object.orgTGLSignalLevels=(uintptr_t)passthrough;
 auto check=[&](void*p,unsigned s,unsigned e,void*o){unsigned before=calls;assert(XE::wrapTGLSignalLevels(p,s,e,o)==0xabcdef01);assert(calls==before+1&&seenPort==p&&seenSwing==s&&seenPre==e&&seenOverride==o);};
 uint32_t custom[3]={1,2,3};check(port,1,1,custom);
 check(nullptr,1,1,nullptr);check(port,4,0,nullptr);check(port,0,4,nullptr);check(port,2,2,nullptr);check(port,~0u,1,nullptr);
 object.tglDekelReady=false;check(port,1,1,nullptr);object.tglDekelReady=true;
 cfg[0]=1;check(port,1,1,nullptr);cfg[0]=4;cfg[3]=0;check(port,1,1,nullptr);cfg[3]=1;
 *(uint32_t*)(port+0x550)=2;check(port,1,1,nullptr);*(uint32_t*)(port+0x550)=4;
 *(uint32_t*)(port+0x128)=30;check(port,1,1,nullptr);*(uint32_t*)(port+0x128)=20;
 *(uint32_t**)(port+0x548)=nullptr;check(port,1,1,nullptr);
 printf("Native donor CPU replay: %u rate/level cases match primary tables; %u excluded calls preserve arguments/result. No GPU execution.\n",checked,calls);
}
'''.replace('FIELDS',fields).replace('WRAPPER',wrapper).replace('TABLES',','.join(tables))
temporary=tempfile.TemporaryDirectory(prefix='tgl-dkl-signal-');p=Path(temporary.name);(p/'check.cpp').write_text(code)
subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2','-I',str(r/'WhateverXe'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
subprocess.run([str(p/'check'),str(binpath)],check=True)
subprocess.run([str(p/'check'),str(binpath)],check=True,env=dict(os.environ,TGL_TEST_QUIET="1"))
