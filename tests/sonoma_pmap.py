#!/usr/bin/env python3
"""Execute actual 23J631 template bridges against a guarded synthetic pmap frame.

This checks instruction/register/flag semantics only, not native locking, TLBs,
or GPU coherency. Optional --kernel checks all source guards against exact bytes.
"""
from pathlib import Path
import argparse,re,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
s=(R/'WhateverXe/kern_xe.cpp').read_text()
a=argparse.ArgumentParser();a.add_argument('--kernel',type=Path);a.add_argument('--hde-source',type=Path);a=a.parse_args()
bridges='\n'.join(re.search(r'extern "C" __attribute__\(\(naked,used\)\) void tglSonomaPmap'+kind+r'Bridge\(\) \{.*?\n\}',s,re.S)[0] for kind in ['New','Update'])
if a.kernel:
 from pmap_kernel_contract import check
 check(a.kernel,s,"Sonoma",a.hde_source)
cpp=r'''#include <stdint.h>
#include <initializer_list>
#include <string.h>
#include <assert.h>
#include <stdio.h>
extern "C" {
 uint32_t tglPmapBARFirstPage,tglPmapBAREndPage;
 uint64_t tglPmapNewEvidence[13],tglPmapUpdateEvidence[13];
 uintptr_t tglPmapNewResume,tglPmapUpdateResume;
 uint64_t in[16],out[16],outFlags;void *target;
 alignas(16) uint8_t frame[512];
}
BRIDGES
extern "C" __attribute__((naked)) void done() {asm volatile(
 CAPTURE
 "pushfq\n"
 "popq _outFlags(%rip)\n"
 "popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %rbx\npopq %rbp\nretq\n");}
extern "C" __attribute__((naked)) void run() {asm volatile(
 "pushq %rbp\npushq %rbx\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
 LOAD
 "jmpq *_target(%rip)\n");}
int main() {
 tglPmapNewResume=tglPmapUpdateResume=(uintptr_t)done;

 const uint32_t flags[]={0,4,0x100,0x104};
 unsigned cases=0;
 for(auto base:{0x4010000U,0x80000U,0x6000000U}) {
 tglPmapBARFirstPage=base;tglPmapBAREndPage=base+0x1000;
 const uint32_t pages[]={0,base-1,base,base+0x555,base+0xfff,base+0x1000,0xffffffff};
 for(unsigned kind=0;kind<2;++kind) for(auto pn:pages) for(auto flag:flags)
 for(unsigned native=0;native<2;++native) for(unsigned pat=0;pat<8;++pat) for(unsigned prot=0;prot<16;++prot) {
  memset(frame,0xA5,sizeof(frame));auto rbp=frame+256;
  memcpy(rbp-0x64,&pn,4);memcpy(rbp-0x4c,&flag,4);memcpy(rbp-0x50,&prot,4);
  uint64_t cr3=native?0x12345000:0;memcpy(rbp-0x48,&cr3,8);
  uint8_t before[512];memcpy(before,frame,512);
  for(unsigned i=0;i<16;++i)in[i]=0xAABBCCDD00001000ULL+i*0x1000;
  in[5]=(uintptr_t)rbp;
  const unsigned templateReg=kind?2:3,outputReg=kind?0:2;
  const uint64_t original=0x8000004010000000ULL|((pat&3)<<3)|((pat&4)<<5);
  in[templateReg]=original;
  const bool hit=pn>=base && pn<base+0x1000;
  const bool change=hit && native && !(flag&0x100) && pat==2;
  const auto corrected=original|(change?8:0);
  uint64_t expect[16];memcpy(expect,in,sizeof(in));expect[templateReg]=corrected;
  expect[kind?3:6]=prot;expect[outputReg]=corrected|(prot&1);
  auto evidence=kind?tglPmapUpdateEvidence:tglPmapNewEvidence;memset(evidence,0,13*8);
  target=kind?(void*)tglSonomaPmapUpdateBridge:(void*)tglSonomaPmapNewBridge;
  for(unsigned repeat=1;repeat<=2;++repeat) {
   run();
   for(unsigned i=0;i<16;++i)if(i!=4)assert(out[i]==expect[i]); // RSP belongs to harness
   assert((outFlags&0x8C5)==((prot&2)?0:0x44)); // TEST: CF/OF/SF=0, ZF/PF
   assert(!memcmp(frame,before,512));
   assert(evidence[0]==(hit?repeat:0));assert(evidence[1]==(change?repeat:0));
   assert(evidence[2]==(hit && !native?repeat:0));
   assert(evidence[3]==(hit && native && (flag&0x100)?repeat:0));
   assert(evidence[4]==(hit && native && !(flag&0x100) && pat!=2?repeat:0));
   if(hit){assert(evidence[6]==pn);assert(evidence[7]==flag);assert(evidence[8]==cr3);assert(evidence[9]==original);assert(evidence[12]==1);}
   ++cases;
  }
 }
 }
 printf("PASS: %u actual bridge executions, all GPRs/defined flags/stack unchanged except displaced outputs and admitted UC correction; hit/skip counters and first-sample path\n",cases);
}
'''
# Order is explicit and shared only by harness capture/load, not bridge code.
regs=['rax','rbx','rcx','rdx','rsp','rbp','rsi','rdi']+['r'+str(i) for i in range(8,16)]
capture='\n'.join('"movq %'+r+', _out+'+str(8*i)+'(%rip)\\n"' for i,r in enumerate(regs))
load='\n'.join('"movq _in+'+str(8*i)+'(%rip), %'+r+'\\n"' for i,r in enumerate(regs) if r!='rsp')
cpp=cpp.replace('BRIDGES',bridges).replace('CAPTURE',capture).replace('LOAD',load)
with tempfile.TemporaryDirectory(prefix='sonoma-pmap-') as temp:
 p=Path(temp);(p/'test.cpp').write_text(cpp)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
