#!/usr/bin/env python3
"""Run the native FF_MODE2 site on CPU memory; check full write and fall-through."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
s=(Path(__file__).resolve().parents[1]/'WhateverXe/kern_xe.cpp').read_text()
d=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(d).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
def arr(n):return bytes(int(x,0) for x in re.search(r'const uint8_t '+n+r'\[\] \{([^}]+)',s)[1].split(','))
a,b=arr('ffModeFind'),arr('ffModeReplace')
assert len(a)==len(b)==17 and d[0x2561c:0x2562d]==a
assert b==bytes.fromhex('c78004660000000004e0')+bytes([0x90])*7
code=r'''#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
extern "C" __attribute__((naked)) void run(void*,void*,void*) {
 asm("pushq %rbx\nmovq %rdx,%rbx\nmovq %rsi,%rax\ncallq *%rdi\npopq %rbx\nretq");
}
int main(){
 const uint8_t original[]={OLD};const uint8_t patch[]={NEW};const uint8_t tail[]={TAIL};
 auto m=(uint8_t*)mmap(nullptr,4096,7,MAP_ANON|MAP_PRIVATE,-1,0);assert(m!=MAP_FAILED);
 uint8_t regs[0x10000],base[0x10000],wa[0x30]={};unsigned cases=0;
 for(uint32_t flags:{0U,1U<<26,~0U})for(uint32_t previous:{0U,0x12345678U,~0U}){
  memset(regs,0x5A,sizeof(regs));memcpy(regs+0x6604,&previous,4);memcpy(base,regs,sizeof(regs));memcpy(wa+0x18,&flags,4);
  memcpy(m,original,17);memcpy(m+17,tail,sizeof(tail));m[17+sizeof(tail)]=0xc3;run(m,regs,wa);
  assert(*(uint32_t*)(regs+0x6604)==((flags&(1U<<26))?(previous|0x40000):previous));
  memcpy(regs,base,sizeof(regs));memcpy(m,patch,17);run(m,regs,wa);
  assert(*(uint32_t*)(regs+0x6604)==0xE0040000);
  *(uint32_t*)(base+0x6604)=0xE0040000;*(uint32_t*)(base+0x94d4)|=0x80;
  assert(!memcmp(regs,base,sizeof(regs)));++cases;
 }
 printf("PASS %u native-site cases: full FF_MODE2 write independent of gate/prior contents; following94D4 write and all other memory preserved\n",cases);
}'''
code='#include <initializer_list>\n'+code
for k,v in [('OLD',a),('NEW',b),('TAIL',d[0x2562d:0x25637])]:code=code.replace(k,','.join(str(x) for x in v))
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
