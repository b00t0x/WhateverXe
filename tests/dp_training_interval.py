"""Replay the shared installer and the fixed donor's selection, cache and setupPort fragments."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
donor_path = Path(sys.argv[1])
donor = donor_path.read_bytes()
# These are the executable regions used by the fixture. Signing and packaging
# differ between systems; the six release payloads share this donor code.
if len(donor)<0xe0000 or hashlib.sha256(donor[0x1de0e:0x1eb9d]+donor[0x1b61a:0x1b7f0]).hexdigest() != 'b64ba7a8e38af877124cb7bbfeaa4cd13be8774629821e192828617f0ccd913e':
    raise ValueError('Unexpected framebuffer selection or setupPort code')
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
start = source.index('static bool installDPTrainingInterval(')
installer = source[start:source.index('\n}', start) + 2]
fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <sys/mman.h>
using mach_vm_address_t=uintptr_t;
struct KernelPatcher {
 void clearError(){}
 template<class T>T solveSymbol(size_t,const char *name,uintptr_t base,size_t){
  assert(!strcmp(name,"__ZN14AppleIntelPort11getDPCDInfoEv"));return base+0x1de0e;
 }
 void routeBlock(uintptr_t dst,const uint8_t *code,size_t n){memcpy((void*)dst,code,n);}
};
INSTALLER
static unsigned basic,extended,late,low,reads;
static bool failBasic,failExtended,failLate;
static int readAUX(void *,unsigned address,uint8_t *out,unsigned size){
 ++reads;
 if(address==0xd){assert(size==2);if(failLate)return 1;out[0]=low;out[1]=late;return 0;}
 assert(size==16 && (address==0 || address==0x2200));
 if(!address && failBasic)return 47;
 if(address && failExtended)return 1;
 memset(out,0,size);out[0]=address?0x14:0x12;out[14]=address?extended:basic;
 return 0;
}
extern "C" unsigned replay(uint8_t *start,uint8_t *port);
extern "C" void setup(uint8_t *start,uint8_t *port);
static void jump(uint8_t *site,uintptr_t target){
 const uint8_t jmp[]={0xff,0x25,0,0,0,0};memcpy(site,jmp,6);memcpy(site+6,&target,8);
}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<uint8_t> donor((std::istreambuf_iterator<char>(f)),{});
 auto image=(uint8_t*)mmap(nullptr,donor.size(),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);
 assert(image!=MAP_FAILED);memcpy(image,donor.data(),donor.size());
 KernelPatcher p;auto install=[&]{return installDPTrainingInterval(p,0,(uintptr_t)image,donor.size());};
 image[0x1e970]^=1;assert(!install() && image[0x1dfc9]==0x48);image[0x1e970]^=1;
 assert(install());
 // Execute the real capability selection. Replace only external AUX and bzero.
 jump(image+0x1cc72,(uintptr_t)readAUX);
 const uint8_t clear16[]={0x0f,0x57,0xc0,0x0f,0x11,0x07,0xc3};
 memcpy(image+0x1000,clear16,sizeof(clear16));int32_t rel=0x1000-0x1dfac;memcpy(image+0x1dfa8,&rel,4);
 // The omitted port discovery clobbers the old AUX buffer and RBX. Resume
 // at the donor's real spill reload, then use its ordinary late AUX read.
 const uint8_t clobber[]={0x31,0xdb,0xc6,0x45,0xc0,0xff,0xc6,0x45,0xc1,0x72,0xc6,0x45,0xce,0x73,0xe9};
 memcpy(image+0x1dff9,clobber,sizeof(clobber));rel=0x1e890-(0x1dff9+sizeof(clobber)+4);
 memcpy(image+0x1dff9+sizeof(clobber),&rel,4);
 image[0x1e89e]=0xe9;rel=0x1e938-0x1e8a3;memcpy(image+0x1e89f,&rel,4);
 // Return the native extension flag via the donor's stack unwind.
 const uint8_t finish[]={0x0f,0xb6,0xc3,0xe9};memcpy(image+0x1e97c,finish,sizeof(finish));
 rel=0x1de6d-0x1e984;memcpy(image+0x1e980,&rel,4);
 // Bypass the kernel stack canary in the failed initial-read exit.
 image[0x1de59]=0xe9;rel=0x1de6d-0x1de5e;memcpy(image+0x1de5a,&rel,4);
 uint8_t untouched[0x600]={};untouched[0xfa]=81;failBasic=true;reads=0;
 assert(replay(image+0x1df24,untouched)==47 && reads==1 && untouched[0xfa]==81);
 failBasic=false;
 image[0x1b633]=image[0x1b7f0]=0xc3;
 for(unsigned raw=0;raw<=255;++raw)for(unsigned scenario=0;scenario<4;++scenario)for(bool failure:{false,true}){
  basic=raw;extended=(raw+13)&255;failExtended=scenario==1;
  if(scenario==2)extended|=128;
  if(scenario==3)extended=0;
  failLate=failure;late=0xed;low=raw^0x55;reads=0;
  uint8_t port[0x600]={};bool useExtended=!failExtended && ((basic|extended)&128);
  assert(replay(image+0x1df24,port)==unsigned(useExtended) && reads==3);
  assert(port[0xc]==(useExtended?0x14:0x12));
  unsigned interval=(useExtended?extended:basic)&127;
  assert(port[0xf9]==(failLate?3:low&3) && port[0xfa]==interval);
  setup(image+0x1b61a,port);setup(image+0x1b76f,port);
  uint32_t us[2];memcpy(us,port+0x12c,sizeof(us));
  assert(us[0]==std::clamp(interval*4000,100U,65000U));
  assert(us[1]==std::clamp(interval*4000,400U,65000U));
 }
 // With the donor's original cache store restored, the conflicting later
 // base read overwrites the selected extended interval and includes bit 7.
 memcpy(image+0x1e964,donor.data()+0x1e964,22);
 basic=0x80;extended=2;late=0x80;failExtended=failLate=false;
 uint8_t control[0x600]={};
 assert(replay(image+0x1df24,control)==1 && control[0xfa]==0x80);
}
'''
assembly = """
.text
.globl _replay
_replay:
 push %rbp
 mov %rsp,%rbp
 push %r15
 push %r14
 push %r13
 push %r12
 push %rbx
 sub $0x98,%rsp
 mov %rsi,%r15
 lea 0xc(%r15),%r12
 jmp *%rdi
.globl _setup
_setup:
 push %rbx
 mov %rsi,%rbx
 call *%rdi
 pop %rbx
 ret
"""
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    (path/'check.cpp').write_text(fixture.replace('INSTALLER', installer))
    (path/'call.s').write_text(assembly)
    subprocess.run(['clang++','-std=c++17',str(path/'check.cpp'),str(path/'call.s'),'-o',str(path/'check')],check=True)
    subprocess.run([str(path/'check'),sys.argv[1]],check=True)
