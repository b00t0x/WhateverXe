#!/usr/bin/env python3
"""Execute two donor outer-ring emitters on CPU; capture writes, never submit GPU."""
import hashlib,re,subprocess,tempfile,sys
from pathlib import Path
# Current production emitters by default; an older source may be supplied explicitly.
r=Path(__file__).resolve().parents[1]
src=Path(sys.argv[2]).read_text() if len(sys.argv)>2 else (r/"WhateverXe/kern_xe.cpp").read_text()
def arrays(name):
 t=re.search(r'const uint8_t '+name+r'\[\]\[10\] \{(.*?)\};',src,re.S).group(1)
 return [bytes(int(x.strip(),0) for x in row.split(',')) for row in re.findall(r'\{([^}]+)\}',t)]
data=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(data).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
old,new=arrays('flushFind'),arrays('flushReplace')
for off,a,b in zip([0x84e5d,0x85406],old,new):
 assert data[off:off+10]==a
 # Gen12 PIPE_CONTROL: HDC pipeline flush is DW0 bit9; depth stall
 # is DW1 bit13. No additional tile-cache bit is specified for these sites.
 header=int.from_bytes(a[2:6],'little') | (1<<9)
 flags=int.from_bytes(a[6:10],'little') | (1<<13)
 assert b==a[:2]+header.to_bytes(4,'little')+flags.to_bytes(4,'little')
code=r'''
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fstream>
#include <vector>
static uint64_t scratch,guard=0xfeed12345678ULL;
static unsigned mode;
static std::vector<uint32_t> output, lengths;
extern "C" uint32_t gtt(void*) {return mode;}
extern "C" uint64_t address(void*) {return scratch;}
extern "C" void writebuf(void*,uint32_t*p,unsigned n) {assert(n<64);lengths.push_back(n);output.insert(output.end(),p,p+n);}
void jump(uint8_t*p,void*f) {p[0]=0x48;p[1]=0xb8;memcpy(p+2,&f,8);p[10]=0xff;p[11]=0xe0;}
template<class T> void put(uint8_t*p,unsigned off,T v){memcpy(p+off,&v,sizeof(v));}
int main(int argc,char**argv) {
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<char>d((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
 auto mem=(uint8_t*)mmap(nullptr,d.size(),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);
 assert(mem!=MAP_FAILED);memcpy(mem,d.data(),d.size());
 jump(mem+0x42364,(void*)gtt);jump(mem+0x8224,(void*)address);jump(mem+0x41e7a,(void*)writebuf);put(mem,0xc8200,&guard);
 uint8_t self[0x100]={},owner[0x1200]={},context[0x100]={},task[0x100]={};
 put(self,0x10,owner);put(self,0x20,context);put(context,0x58,task);
 put(self,0x28,uint64_t(0x123456780040ULL));put(self,0x38,uint32_t(17));
 const uint8_t first[]={FIRST},second[]={SECOND},patchedFirst[]={PATCHED_FIRST},patchedSecond[]={PATCHED_SECOND};
 const uint64_t addresses[]={0x40000010ULL,0x123400001010ULL,0x123456789abfULL,0xfffffffffffcULL};
 unsigned cases=0,changed=0,unchanged=0;
 for(auto s:addresses)for(mode=0;mode<2;++mode)for(unsigned path=0;path<2;++path)for(unsigned notify=0;notify<2;++notify)for(unsigned extra=0;extra<2;++extra)for(unsigned which=0;which<2;++which) {
  scratch=s;owner[0x1190]=path?0x40:0;put(owner,0xf4a,uint32_t(extra<<20));
  std::vector<uint32_t> a,al,b,bl;
  for(unsigned variant=0;variant<2;++variant) {
   memcpy(mem+0x84e5d,variant?patchedFirst:first,10);memcpy(mem+0x85406,variant?patchedSecond:second,10);
   output.clear();lengths.clear();
   if(which)((void(*)(void*)) (mem+0x853a6))(self);
   else ((void(*)(void*,uint32_t,bool))(mem+0x84dc8))(self,0x1234abcd,notify);
   if(!variant){a=output;al=lengths;}else{b=output;bl=lengths;}
  }
  assert(a.size()==b.size() && al==bl && al[0]==6);
  assert(b[0]==(a[0]|(1U<<9)) && b[1]==(a[1]|(1U<<13)));
  assert(b[2]==(uint32_t(s)&~3U) && b[3]==uint32_t(s>>32));
  for(unsigned i=0;i<a.size();++i)if(i!=0 && i!=1)assert(a[i]==b[i]);
  if(which)assert(b.size()==12+extra*40);
  else {assert(b[8]==uint32_t(0x123456780040ULL+17*64));assert(b[9]==0x1234);assert(b[10]==0x1234abcd);assert(b.size()==(path&&notify?22:12));}
  if(a[1]==b[1])++unchanged;else ++changed;
  ++cases;
 }
 printf("%u paired native emitter cases PASS: %u depth-stall additions, %u already set; packet boundaries, address, stamp, notify and optional writes preserved\n",cases,changed,unchanged);
 munmap(mem,d.size());
}
'''.replace('PATCHED_FIRST',','.join(map(str,new[0]))).replace('PATCHED_SECOND',','.join(map(str,new[1]))).replace('FIRST',','.join(map(str,old[0]))).replace('SECOND',','.join(map(str,old[1])))
with tempfile.TemporaryDirectory(prefix='tgl-outer-flush-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),sys.argv[1]],check=True)
