#!/usr/bin/env python3
"""Execute production mode/update/cursor admission and stop serialization."""
from pathlib import Path
from production_source import read_source
import hashlib,re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1];s=read_source(root/'WhateverXe/kern_xe.cpp')
functions='\n'.join(re.search(r'(?:static bool|bool|uint32_t|void|int) (?:XE::)?'+name+r'\([^;{]*\{.*?\n\}',s,re.S)[0]
 for name in ['dsiCachePath','dsiLifecycleTarget','wrapDSISetDisplayMode','dsiNativeUpdateAllowed','dsiSharedWriterStopped','wrapDisplay13WMArmUpdate','beginDSINativeWriter','endDSINativeWriter','wrapDSICursorImage','wrapDSICursorState','beginDSIFramebufferWriter','wrapDSIFlipTransaction','wrapDSIGammaTransaction','wrapDSICscTransaction','wrapDSIPlaneGammaTransaction','wrapDSISetGammaTable','wrapDSISetAttribute','wrapDSISetConnectionAttribute','wrapDSISetPowerState','wrapDSISetStream'])
functions=functions.replace('void XE::endDSINativeWriter(int token) {', 'void XE::endDSINativeWriter(int token) {\n struct EndScope {EndScope(){endingWriter=true;}~EndScope(){endingWriter=false;}} endScope;')
scope=re.search(r'struct DSIModeScope \{.*?\n\};',s,re.S)[0]
if len(sys.argv)==2:
 binary=Path(sys.argv[1]).read_bytes()
 assert hashlib.sha256(binary).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 for name,width,count in [('processDSINativeWriters',20,2),('processDSINativeTransactions',17,5),('processDSINativeAttributes',20,4)]:
  installer=re.search(r'bool XE::'+name+r'\([^;{]*\{.*?\n\}',s,re.S)[0]
  offsets=[int(x,0) for x in re.search(r'offsets\[\]=\{([^}]+)',installer)[1].split(',')]
  entries=re.search(r'entries\[\]\['+str(width)+r'\]=\{(.*?)\};',installer,re.S)[1]
  entries=[bytes(int(x,0) for x in row.split(',')) for row in re.findall(r'\{([^}]+)\}',entries)]
  assert len(offsets)==len(entries)==count
  for offset,entry in zip(offsets,entries):assert binary[offset:offset+width]==entry,hex(offset)
