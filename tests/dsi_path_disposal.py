#!/usr/bin/env python3
"""Exercise production DSI PG/path disposal, optionally replay exact CPU leaves."""
from pathlib import Path
from production_source import read_source
import hashlib,re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
body=re.search(r'bool XE::disposeDSIPath\(.*?\n\}',s,re.S)[0]
code=r'''
#include "kern_dsi_quiesce.hpp"
#include "kern_dsi_clock.hpp"
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_power_bridge.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <new>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
using namespace XELP::DSI;
struct OSObject {alignas(8) uint8_t bytes[0x4B90]{};unsigned size=sizeof(bytes),refs=1;
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}
 void retain(){++refs;}void release(){assert(refs>1);--refs;}};
using IOService=OSObject;using IOLock=int;
unsigned depth=0;bool gate=false,busy=false,queueOnPut=false;unsigned opens=0,closes=0,putCalls=0,leaves=0;bool failPut=false,liePut=false,failLeaf=false,failTiming=false;
void IOLockLock(IOLock*){assert(!depth);++depth;}void IOLockUnlock(IOLock*){assert(depth==1);--depth;}
struct IOWorkLoop:OSObject{};
bool inGate(const IOWorkLoop*){assert(!depth);return gate;}
bool tryClose(IOWorkLoop*){assert(!depth);++closes;return !busy && (gate=true);}
void open(IOWorkLoop*){assert(!depth && gate);gate=false;++opens;}
struct {IOLock *lock=nullptr;IOService *controller=nullptr;IOWorkLoop *workLoop=nullptr;OSObject *dsiPort=nullptr;void *power=nullptr;
 unsigned dsiNativeWriters=0;bool memoryFaulted=false,clockFaulted=false;Display13Power::NativeBridge bridge;Display13DBUF::State dbuf;
 struct {IOService *memoryFramebuffer=nullptr;bool destroyingPath=true;uintptr_t publishedScanout=0;struct {uintptr_t record=0;}scanout;}pipe[3];
}display13FBPower;
uint64_t receipt[14]{};
struct {IOLock *lock=nullptr;Display13FBIRQ::State state;Display13FBIRQ::HandlerScopes scopes;
 bool quiescing=false;unsigned blockedPipes=0,admittedPipes=2,handlers=0,softwareHandlers=0;}display13FBIRQ;
bool adlnIRQOwner(IOService *c){return c==display13FBPower.controller;}
bool IOSimpleLockLockDisableInterrupt(IOLock*){assert(depth==1);++depth;return true;}
void IOSimpleLockUnlockEnableInterrupt(IOLock*,bool){assert(depth==2);--depth;}
void adlnPublishMemory(const char*,unsigned,void *p,size_t n){assert(!depth && n==sizeof(receipt));memcpy(receipt,p,n);}
template<class T>void put(void *p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
IOService *mapEntries[3]{},**pipeMap=mapEntries;
void freeScaler(IOService*,OSObject *s){assert(depth && gate);if(s){put(s,0x10,uintptr_t(0));put(s,0x20,uint8_t(0));}}
void freePlane(IOService *c,OSObject *p){++leaves;assert(depth && gate);freeScaler(c,Display13FBMemory::field<OSObject*>(p,0x88));put(p,0x68,uintptr_t(0));}
void detach(IOService *fb,OSObject*){assert(depth && gate);put(fb,0x4A00,0U);put(fb,0x4A08,uintptr_t(0));}
void removePath(IOService *fb,OSObject *p){assert(depth && gate);if(failLeaf)return;pipeMap[0]=nullptr;
 put(fb,0x49E8,0U);put(fb,0x49F0,uintptr_t(0));put(p,0x32B0,uintptr_t(0));put(p,0x3644,0U);}
struct XE {
 uintptr_t dsiFreePlane=reinterpret_cast<uintptr_t>(freePlane),dsiFreeScaler=reinterpret_cast<uintptr_t>(freeScaler),
 dsiDetachPath=reinterpret_cast<uintptr_t>(detach),dsiRemovePath=reinterpret_cast<uintptr_t>(removePath),dsiPipeMap=reinterpret_cast<uintptr_t>(&pipeMap);
 uintptr_t adlnWorkLoopInGate=reinterpret_cast<uintptr_t>(inGate),adlnWorkLoopTryClose=reinterpret_cast<uintptr_t>(tryClose),adlnWorkLoopOpen=reinterpret_cast<uintptr_t>(open);
 struct {unsigned dsi=1;}adlnOutputWiring;IOService *adlnTopologyController=nullptr;
 bool dsiIRQParked=true,dsiDDIReleased=true,dsiMemoryReleased=true,dsiPGReleased=false;
 Quiesce::Result dsiStop;Clock::Result dsiClockStop;
 static bool adlnPowerReference(void *p,unsigned op,unsigned domain){
  assert(!depth && gate && p==display13FBPower.power && op==2 && domain==1);++putCalls;
  if(queueOnPut)*reinterpret_cast<uint32_t*>(display13FBPower.controller->bytes+0xE08)=1;
  if(failPut)return false;if(liePut)return true;
  // Real NativeBridge balances the borrowed firmware PG1, preserving PG3.
  struct IO {uint32_t read(uint32_t r){return r==0x45404?0xC0000003U:0;}bool readable(uint32_t){return true;}
   void write(uint32_t,uint32_t){assert(false);}void delayUs(unsigned){assert(false);}}io;
  return display13FBPower.bridge.putPG(io,1).status==Display13Power::Status::Complete;
 }
 bool releaseDSIFixedMode(IOService*){assert(!depth && gate);return !failTiming;}
 bool disposeDSIPath(IOService*,OSObject*);
};
BODY
uint8_t *image=nullptr;
struct Case {
 XE xe;IOService controller,fb,peer;OSObject path,port,plane,scaler,planeScaler;IOWorkLoop loop;IOLock lock;
 Case(){
  using Power=decltype(display13FBPower);display13FBPower.~Power();new (&display13FBPower) Power;auto &c=display13FBPower;c.lock=&lock;c.controller=&controller;c.workLoop=&loop;c.dsiPort=&port;c.power=&controller;
  port.retain();c.pipe[0].memoryFramebuffer=&fb;c.pipe[0].destroyingPath=true;c.dbuf.selected=2;
  c.bridge.begun=true;c.bridge.bootAdopted=true;c.bridge.bootPath=reinterpret_cast<uintptr_t>(&path);c.bridge.bootPort=0;
  c.bridge.pgRefs[0]=1;c.bridge.pgRefs[2]=1;
  auto &state=c.bridge.state;state.ready=true;state.allowed=Display13Power::All;
  const auto pg=Display13Power::dependencies(Display13Power::nativeScope(1)),peerPG=Display13Power::dependencies(Display13Power::nativeScope(3));
  state.borrowed=pg|peerPG;
  for(unsigned i=0;i<Display13Power::Count;++i)state.refs[i]=unsigned(bool(pg&(1U<<i)))+unsigned(bool(peerPG&(1U<<i)));
  c.bridge.pgLease[0].owner=&state;c.bridge.pgLease[0].domains=pg;
  c.bridge.pgLease[2].owner=&state;c.bridge.pgLease[2].domains=peerPG;
  xe.adlnTopologyController=&controller;xe.dsiStop.stage=Quiesce::Stage::Complete;xe.dsiStop.error=Quiesce::Error::None;
  xe.dsiClockStop.status=Clock::Status::Complete;
  put(&controller,0xE00,&loop);put(&fb,0x1D0,&controller);put(&fb,0x4288,uint8_t(0xFF));
  put(&fb,0x49E8,1U);put(&fb,0x49F0,&path);put(&fb,0x4A00,1U);put(&fb,0x4A08,&path);
  put(&path,0x32B0,&fb);put(&path,0x32C8,&plane);put(&path,0x32C0,&scaler);put(&path,0x3644,1U);put(&path,0x3648,&port);
  put(&port,0x550,1U);put(&port,0x554,3U);put(&plane,0x68,&fb);put(&plane,0x70,&path);put(&plane,0x88,&planeScaler);
  for(auto s:{&scaler,&planeScaler}){put(s,0x10,&fb);put(s,0x20,uint8_t(1));}
  pipeMap=mapEntries;pipeMap[0]=&fb;pipeMap[1]=&peer;pipeMap[2]=nullptr;
  display13FBIRQ={};display13FBIRQ.lock=&lock;display13FBIRQ.state.active=2;
  depth=0;queueOnPut=gate=busy=failPut=liePut=failLeaf=failTiming=false;opens=closes=putCalls=leaves=0;
  if(image){xe.dsiFreePlane=reinterpret_cast<uintptr_t>(image+0x5751C);xe.dsiFreeScaler=reinterpret_cast<uintptr_t>(image+0x5739A);
   xe.dsiDetachPath=reinterpret_cast<uintptr_t>(image+0x67FB4);xe.dsiRemovePath=reinterpret_cast<uintptr_t>(image+0x68EA4);
   put(image,0x138BB0,pipeMap);xe.dsiPipeMap=reinterpret_cast<uintptr_t>(image+0x138BB0);}
 }
 void retained(){assert(!xe.disposeDSIPath(&fb,&path));assert(!putCalls && !leaves && !xe.dsiPGReleased);
  assert(Display13FBMemory::field<OSObject*>(&fb,0x49F0)==&path && pipeMap[0]==&fb && loop.refs==1 && !depth && !gate);}
 void success(){assert(xe.disposeDSIPath(&fb,&path));assert(xe.dsiPGReleased && putCalls==1 && loop.refs==1 && !depth);
  assert(!pipeMap[0] && pipeMap[1]==&peer && !display13FBPower.bridge.pgRefs[0] && display13FBPower.bridge.pgRefs[2]==1);
  assert(Display13FBMemory::field<uint32_t>(&port,0x554)==2 && !Display13FBMemory::field<uintptr_t>(&plane,0x68));
  assert(!xe.disposeDSIPath(&fb,&path) && xe.dsiPGReleased && putCalls==1);}
};
void run(){
 {Case c;c.success();assert(opens==closes && !gate);}
 {Case c;gate=true;c.success();assert(!opens && !closes && gate);gate=false;}
 {Case c;busy=true;c.retained();}
 {Case c;c.xe.dsiMemoryReleased=false;c.retained();}
 {Case c;failTiming=true;assert(!c.xe.disposeDSIPath(&c.fb,&c.path));
  assert(display13FBPower.dsiPort==&c.port && c.port.refs==2 && c.xe.dsiPGReleased);}

 {Case c;display13FBPower.memoryFaulted=true;c.retained();}
 {Case c;display13FBPower.dsiNativeWriters=1;c.retained();}
 {Case c;display13FBIRQ.state.active|=1;c.retained();}
 {Case c;display13FBIRQ.softwareHandlers=1;c.retained();}
 {Case c;put(&c.controller,0xE08,1U);c.retained();}
 {Case c;put(&c.controller,0xE10,1U);c.retained();}
 {Case c;put(&c.controller,0xE08,Display13FBIRQ::pipeEvents(1));put(&c.controller,0xE10,0xC0000U);c.success();}
 {Case c;queueOnPut=true;assert(!c.xe.disposeDSIPath(&c.fb,&c.path));assert(c.xe.dsiPGReleased && pipeMap[0]==&c.fb && !leaves);}
 {Case c;display13FBPower.dbuf.selected|=1;c.retained();}
 {Case c;display13FBPower.bridge.pgRefs[0]=2;c.retained();}
 {Case c;display13FBPower.bridge.bootPGProtected=true;c.retained();}
 {Case c;put(&c.path,0x3644,2U);c.retained();}
 {Case c;put(&c.path,0x32B8,&c.peer);c.retained();}
 {Case c;put(&c.planeScaler,0x10,&c.peer);c.retained();}
 {Case c;put(&c.fb,0x49E8,2U);c.retained();}
 {Case c;put(&c.fb,0x4A08,&c.peer);c.retained();}
 {Case c;put(&c.fb,0x4A00,0U);c.retained();}
 {Case c;c.controller.size=0x100;c.retained();}
 {Case c;put(&c.fb,0x4A00,0U);put(&c.fb,0x4A08,uintptr_t(0));c.success();}
 {Case c;put(&c.path,0x32D0,&c.peer);c.retained();}
 {Case c;failPut=true;assert(!c.xe.disposeDSIPath(&c.fb,&c.path));assert(putCalls==1 && !c.xe.dsiPGReleased && !leaves && pipeMap[0]==&c.fb);}
 {Case c;liePut=true;assert(!c.xe.disposeDSIPath(&c.fb,&c.path));assert(putCalls==1 && !c.xe.dsiPGReleased && !leaves && pipeMap[0]==&c.fb);}
 if(!image){Case c;failLeaf=true;assert(!c.xe.disposeDSIPath(&c.fb,&c.path));assert(c.xe.dsiPGReleased && pipeMap[0]==&c.fb);}
}
int main(int argc,char **argv){
 run();
 if(argc==2){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> b{std::istreambuf_iterator<char>(f),{}};
  image=static_cast<uint8_t*>(mmap(nullptr,4*1024*1024,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));assert(image!=MAP_FAILED);
  memcpy(image,b.data(),b.size());
  // freePath -> resetSoftwareState remains native; only imported bzero is supplied.
  image[0x300000]=0x48;image[0x300001]=0xB8;put(image,0x300002,reinterpret_cast<uintptr_t>(bzero));image[0x30000A]=0xFF;image[0x30000B]=0xE0;
  for(unsigned call:{0x9161FU,0x91630U,0x91641U,0x91652U,0x9166CU})put(image,call+1,int32_t(0x300000-call-5));
  run();munmap(image,4*1024*1024);
 }
 std::puts("PASS production DSI PG/path disposal: ownership, gate, failed put retention, CPU arrays/scalers and peer preservation");
}
'''.replace('BODY',body)
args=[];arch=[]
if len(sys.argv)>1:
 donor=Path(sys.argv[1]).resolve();data=donor.read_bytes()
 assert hashlib.sha256(data).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 args=[str(donor)];arch=['-arch','x86_64']
with tempfile.TemporaryDirectory(prefix='dsi-dispose-') as d:
 source=Path(d)/'test.cpp';source.write_text(code);binary=Path(d)/'test'
 subprocess.run(['clang++',*arch,'-std=c++17','-O1','-I'+str(root/'WhateverXe'),str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary),*args],check=True)
