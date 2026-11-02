#!/usr/bin/env python3
"""Production client admission and optional exact native AGDC dispatch replay."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
names=['processFramebufferAGDC','wrapFramebufferUserClient','wrapFramebufferAGDCCallback']
functions='\n'.join(re.search(r'(?:bool|uint32_t) XE::'+n+r'\(.*?\n\}',s,re.S).group() for n in names)
arrays={n:re.search(n+r'\[\]=\{([^}]+)',functions).group(1) for n in ['clientEntry','callbackEntry','dispatch']}
assert s.count('processFramebufferAGDC(patcher,index,address,size)')==1
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 assert hashlib.sha256(b).hexdigest() in ['c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731','8f83afbe21f216ed18ea798c10d91b7b4fd990b62a5740832c1f90137722c32f']
 for key,offset in [('clientEntry',0x273b4),('callbackEntry',0x69df2),('dispatch',0x69e1e)]:
  expected=bytes(int(x,0) for x in arrays[key].split(','));assert b[offset:offset+len(expected)]==expected
code=r'''
#include "kern_framebuffer_donor.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <sys/mman.h>
using mach_vm_address_t=uintptr_t;
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
#define FunctionCast(fn,addr) reinterpret_cast<decltype(&fn)>(addr)
#define arrsize(a) (sizeof(a)/sizeof(a[0]))
struct Meta {unsigned size=0x4b90;unsigned getClassSize(){return size;}};
struct IOService {alignas(16) uint8_t bytes[0x4b90]{};Meta meta;Meta *getMetaClass(){return &meta;}};
namespace Display13FBMemory {template<class T> T field(void *p,unsigned n){T v;memcpy(&v,(uint8_t*)p+n,sizeof(v));return v;}}
template<class T> void put(void *p,unsigned n,T v){memcpy((uint8_t*)p+n,&v,sizeof(v));}
struct IORegistryEntry {
 void setProperty(const char *,bool){} void setProperty(const char *,void *,size_t){}
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
};
struct KernelPatcher {
 unsigned routes=0;bool fail=false;uintptr_t delta=0;
 struct RouteRequest {template<class F> RouteRequest(const char *,F,uintptr_t &) {}};
 template<class T> T solveSymbol(unsigned,const char *s,uintptr_t a,size_t){return a+(strstr(s,"newUserClient")?0x273b4:0x69df2)+delta;}
 void clearError(){}
 bool routeMultipleShort(unsigned,RouteRequest*,unsigned count,uintptr_t,size_t){assert(count==2);++routes;return !fail;}
};
struct XE {
 static XE *callbackXE;bool display13FramebufferRequested=true,display13FBABIReady=true;
 IOService *adlnTopologyController=nullptr,*tglObservationController=nullptr;
 uint32_t framebufferProtocolCalls=0;void *framebufferStartedController=nullptr;
 uintptr_t orgFramebufferUserClient=0,orgFramebufferAGDCCallback=0;
 bool processFramebufferAGDC(KernelPatcher &,size_t,uintptr_t,size_t);
 bool processDisplay13TrialAGDC(KernelPatcher &,size_t,uintptr_t,size_t){return true;}
 static uint32_t wrapFramebufferUserClient(IOService *,void *,void *,uint32_t,void **);
 static uint32_t wrapFramebufferAGDCCallback(IOService *,uint32_t,unsigned long,uint32_t);
 static void observeFramebufferUserClient(IOService*,uint32_t,void**,uint32_t){};
 static void tglTrace(unsigned,unsigned,bool,unsigned,unsigned,unsigned,unsigned long){};
};
XE *XE::callbackXE;
FUNCTIONS
IOService fb,controller,other;unsigned clientCalls=0,callbackCalls=0;
uintptr_t expectedArgument=0;uint32_t expectedEvent=0,expectedValue=0,result=0xe00002bc;
uint32_t receiver(void *context,uintptr_t arg,uint32_t value,uint32_t event){
 assert(!context&&arg==expectedArgument&&value==expectedValue&&event==expectedEvent);++callbackCalls;return result;
}
uint32_t originalClient(IOService *f,void *task,void *security,uint32_t type,void **client){
 assert(f==&fb&&task==(void*)0x1234&&security==(void*)0x5678);++clientCalls;
 if(client)*client=&controller;return result+type;
}
uint32_t originalCallback(IOService *c,uint32_t e,unsigned long a,uint32_t v){
 const bool inc=e<=11&&(0x90fU&(1U<<e));if(!inc&&e!=4&&e!=128)return 0;
 auto fn=Display13FBMemory::field<decltype(&receiver)>(c,0x1bf8);
 return fn(Display13FBMemory::field<void*>(c,0x1c00),inc?uint32_t(a+1):a,v,e);
}
void reset(XE &s){fb={};controller={};controller.meta.size=0x1c20;clientCalls=0;
 s.framebufferProtocolCalls=0;s.framebufferStartedController=&controller;
 s.adlnTopologyController=s.tglObservationController=&controller;put(&fb,0x1d0,&controller);put(&controller,0x1bf8,(uintptr_t)receiver);}
uint32_t open(void **client,unsigned type=0){return XE::wrapFramebufferUserClient(&fb,(void*)0x1234,(void*)0x5678,type,client);}
void clients(XE &s){
 for(unsigned bad=0;bad<7;++bad){reset(s);
  switch(bad){case 0:put(&controller,0x1bf8,uintptr_t(0));break;case 1:s.framebufferStartedController=nullptr;break;
   case 2:s.adlnTopologyController=&other;break;case 3:fb.meta.size=0x100;break;case 4:controller.meta.size=0x100;break;
   case 5:put(&fb,0x1d0,(IOService*)nullptr);break;case 6:FramebufferDonor::Call::close(s.framebufferProtocolCalls);break;}
  auto before=fb;void *client=(void*)1;assert(open(&client)==kIOReturnNotReady&&!client&&!clientCalls);
  assert(!memcmp(&before,&fb,sizeof(fb)));
 }
 reset(s);put(&controller,0x1bf8,uintptr_t(0));void *client=(void*)1;
 assert(open(&client)==kIOReturnNotReady&&!client&&!clientCalls);
 put(&controller,0x1bf8,(uintptr_t)receiver);
 assert(open(&client)==result&&client==&controller&&clientCalls==1);
 put(&controller,0x1bf8,uintptr_t(0));s.tglObservationController=nullptr;
 for(unsigned type:{1U,2U,~0U})assert(open(&client,type)==uint32_t(result+type));
}
void events(){
 for(bool registered:{false,true})for(unsigned event=0;event<256;++event){
  put(&controller,0x1bf8,registered?(uintptr_t)receiver:0);put(&controller,0x1c00,(void*)nullptr);
  const uintptr_t arg=0x12345678fffffff0;const bool inc=event<=11&&(0x90fU&(1U<<event)),invoked=inc||event==4||event==128;
  expectedArgument=inc?uint32_t(arg+1):arg;expectedEvent=event;expectedValue=0x789;callbackCalls=0;
  assert(XE::wrapFramebufferAGDCCallback(&controller,event,arg,expectedValue)==(invoked?(registered?result:kIOReturnNotReady):0));
  assert(callbackCalls==(invoked&&registered?1U:0U));
 }
}
int main(int argc,char **argv){
 XE s;XE::callbackXE=&s;s.orgFramebufferUserClient=(uintptr_t)originalClient;s.orgFramebufferAGDCCallback=(uintptr_t)originalCallback;
 clients(s);events();
 s.display13FramebufferRequested=false;events();reset(s);put(&controller,0x1bf8,uintptr_t(0));
 void *blocked=(void*)1;assert(open(&blocked)==kIOReturnNotReady && !blocked);
 s.display13FramebufferRequested=true;void *client=nullptr;assert(open(&client,1000)==result+1000);
 const uint8_t clientEntry[]={CLIENT_ENTRY},callbackEntry[]={CALLBACK_ENTRY},dispatch[]={DISPATCH};
 std::vector<uint8_t>b(0x150000);
 memcpy(b.data()+0x273b4,clientEntry,sizeof(clientEntry));memcpy(b.data()+0x69df2,callbackEntry,sizeof(callbackEntry));memcpy(b.data()+0x69e1e,dispatch,sizeof(dispatch));
 for(unsigned bad=2;bad<8;++bad){KernelPatcher p;auto copy=b;s.display13FramebufferRequested=s.display13FBABIReady=true;size_t size=b.size();
  switch(bad){case 0:s.display13FramebufferRequested=false;break;case 1:s.display13FBABIReady=false;break;case 2:size=0x69e5f;break;
   case 3:p.delta=1;break;case 4:copy[0x273b4]^=1;break;case 5:copy[0x69df2]^=1;break;case 6:copy[0x69e21]^=1;break;case 7:p.fail=true;break;}
  assert(!s.processFramebufferAGDC(p,1,(uintptr_t)copy.data(),size));assert(p.routes==(bad==7?1U:0U));
 }
 s.display13FramebufferRequested=s.display13FBABIReady=true;KernelPatcher p;
 assert(s.processFramebufferAGDC(p,1,(uintptr_t)b.data(),b.size())&&p.routes==1);
 if(argc>1){
  std::ifstream f(argv[1],std::ios::binary);b.assign(std::istreambuf_iterator<char>(f),{});
  size_t length=((b.size()>0x150000?b.size():0x150000)+4095)&~size_t(4095);
  auto mem=(uint8_t*)mmap(nullptr,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mem!=MAP_FAILED);memcpy(mem,b.data(),b.size());
  uintptr_t baseVtable[256]{};baseVtable[0x788/8]=(uintptr_t)originalClient;put(mem,0xc8088,(uintptr_t)baseVtable);
  assert(!mprotect(mem,length,PROT_READ|PROT_WRITE|PROT_EXEC));
  s.orgFramebufferUserClient=(uintptr_t)(mem+0x273b4);s.orgFramebufferAGDCCallback=(uintptr_t)(mem+0x69df2);
  clients(s);events();
  reset(s);put(&controller,0x1bf8,uintptr_t(0));assert(open(&client)==kIOReturnNotReady);
  using Register=uint32_t(*)(void*,void*);void *request[]={nullptr,(void*)receiver};
  assert(!reinterpret_cast<Register>(mem+0x69dce)(&controller,request));
  assert(open(&client)==result&&client==&controller);munmap(mem,length);
  puts("PASS exact donor client tail call, registration and 512 guarded/native event cases");
 }
 puts("PASS production AGDC admission, retry without FB mutation, passthrough, start/stop publication and installer refusals");
}
'''.replace('FUNCTIONS',functions)
for old,key in [('CLIENT_ENTRY','clientEntry'),('CALLBACK_ENTRY','callbackEntry'),('DISPATCH','dispatch')]:code=code.replace(old,arrays[key])
with tempfile.TemporaryDirectory(prefix='display13-agdc-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+sys.argv[1:],check=True)