code=r'''
#include "kern_display13_fb_memory.hpp"
#include "kern_dsi_apple_timing.hpp"
#include "kern_dsi_attributes.hpp"
#include "kern_display13_transcoder.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct OSObject {alignas(8) uint8_t bytes[0x4B90]{};unsigned size=sizeof(bytes);
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}};
using IOService=OSObject;
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *name,const void *data,unsigned bytes){
  assert(!strcmp(name,"WEXDSIModeRequest") && bytes==64);
  const auto words=static_cast<const uint64_t *>(data);assert(words[0]==1);
 }};
IOService *pendingFB=nullptr;bool dsiPendingTarget(IOService *fb){return fb && pendingFB==fb;}using IOLock=int;struct {bool modeReady=false;} dsiPendingBacking;
bool dsiPublishedTarget(IOService *fb){return dsiPendingTarget(fb) && dsiPendingBacking.modeReady;}
struct IORecursiveLock {unsigned depth=0;};
unsigned owner=0,modeCalls=0,armCalls=0,backingCalls=0;IORecursiveLock fbLock;bool expectDSI=false,endingWriter=false;
void IOLockLock(IOLock*){assert((fbLock.depth || endingWriter) && !owner);++owner;}void IOLockUnlock(IOLock*){assert(owner==1);--owner;}
void IORecursiveLockLock(IORecursiveLock *p){assert(!owner);++p->depth;}
void IORecursiveLockUnlock(IORecursiveLock *p){assert(!owner && p->depth);--p->depth;}
struct {IOLock *lock=nullptr;IOService *controller=nullptr;OSObject *dsiPort=nullptr;
 unsigned dsiNativeWriters=0;bool memoryFaulted=false,clockFaulted=false,watermarkFaulted=false;
 struct {struct {bool faulted=false,suspended=false;}state;}bridge;struct {bool faulted=false;}dbuf;
 struct {IOService *memoryFramebuffer=nullptr;bool destroyingPath=false;}pipe[3];
 Display13Transcoder::Bindings transcoders;}display13FBPower;
struct {uint32_t clientMode=0x10000;uintptr_t changingThread=0;bool presentationFault=false;}dsiFixedMode;
const XELP::DSI::AppleAdapter::Timing *dsiPresentation(uint32_t mode){
 static XELP::DSI::AppleAdapter::Timing t;
 return XELP::DSI::AppleAdapter::acceptsMode(mode,dsiFixedMode.clientMode)?&t:nullptr;
}
IOService *panel=nullptr,*nativeFB=nullptr;bool dsiModeTarget(IOService *f){return f && f==panel;}
constexpr uint32_t kIOReturnNotReady=0xe00002d8U,kIOReturnUnsupported=0xe00002c7U,kIOReturnSuccess=0;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
uint32_t nativeMode(IOService*,int32_t m,int32_t d){assert(!fbLock.depth && !owner);assert(m==0x10000 && d==2);++modeCalls;return 7;}
void adlnFault(bool &f,unsigned){f=true;}
unsigned imageCalls=0,stateCalls=0,expectedWriters=1;bool nestedCursor=false;
uint32_t nativeImage(IOService*,IOService*,void*);
uint32_t nativeState(IOService*,IOService*,int32_t,int32_t,bool,bool);
unsigned txCalls[5]{},txAnchor=0,paramAnchor=0;bool nestedTx=false;
constexpr uint64_t txOptions=0xFEDCBA9876543210ULL;
uint32_t nativeFlip(IOService*,void*,uint64_t,void*);
uint32_t nativeGamma(IOService*,void*,void*);
uint32_t nativeCsc(IOService*,void*,void*,uint64_t,bool);
uint32_t nativePlaneGamma(IOService*,void*,bool);
uint32_t nativeGammaTable(IOService*,uint32_t,uint32_t,uint32_t,void*,bool);
unsigned attrCalls[3]{},expectedAttribute=0,expectedPower=0;int32_t expectedConnection=0;
uintptr_t expectedValue=0;bool nestedAttribute=false;
uint32_t nativeAttribute(IOService*,uint32_t,uintptr_t);
uint32_t nativeConnectionAttribute(IOService*,int32_t,uint32_t,uintptr_t);
uint32_t nativePower(IOService*,uint32_t);
unsigned streamCalls=0;OSObject *expectedPath=nullptr;
uint32_t nativeStream(IOService*,IOService*,OSObject*,const void*);
struct XE {
 struct {unsigned dsi=1;}adlnOutputWiring;bool dsiStopAttempted=false,dsiResourcesReady=true;
 bool dsiPowerTest=false;struct {bool sleeping=false,failed=false;}dsiRuntimePower;unsigned wakeCalls=0;bool wakeOK=false;
 bool dsiStopTest=false,dsiMemoryReleased=false,dsiDDIReleased=false,stopOK=false;unsigned stopCalls=0;
 struct {bool done=false;bool complete(){return done;}}dsiClockStop;
 struct {bool complete=false;}dsiPanelStop;
 bool quiesceDSIPath(IOService *ctl,IOService *fb,OSObject *path,bool retain=false){
  assert(fbLock.depth && !owner && ctl==display13FBPower.controller && fb==panel);
  assert(path==Display13FBMemory::singlePath<OSObject *>(fb));++stopCalls;
  if(stopOK && retain){dsiStopAttempted=true;dsiRuntimePower.sleeping=true;return true;}
  if(stopOK){dsiStopAttempted=true;dsiMemoryReleased=dsiDDIReleased=dsiPanelStop.complete=dsiClockStop.done=true;}
  return stopOK;
 }
 bool resumeDSIPath(IOService *ctl,IOService *fb,OSObject *path){
  assert(fbLock.depth && !owner && ctl==display13FBPower.controller && fb==panel && dsiRuntimePower.sleeping);
  assert(path==Display13FBMemory::singlePath<OSObject *>(fb));++wakeCalls;
  if(wakeOK){dsiRuntimePower.sleeping=false;dsiStopAttempted=false;}
  return wakeOK;
 }
 bool dsiPanelOnly=false,publicationReady=false;bool publishDSINativeMode(IOService*){return publicationReady;}
 uint32_t setDSIPresentation(IOService *fb,uint32_t mode,const XELP::DSI::AppleAdapter::Timing &){
  assert(fbLock.depth && !owner);memcpy(reinterpret_cast<uint8_t *>(fb)+0x4250,&mode,sizeof(mode));return 0;}
 bool dsiNativeWriterHooksReady=true;uintptr_t orgDSICursor[2]{};
 uintptr_t orgDSIMode[6]{};
 static void recordDSIClient(IOService*,unsigned,uint64_t,uint32_t){};
 static void recordDSIAttribute(IOService*,unsigned,uint32_t,uintptr_t,uint32_t){};
 uintptr_t orgDSIAttributes[4]{};
 static uint32_t wrapDSISetStream(IOService*,IOService*,OSObject*,const void*);
 static uint32_t wrapDSISetAttribute(IOService*,uint32_t,uintptr_t);
 uint32_t saveDSIBrightness(IOService*,uintptr_t value){assert(fbLock.depth && !owner);return value<=UINT32_MAX?0U:kIOReturnUnsupported;}
 uint32_t setDSIBrightness(IOService*,uintptr_t value){assert(fbLock.depth && !owner);return value<=65535?0U:kIOReturnUnsupported;}
 static uint32_t wrapDSISetConnectionAttribute(IOService*,int32_t,uint32_t,uintptr_t);
 static uint32_t wrapDSISetPowerState(IOService*,uint32_t);
 uintptr_t orgDSITransaction[5]{};
 static int beginDSIFramebufferWriter(IOService*);
 static uint32_t wrapDSIFlipTransaction(IOService*,void*,uint64_t,void*);
 static uint32_t wrapDSIGammaTransaction(IOService*,void*,void*);
 static uint32_t wrapDSICscTransaction(IOService*,void*,void*,uint64_t,bool);
 static uint32_t wrapDSIPlaneGammaTransaction(IOService*,void*,bool);
 static uint32_t wrapDSISetGammaTable(IOService*,uint32_t,uint32_t,uint32_t,void*,bool);
 static int beginDSINativeWriter(IOService*,IOService*);
 static void endDSINativeWriter(int);
 static uint32_t wrapDSICursorImage(IOService*,IOService*,void*);
 static uint32_t wrapDSICursorState(IOService*,IOService*,int32_t,int32_t,bool,bool);
 static uint32_t wrapDSISetDisplayMode(IOService*,int32_t,int32_t);
 void probeDSINativeBacking(IOService*){assert(fbLock.depth && !owner);++backingCalls;}
 static bool dsiNativeUpdateAllowed(IOService*);
 static bool dsiSharedWriterStopped(IOService*,unsigned);
 static void wrapDisplay13WMArmUpdate(IOService*);
 static void display13WMArmUpdate(IOService*){assert(!owner);assert(bool(fbLock.depth)==expectDSI);++armCalls;}
}xe,*callbackXE=&xe;
SCOPE
FUNCTIONS
uint32_t nativeState(IOService *ctl,IOService *fb,int32_t x,int32_t y,bool visible,bool commit){
 assert(!fbLock.depth && !owner);assert(ctl==display13FBPower.controller && fb==nativeFB);
 assert(x==-17 && y==42 && visible && !commit);assert(display13FBPower.dsiNativeWriters==expectedWriters);
 ++stateCalls;return 0xe0000001U;
}
uint32_t nativeImage(IOService *ctl,IOService *fb,void *info){
 assert(!fbLock.depth && !owner);assert(ctl==display13FBPower.controller && fb==nativeFB && info==&imageCalls);
 assert(display13FBPower.dsiNativeWriters==expectedWriters);++imageCalls;
 if(nestedCursor){++expectedWriters;assert(XE::wrapDSICursorState(ctl,fb,-17,42,true,false)==0xe0000001U);
  --expectedWriters;assert(display13FBPower.dsiNativeWriters==expectedWriters);}
 return 0xe0000002U;
}
uint32_t txResult(unsigned i){return i==3?0:0xe0000110U+i;}
uint32_t txLeaf(unsigned i,IOService *fb,void *tx){
 assert(fb==nativeFB && tx==&txAnchor && !owner && !fbLock.depth);
 assert(display13FBPower.dsiNativeWriters==expectedWriters);++txCalls[i];return txResult(i);
}
uint32_t nativeFlip(IOService *fb,void *tx,uint64_t options,void *params){
 assert(options==txOptions && params==&paramAnchor);const auto result=txLeaf(0,fb,tx);
 if(nestedTx){++expectedWriters;assert(XE::wrapDSICscTransaction(fb,tx,params,options,true)==txResult(2));--expectedWriters;
  assert(display13FBPower.dsiNativeWriters==expectedWriters);}
 return result;
}
uint32_t nativeGamma(IOService *fb,void *tx,void *params){assert(params==&paramAnchor);return txLeaf(1,fb,tx);}
uint32_t nativeCsc(IOService *fb,void *tx,void *matrix,uint64_t options,bool final){
 assert(matrix==&paramAnchor && options==txOptions && final);return txLeaf(2,fb,tx);}
uint32_t nativePlaneGamma(IOService *fb,void *tx,bool post){assert(!post);return txLeaf(3,fb,tx);}
uint32_t nativeGammaTable(IOService *fb,uint32_t channels,uint32_t count,uint32_t bits,void *data,bool sync){
 assert(channels==3 && count==1024 && bits==16 && data==&paramAnchor && sync);return txLeaf(4,fb,&txAnchor);}
uint32_t callTx(unsigned i,IOService *fb){
 switch(i){
 case 0:return XE::wrapDSIFlipTransaction(fb,&txAnchor,txOptions,&paramAnchor);
 case 1:return XE::wrapDSIGammaTransaction(fb,&txAnchor,&paramAnchor);
 case 2:return XE::wrapDSICscTransaction(fb,&txAnchor,&paramAnchor,txOptions,true);
 case 3:return XE::wrapDSIPlaneGammaTransaction(fb,&txAnchor,false);
 case 4:return XE::wrapDSISetGammaTable(fb,3,1024,16,&paramAnchor,true);
 }assert(false);return 0;
}
uint32_t nativeAttribute(IOService *fb,uint32_t attribute,uintptr_t value){
 assert(fb==nativeFB && attribute==expectedAttribute && value==expectedValue && !owner && !fbLock.depth);
 assert(display13FBPower.dsiNativeWriters==expectedWriters);++attrCalls[0];
 if(nestedAttribute){++expectedWriters;assert(callTx(0,fb)==txResult(0));--expectedWriters;
  assert(display13FBPower.dsiNativeWriters==expectedWriters);}
 return 0xe0000121U;
}
uint32_t nativeConnectionAttribute(IOService *fb,int32_t connection,uint32_t attribute,uintptr_t value){
 assert(fb==nativeFB && connection==expectedConnection && attribute==expectedAttribute && value==expectedValue && !owner && !fbLock.depth);
 assert(display13FBPower.dsiNativeWriters==expectedWriters);++attrCalls[1];return 0xe0000122U;
}
uint32_t nativePower(IOService *fb,uint32_t state){
 assert(fb==nativeFB && state==expectedPower && !owner && !fbLock.depth);++attrCalls[2];return 0xe0000123U;
}
uint32_t nativeStream(IOService *ctl,IOService *fb,OSObject *path,const void *timing){
 assert(fb==nativeFB && ctl==display13FBPower.controller && path==expectedPath && timing==&paramAnchor && !fbLock.depth && !owner);
 ++streamCalls;return 0xe0000124U;
}
template<class T>void put(void *p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
struct Case {
 IOService fb,ctl,peer;OSObject path,port;IOLock lock;
 Case(){assert(!fbLock.depth && !owner);expectedWriters=1;nestedCursor=nestedTx=nestedAttribute=false;xe={};dsiFixedMode={};
  xe.orgDSIAttributes[0]=reinterpret_cast<uintptr_t>(nativeAttribute);
  xe.orgDSIAttributes[1]=reinterpret_cast<uintptr_t>(nativeConnectionAttribute);
  xe.orgDSIAttributes[2]=reinterpret_cast<uintptr_t>(nativePower);
  xe.orgDSIAttributes[3]=reinterpret_cast<uintptr_t>(nativeStream);
  xe.orgDSITransaction[0]=reinterpret_cast<uintptr_t>(nativeFlip);
  xe.orgDSITransaction[1]=reinterpret_cast<uintptr_t>(nativeGamma);
  xe.orgDSITransaction[2]=reinterpret_cast<uintptr_t>(nativeCsc);
  xe.orgDSITransaction[3]=reinterpret_cast<uintptr_t>(nativePlaneGamma);
  xe.orgDSITransaction[4]=reinterpret_cast<uintptr_t>(nativeGammaTable);
  xe.orgDSICursor[0]=reinterpret_cast<uintptr_t>(nativeImage);xe.orgDSICursor[1]=reinterpret_cast<uintptr_t>(nativeState);xe.orgDSIMode[5]=reinterpret_cast<uintptr_t>(nativeMode);display13FBPower={};auto &c=display13FBPower;
  c.lock=&lock;c.controller=&ctl;c.dsiPort=&port;c.pipe[0].memoryFramebuffer=&fb;c.transcoders.dsi0Pipe=0;panel=nativeFB=&fb;
  put(&fb,0x4220,&fbLock);put(&fb,0x1D0,&ctl);put(&fb,0x4A00,1U);put(&fb,0x4A08,&path);
  put(&path,0x3640,0U);put(&path,0x3644,2U);put(&path,0x32B0,&fb);put(&path,0x3648,&port);put(&port,0x550,1U);
  expectDSI=true;
 }
 void refuseCursor(){auto i=imageCalls,s=stateCalls;
  assert(XE::wrapDSICursorImage(&ctl,&fb,&imageCalls)==kIOReturnNotReady);
  assert(XE::wrapDSICursorState(&ctl,&fb,-17,42,true,false)==kIOReturnNotReady);
  assert(i==imageCalls && s==stateCalls && !fbLock.depth && !owner);
 }
 void refuse(){const auto calls=armCalls;XE::wrapDisplay13WMArmUpdate(&fb);assert(calls==armCalls && !fbLock.depth && !owner);}
};
int main(){

 for(unsigned attr:{0x706F7772U,0x70777273U}){Case c;xe.dsiPowerTest=true;put(&c.fb,0x4284,2U);
  assert(XE::wrapDSISetAttribute(&c.fb,attr,1)==kIOReturnNotReady && xe.stopCalls==1);
  assert(Display13FBMemory::field<uint32_t>(&c.fb,0x4284)==2);
  xe.stopOK=true;
  for(unsigned cycle=0;cycle<3;++cycle){
   assert(XE::wrapDSISetAttribute(&c.fb,attr,1)==kIOReturnSuccess && xe.dsiRuntimePower.sleeping);
   auto stops=xe.stopCalls;
   assert(XE::wrapDSISetAttribute(&c.fb,attr,0)==kIOReturnSuccess && xe.stopCalls==stops);
   assert(Display13FBMemory::field<uint32_t>(&c.fb,0x4284)==0 && !xe.dsiMemoryReleased && !xe.dsiDDIReleased);
   c.refuse();c.refuseCursor();xe.wakeOK=false;
   assert(XE::wrapDSISetAttribute(&c.fb,attr,2)==kIOReturnNotReady);
   assert(Display13FBMemory::field<uint32_t>(&c.fb,0x4284)==0);
   xe.wakeOK=true;assert(XE::wrapDSISetAttribute(&c.fb,attr,2)==kIOReturnSuccess);
   assert(!xe.dsiRuntimePower.sleeping && !xe.dsiStopAttempted && Display13FBMemory::field<uint32_t>(&c.fb,0x4284)==2);
   auto wakes=xe.wakeCalls;assert(XE::wrapDSISetAttribute(&c.fb,attr,2)==kIOReturnSuccess && xe.wakeCalls==wakes);
  }
  xe.dsiRuntimePower.failed=true;auto calls=xe.stopCalls;
  assert(XE::wrapDSISetAttribute(&c.fb,attr,1)==kIOReturnNotReady && xe.stopCalls==calls);
 }

 for(unsigned attr:{0x706F7772U,0x70777273U})for(unsigned state:{0U,1U}){Case c;xe.dsiStopTest=true;put(&c.fb,0x4284,2U);
  assert(XE::wrapDSISetAttribute(&c.fb,attr,state)==kIOReturnNotReady && xe.stopCalls==1);
  assert(Display13FBMemory::field<uint32_t>(&c.fb,0x4284)==2);
  xe.stopOK=true;assert(XE::wrapDSISetAttribute(&c.fb,attr,state)==kIOReturnSuccess && xe.stopCalls==2);
  assert(Display13FBMemory::field<uint32_t>(&c.fb,0x4284)==state);
  assert(XE::wrapDSISetAttribute(&c.fb,attr,state)==kIOReturnSuccess && xe.stopCalls==2);
  assert(XE::wrapDSISetAttribute(&c.fb,attr,2)==kIOReturnUnsupported && xe.stopCalls==2);
 }

 {Case c;auto before=backingCalls;
  assert(XE::wrapDSISetDisplayMode(&c.fb,0x10000,0)==kIOReturnNotReady && backingCalls==before+1);
  assert(XE::wrapDSISetDisplayMode(&c.fb,0x10000,2)==kIOReturnNotReady && backingCalls==before+1);
  assert(XE::wrapDSISetDisplayMode(&c.fb,0x10001,0)==kIOReturnNotReady && backingCalls==before+1);
  xe.dsiStopAttempted=true;
  assert(XE::wrapDSISetDisplayMode(&c.fb,0x10000,0)==kIOReturnNotReady && backingCalls==before+1);}

 {Case c;dsiFixedMode.clientMode=0x80001000;xe.publicationReady=true;
  assert(!XE::wrapDSISetDisplayMode(&c.fb,static_cast<int32_t>(0x80001000),0));
  assert(Display13FBMemory::field<uint32_t>(&c.fb,0x4250)==0x80001000);
  assert(XE::wrapDSISetDisplayMode(&c.fb,static_cast<int32_t>(0x80001001),0)==kIOReturnNotReady);}
 // Unsupported depth never reaches ordinary mode writes.
 {Case c;auto before=modeCalls;assert(XE::wrapDSISetDisplayMode(&c.fb,0x10000,2)==kIOReturnNotReady);
  assert(modeCalls==before && !fbLock.depth && !owner);}
 {Case c;auto before=armCalls;XE::wrapDisplay13WMArmUpdate(&c.fb);assert(armCalls==before+1 && !fbLock.depth);}
 {Case c;xe.dsiStopAttempted=true;c.refuse();}
 {Case c;xe.dsiResourcesReady=false;c.refuse();}
 {Case c;display13FBPower.watermarkFaulted=true;c.refuse();}
 {Case c;display13FBPower.memoryFaulted=true;c.refuse();}
 {Case c;display13FBPower.bridge.state.suspended=true;c.refuse();}
 {Case c;display13FBPower.pipe[0].destroyingPath=true;c.refuse();}
 {Case c;display13FBPower.transcoders={};c.refuse();}
 {Case c;display13FBPower.pipe[0].memoryFramebuffer=nullptr;c.refuse();}
 {Case c;put(&c.path,0x3644,1U);c.refuse();}
 {Case c;put(&c.path,0x3640,1U);c.refuse();}
 {Case c;put(&c.path,0x32B0,&c.peer);c.refuse();}
 {Case c;put(&c.port,0x550,2U);c.refuse();}
 {Case c;put(&c.fb,0x4A00,0U);c.refuse();}
 {Case c;c.path.size=0x100;c.refuse();}
 {Case c;put(&c.fb,0x4220,static_cast<IORecursiveLock*>(nullptr));c.refuse();}
 // Real external identity stays admitted during DSI stop/fault. The existing
 // body gets its original return/arguments; DSI does not poison peer updates.
 {Case c;panel=nullptr;put(&c.path,0x3640,1U);put(&c.port,0x120,3U);xe.dsiStopAttempted=true;display13FBPower.memoryFaulted=true;
  auto a=armCalls,m=modeCalls;expectDSI=false;XE::wrapDisplay13WMArmUpdate(&c.fb);
  assert(armCalls==a+1 && XE::wrapDSISetDisplayMode(&c.fb,0x10000,2)==7 && modeCalls==m+1 && !fbLock.depth);}
 // Without DSI, forward unchanged even when this adapter cannot interpret FB.
 {Case c;xe.adlnOutputWiring.dsi=0;auto a=armCalls,m=modeCalls;expectDSI=false;
  XE::wrapDisplay13WMArmUpdate(nullptr);assert(armCalls==a+1);
  assert(XE::wrapDSISetDisplayMode(nullptr,0x10000,2)==7 && modeCalls==m+1);}
 // Shared callback policy is read under the owner lock. A retired DSI
 // cannot revive through pipe A, retained FB identity, or an unknown pipe.
 {Case c;IORecursiveLockLock(&fbLock);IOLockLock(display13FBPower.lock);
  assert(!XE::dsiSharedWriterStopped(&c.fb,0));xe.dsiStopAttempted=true;
  assert(XE::dsiSharedWriterStopped(&c.fb,0) && XE::dsiSharedWriterStopped(&c.fb,1));
  assert(XE::dsiSharedWriterStopped(&c.peer,UINT32_MAX));
  assert(!XE::dsiSharedWriterStopped(&c.peer,1) && !XE::dsiSharedWriterStopped(&c.peer,2));
  xe.adlnOutputWiring.dsi=0;assert(!XE::dsiSharedWriterStopped(&c.fb,0));
  assert(!display13FBPower.memoryFaulted);IOLockUnlock(display13FBPower.lock);IORecursiveLockUnlock(&fbLock);}
 // Actual writer wrappers count nesting and release counts on native errors.
 {Case c;nestedCursor=true;auto i=imageCalls,s=stateCalls;
  assert(XE::wrapDSICursorImage(&c.ctl,&c.fb,&imageCalls)==0xe0000002U);
  assert(imageCalls==i+1 && stateCalls==s+1 && !display13FBPower.dsiNativeWriters);}
 {Case c;assert(XE::wrapDSICursorState(&c.ctl,&c.fb,-17,42,true,false)==0xe0000001U);
  assert(!display13FBPower.dsiNativeWriters);}
 {Case c;xe.dsiStopAttempted=true;c.refuseCursor();}
 {Case c;xe.dsiResourcesReady=false;c.refuseCursor();}
 {Case c;xe.dsiNativeWriterHooksReady=false;c.refuseCursor();}
 {Case c;display13FBPower.memoryFaulted=true;c.refuseCursor();}
 {Case c;pendingFB=&c.fb;c.refuseCursor();pendingFB=nullptr;}
 {Case c;display13FBPower.dsiNativeWriters=UINT32_MAX;c.refuseCursor();
  assert(display13FBPower.dsiNativeWriters==UINT32_MAX);}
 {Case c;auto i=imageCalls;assert(XE::wrapDSICursorImage(&c.peer,&c.fb,&imageCalls)==kIOReturnNotReady);
  assert(i==imageCalls && !display13FBPower.dsiNativeWriters);}
 {Case c;put(&c.fb,0x4220,static_cast<IORecursiveLock*>(nullptr));c.refuseCursor();}
 {Case c;panel=nullptr;put(&c.path,0x3640,1U);put(&c.port,0x120,3U);
  xe.dsiStopAttempted=true;expectedWriters=0;
  assert(XE::wrapDSICursorState(&c.ctl,nullptr,-17,42,true,false)==kIOReturnNotReady);
  assert(XE::wrapDSICursorState(&c.ctl,&c.fb,-17,42,true,false)==0xe0000001U);
  assert(!display13FBPower.dsiNativeWriters);
  panel=&c.fb; // Identity predicate is irrelevant for disabled DSI.
  xe.adlnOutputWiring.dsi=0;
  assert(XE::wrapDSICursorImage(&c.ctl,&c.fb,&imageCalls)==0xe0000002U);}
 for(unsigned i=0;i<5;++i){
  {Case c;auto before=txCalls[i];assert(callTx(i,&c.fb)==txResult(i));
   assert(txCalls[i]==before+1 && !display13FBPower.dsiNativeWriters && !owner && !fbLock.depth);}
  for(unsigned bad=0;bad<5;++bad){Case c;auto before=txCalls[i];
   if(bad==0)xe.dsiStopAttempted=true;
   if(bad==1)xe.dsiResourcesReady=false;
   if(bad==2)xe.dsiNativeWriterHooksReady=false;
   if(bad==3)display13FBPower.memoryFaulted=true;
   if(bad==4)put(&c.fb,0x1D0,&c.peer);
   assert(callTx(i,&c.fb)==kIOReturnNotReady && txCalls[i]==before && !display13FBPower.dsiNativeWriters);}
  {Case c;panel=nullptr;put(&c.path,0x3640,1U);put(&c.port,0x120,3U);xe.dsiStopAttempted=true;
   expectedWriters=0;assert(callTx(i,&c.fb)==txResult(i) && !display13FBPower.dsiNativeWriters);}
  {Case c;xe.adlnOutputWiring.dsi=0;nativeFB=nullptr;expectedWriters=0;
   assert(callTx(i,nullptr)==txResult(i) && !display13FBPower.dsiNativeWriters);}
 }
 {Case c;nestedTx=true;auto before=txCalls[2];assert(callTx(0,&c.fb)==txResult(0));
  assert(txCalls[2]==before+1 && !display13FBPower.dsiNativeWriters && !fbLock.depth && !owner);}
 // Ordinary OS attributes reach native; lifetime accounting spans
 // their nested flip/color work and preserves full pointer-sized values.
 for(unsigned attr:{0x77737276U,0x63617064U,0x63727363U,0x706E6C70U,0x73484443U,0x2070656EU,0x20637463U,0xDEADBEEFU}){Case c;expectedAttribute=attr;expectedValue=txOptions;nestedAttribute=true;
  const auto before=attrCalls[0];assert(XE::wrapDSISetAttribute(&c.fb,attr,expectedValue)==0xe0000121U);
  assert(attrCalls[0]==before+1 && !display13FBPower.dsiNativeWriters && !owner && !fbLock.depth);}
 for(unsigned attr:{0x00647064U,0x72677363U,0x67677363U,0x62677363U,0x67736320U,0x666C7573U,0x76626C6DU,0x64666C67U,0x00677264U,0x20627063U,0x706F7772U,0xDEADBEEFU}){
  Case c;expectedAttribute=attr;expectedValue=txOptions;expectedConnection=0;
  assert(XE::wrapDSISetConnectionAttribute(&c.fb,0,attr,expectedValue)==0xe0000122U && !display13FBPower.dsiNativeWriters);
  assert(XE::wrapDSISetConnectionAttribute(&c.fb,1,attr,expectedValue)==kIOReturnUnsupported);}
 // Transport-only routes remain blocked, independently of the selector
 // namespace. Native/IOGraphics handles all other attributes and return values.
 for(unsigned attr:{0x68646370U,0x73707772U}) {
  Case c;const auto before=attrCalls[0];const auto original=c.fb;
  assert(XE::wrapDSISetAttribute(&c.fb,attr,0)==kIOReturnUnsupported);
  assert(attrCalls[0]==before && !memcmp(&c.fb,&original,sizeof(c.fb)));}
 for(unsigned attr:{0x706E6C74U,0x00696772U,0x7073723FU,0x70726F62U,0x64707461U,0x7073636EU,
                    0x00617572U,0x61756D73U,0x61757063U}) {
  Case c;const auto before=attrCalls[1];const auto original=c.fb;
  assert(XE::wrapDSISetConnectionAttribute(&c.fb,0,attr,0)==kIOReturnUnsupported);
  assert(attrCalls[1]==before && !memcmp(&c.fb,&original,sizeof(c.fb)) && !display13FBPower.memoryFaulted);}
 {Case c;xe.dsiStopAttempted=true;const auto before=attrCalls[0];
  assert(XE::wrapDSISetAttribute(&c.fb,0x626B7376,123)==0);
  assert(XE::wrapDSISetAttribute(&c.fb,0x626B7376,0x100000001ULL)==kIOReturnUnsupported);
  assert(attrCalls[0]==before && !display13FBPower.dsiNativeWriters);}
 {Case c;const auto before=attrCalls[1];
  assert(XE::wrapDSISetConnectionAttribute(&c.fb,0,0x626B6C74,12345)==0);
  assert(XE::wrapDSISetConnectionAttribute(&c.fb,0,0x626B6C74,0x100000001ULL)==kIOReturnUnsupported);
  assert(XE::wrapDSISetConnectionAttribute(&c.fb,1,0x626B6C74,0)==kIOReturnUnsupported);
  assert(attrCalls[1]==before && !display13FBPower.dsiNativeWriters);}
 for(unsigned old=0;old<3;++old)for(unsigned state=0;state<4;++state){Case c;put(&c.fb,0x4284,old);const auto before=attrCalls[2];
  const auto expected=old==2 && state==2?kIOReturnSuccess:kIOReturnUnsupported;
  assert(XE::wrapDSISetPowerState(&c.fb,state)==expected && attrCalls[2]==before);
  assert(Display13FBMemory::field<uint32_t>(&c.fb,0x4284)==old);
  assert(XE::wrapDSISetAttribute(&c.fb,0x706F7772,state)==expected);}
 {Case c;put(&c.fb,0x4284,2U);assert(XE::wrapDSISetAttribute(&c.fb,0x706F7772,0x100000002ULL)==kIOReturnUnsupported);}
 for(unsigned bad=0;bad<4;++bad){Case c;put(&c.fb,0x4284,2U);const auto before=attrCalls[0]+attrCalls[1]+attrCalls[2];
  if(bad==0)xe.dsiStopAttempted=true;if(bad==1)xe.dsiResourcesReady=false;
  if(bad==2)xe.dsiNativeWriterHooksReady=false;if(bad==3)display13FBPower.memoryFaulted=true;
  assert(XE::wrapDSISetAttribute(&c.fb,0x77737276,1)==kIOReturnNotReady);
  assert(XE::wrapDSISetConnectionAttribute(&c.fb,0,0x72677363,1)==kIOReturnNotReady);
  assert(XE::wrapDSISetPowerState(&c.fb,2)==kIOReturnNotReady);
  assert(attrCalls[0]+attrCalls[1]+attrCalls[2]==before && !display13FBPower.dsiNativeWriters);}
 for(bool disabled:{false,true}){Case c;expectedWriters=0;expectedValue=txOptions;expectedAttribute=0x706F7772;expectedConnection=4;expectedPower=0;
  IOService *fb=&c.fb;
  if(disabled){xe.adlnOutputWiring.dsi=0;fb=nativeFB=nullptr;}
  else{panel=nullptr;put(&c.path,0x3640,1U);put(&c.port,0x120,3U);xe.dsiStopAttempted=true;}
  assert(XE::wrapDSISetAttribute(fb,expectedAttribute,expectedValue)==0xe0000121U);
  assert(XE::wrapDSISetConnectionAttribute(fb,expectedConnection,expectedAttribute,expectedValue)==0xe0000122U);
  assert(XE::wrapDSISetPowerState(fb,expectedPower)==0xe0000123U);}
 {Case c;auto before=streamCalls;assert(XE::wrapDSISetStream(&c.ctl,&c.fb,&c.path,&paramAnchor)==kIOReturnNotReady);
  assert(streamCalls==before && !fbLock.depth && !owner);}
 {Case c;panel=nullptr;put(&c.path,0x3640,1U);put(&c.port,0x120,3U);expectedPath=&c.path;
  assert(XE::wrapDSISetStream(&c.ctl,&c.fb,&c.path,&paramAnchor)==0xe0000124U);
  const auto before=streamCalls;assert(XE::wrapDSISetStream(&c.ctl,&c.fb,&c.port,&paramAnchor)==kIOReturnNotReady);
  assert(streamCalls==before);}
 {Case c;xe.adlnOutputWiring.dsi=0;nativeFB=nullptr;expectedPath=nullptr;
  assert(XE::wrapDSISetStream(&c.ctl,nullptr,nullptr,&paramAnchor)==0xe0000124U);}
 puts("PASS DSI public mode preflight and native update dispatch: stopped/faulted rejection, held FB serialization, nested cursor/transaction counts, transport-specific attribute/power admission, full-width arguments, native wait lock boundaries, peer and disabled-DSI forwarding");
}
'''.replace('SCOPE',scope).replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-update-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
