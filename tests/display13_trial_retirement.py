#!/usr/bin/env python3
"""Exercise the opt-in boot-retirement wrapper's native ordering and refusals."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
functions='\n'.join(re.search(r'(?:uint32_t|void \*) XE::'+n+r'\(.*?\n\}',s,re.S).group() for n in ['wrapDisplay13TrialCommit'])
code=r'''
#include "kern_display13_fb_memory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
struct OSObject {alignas(8) unsigned char bytes[0x4b90]{};unsigned size=0x4b90;
 OSObject *getMetaClass(){return this;} unsigned getClassSize(){return size;}};
using IOService=OSObject;
namespace TGLFBBinding {enum class Status {Unavailable,Complete};struct Readout {Status status=Status::Complete;unsigned ddi=1;};}
struct Wiring{};
bool trial=true,healthy=true,selected=true,bound=true;
TGLFBBinding::Readout adlnInspectPathBinding(IOService*,OSObject*,void*,Wiring){return {bound?TGLFBBinding::Status::Complete:TGLFBBinding::Status::Unavailable,1};}
namespace Display13FBTopology {bool selected(void*,uint32_t*,Wiring){return ::selected;}}
bool checkKernelArgument(const char*){return trial;}
struct IORegistryEntry {static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}template<class...T>void setProperty(T...){} };
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
OSObject ctl,fb,path,oldPort,newPort;unsigned nativeCalls=0,freeCalls=0,freeError=0;bool fbPresent=true;
unsigned native(void *c,void*,void*){assert(c==&ctl);++nativeCalls;return 123;}
OSObject *lookup(unsigned index){return index==3?&newPort:nullptr;}
struct XE {
 bool display13FramebufferRequested=true;void*adlnPlatform=nullptr;Wiring adlnOutputWiring;
 uintptr_t tglPortByDDI=(uintptr_t)lookup,orgDisplay13TrialCommit=(uintptr_t)native;
 static XE *callbackXE;
 static void *tglResourceFB(void *c,unsigned i){return c==&ctl&&i==0&&fbPresent?&fb:nullptr;}
 static bool display13RetirementHealthy(){return healthy;}
 static unsigned wrapDisplay13FreeResources(void *f){assert(f==&fb&&!nativeCalls);++freeCalls;return freeError;}
 static uint32_t wrapDisplay13TrialCommit(void*,void*,void*);
} state;
XE *XE::callbackXE=&state;
FUNCTIONS
template<class T>void put(OSObject &o,unsigned at,T v){memcpy(o.bytes+at,&v,sizeof(v));}
int main(){
 for(unsigned bad=0;bad<24;++bad){
  state={};ctl={};fb={};path={};oldPort={};newPort={};trial=healthy=selected=bound=fbPresent=true;
  nativeCalls=freeCalls=freeError=0;uint32_t req[32]={1},out[32]{};req[0x48/4]=4;
  put(fb,0x44de,uint8_t(1));put(fb,0x1e7,uint8_t(1));put(fb,0x49e8,1u);put(fb,0x4a00,1u);
  put(fb,0x4a08,&path);put(path,0x3648,&oldPort);put(path,0x3644,1u);put(newPort,0x554,2u);put(newPort,0x120,3u);
  switch(bad){
   case 1:trial=false;break;case 2:state.display13FramebufferRequested=false;break;
   case 3:fbPresent=false;break;case 4:req[0]=2;break;case 5:healthy=false;break;
   case 6:put(fb,0x1e0,uint8_t(1));break;case 7:put(fb,0x44de,uint8_t(0));break;
   case 8:put(fb,0x1e7,uint8_t(0));break;case 9:put(fb,0x49e8,2u);break;
   case 10:put(fb,0x4a00,0u);break;case 11:bound=false;break;case 12:req[0x48/4]=0;break;
   case 13:req[0x48/4]=10;break;case 14:state.tglPortByDDI=0;break;
   case 15:put(path,0x3644,0u);break;case 16:put(path,0x3644,3u);break;
   case 17:put(oldPort,0x554,2u);break;case 18:put(newPort,0x554,1u);break;
   case 19:selected=false;break;case 20:put(newPort,0x120,4u);break;
   case 21:freeError=77;break;case 22:put(path,0x3644,2u);break;
   case 23:put(path,0x3648,&newPort);break;
  }
  const auto result=XE::wrapDisplay13TrialCommit(&ctl,req,out);
  const bool retires=bad==0||bad==21||bad==22;
  assert(freeCalls==unsigned(retires));assert(nativeCalls==unsigned(bad!=21));
  assert(result==(bad==21?77u:123u));
 }
 assert(XE::wrapDisplay13TrialCommit(&ctl,nullptr,nullptr)==123);
 puts("PASS trial retirement: native stop before replacement, failure propagation, opt-in and live-resource refusals");
}
'''.replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='wex-trial-retire-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
