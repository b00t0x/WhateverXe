#!/usr/bin/env python3
"""Replay saved WS aux states through native donor code on CPU; no GPU execution."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,tempfile
repo=Path(__file__).resolve().parents[1]
tmp=tempfile.TemporaryDirectory(prefix='tgl-ccs-align-');S=Path(tmp.name)
source=(repo/'WhateverXe/kern_xe.cpp').read_text()
helper=re.search(r'static bool tglCorrectCCSAlignment\(uint32_t \*w\) \{.*?\n\}',source,re.S).group()
donor=Path(sys.argv[1]);d=donor.read_bytes();assert hashlib.sha256(d).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
head=bytes(int(v,0) for v in re.search(r'const uint8_t auxHead\[\] \{(.*?)\}',source).group(1).split(','));assert d[0x73fc2:0x73fc2+len(head)]==head
rows=json.loads(Path(sys.argv[2]).read_text())['records']
rows=[r for r in rows if r['after']['aux']==5];assert rows, 'no aux=5 replay records supplied'
states=',\n'.join('{'+','.join(r['after']['words'])+'}' for r in rows)
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <fstream>
#include <vector>
HELPER
int main(int argc,char**argv) {
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<char>d((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
 auto mem=(uint8_t*)mmap(nullptr,d.size(),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);
 assert(mem!=MAP_FAILED);memcpy(mem,d.data(),d.size());
 auto native=reinterpret_cast<void(*)(void*,void*,uint32_t*,unsigned)>(mem+0x73fc2);
 uint32_t observed[][16]={STATES};
 uint8_t resource[512]={},entry[256]={};entry[0x91]=2;
 for(auto &saved:observed) {
  uint32_t w[16];memcpy(w,saved,64);w[6]&=~7U;
  native(resource,entry,w,0);assert(!memcmp(w,saved,64));
  assert(tglCorrectCCSAlignment(w));assert(w[0]==(saved[0]|0x8000));
  assert(!memcmp(w+1,saved+1,60));assert(!tglCorrectCCSAlignment(w));
  const unsigned index[]={6,0,0,0,1,2,3,4,5,2,3};
  const uint32_t values[]={0,0x33016000,0x3301b000,0x33117000,0xca080010,0x803f003f,0x2001ff,0x80,1,0x003f003e,0x1fe};
  for(unsigned i=0;i<sizeof(index)/sizeof(*index);++i) {
   uint32_t before[16];memcpy(w,saved,64);w[index[i]]=values[i];memcpy(before,w,64);
   assert(!tglCorrectCCSAlignment(w));assert(!memcmp(w,before,64));
  }
 }
 munmap(mem,d.size());puts("Retained WS aux states: native aux update reproduced; only HALIGN bit changed; idempotence and excluded-layout preservation pass. No GPU execution.");
}
'''.replace('HELPER',helper).replace('STATES',states)
(S/'check.cpp').write_text(code)
subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(S/'check.cpp'),'-o',str(S/'check')],check=True)
subprocess.run([str(S/'check'),str(donor)],check=True)
