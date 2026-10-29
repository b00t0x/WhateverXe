#!/usr/bin/env python3
"""Execute production mode/update/cursor admission and stop serialization."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'WhateverXe/kern_xe.cpp').read_text()
functions='\n'.join(re.search(r'(?:static bool|bool|uint32_t|void|int) (?:XE::)?'+name+r'\(.*?\n\}',s,re.S)[0]
 for name in ['dsiCachePath','dsiLifecycleTarget','wrapDSISetDisplayMode','dsiNativeUpdateAllowed','wrapDisplay13WMArmUpdate','beginDSINativeWriter','endDSINativeWriter','wrapDSICursorImage','wrapDSICursorState'])
functions=functions.replace('void XE::endDSINativeWriter(int token) {', 'void XE::endDSINativeWriter(int token) {\n struct EndScope {EndScope(){endingWriter=true;}~EndScope(){endingWriter=false;}} endScope;')
scope=re.search(r'struct DSIModeScope \{.*?\n\};',s,re.S)[0]
if len(sys.argv)==2:
 binary=Path(sys.argv[1]).read_bytes()
 assert hashlib.sha256(binary).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 installer=re.search(r'bool XE::processDSINativeWriters\(.*?\n\}',s,re.S)[0]
 offsets=[int(x,0) for x in re.search(r'offsets\[\]=\{([^}]+)',installer)[1].split(',')]
 entries=re.search(r'entries\[\]\[20\]=\{(.*?)\};',installer,re.S)[1]
 entries=[bytes(int(x,0) for x in row.split(',')) for row in re.findall(r'\{([^}]+)\}',entries)]
 assert len(offsets)==len(entries)==2
 for offset,entry in zip(offsets,entries):assert binary[offset:offset+20]==entry,hex(offset)
code=r'''
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_transcoder.hpp"
#include <cassert>
#include <cstdio>
struct OSObject {alignas(8) uint8_t bytes[0x4B90]{};unsigned size=sizeof(bytes);
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}};
using IOService=OSObject;using IOLock=int;
struct IORecursiveLock {unsigned depth=0;};
unsigned owner=0,modeCalls=0,armCalls=0;IORecursiveLock fbLock;bool expectDSI=false,endingWriter=false;
void IOLockLock(IOLock*){assert((fbLock.depth || endingWriter) && !owner);++owner;}void IOLockUnlock(IOLock*){assert(owner==1);--owner;}
void IORecursiveLockLock(IORecursiveLock *p){assert(!owner);++p->depth;}
void IORecursiveLockUnlock(IORecursiveLock *p){assert(!owner && p->depth);--p->depth;}
struct {IOLock *lock=nullptr;IOService *controller=nullptr;OSObject *dsiPort=nullptr;
 unsigned dsiNativeWriters=0;bool memoryFaulted=false,clockFaulted=false;
 struct {struct {bool faulted=false,suspended=false;}state;}bridge;struct {bool faulted=false;}dbuf;
 struct {IOService *memoryFramebuffer=nullptr;bool destroyingPath=false;}pipe[3];
 Display13Transcoder::Bindings transcoders;}display13FBPower;
IOService *panel=nullptr,*nativeFB=nullptr;bool dsiModeTarget(IOService *f){return f && f==panel;}
constexpr uint32_t kIOReturnNotReady=0xe00002d8U;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
uint32_t nativeMode(IOService*,int32_t m,int32_t d){assert(!fbLock.depth && !owner);assert(m==0x10000 && d==2);++modeCalls;return 7;}
void adlnFault(bool &f,unsigned){f=true;}
unsigned imageCalls=0,stateCalls=0,expectedWriters=1;bool nestedCursor=false;
uint32_t nativeImage(IOService*,IOService*,void*);
uint32_t nativeState(IOService*,IOService*,int32_t,int32_t,bool,bool);
struct XE {
 struct {unsigned dsi=1;}adlnOutputWiring;bool dsiStopAttempted=false,dsiResourcesReady=true;
 bool dsiNativeWriterHooksReady=true;uintptr_t orgDSICursor[2]{};
 uintptr_t orgDSIMode[6]{};
 static int beginDSINativeWriter(IOService*,IOService*);
 static void endDSINativeWriter(int);
 static uint32_t wrapDSICursorImage(IOService*,IOService*,void*);
 static uint32_t wrapDSICursorState(IOService*,IOService*,int32_t,int32_t,bool,bool);
 static uint32_t wrapDSISetDisplayMode(IOService*,int32_t,int32_t);
 static bool dsiNativeUpdateAllowed(IOService*);
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
template<class T>void put(void *p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
struct Case {
 IOService fb,ctl,peer;OSObject path,port;IOLock lock;
 Case(){assert(!fbLock.depth && !owner);expectedWriters=1;nestedCursor=false;xe={};
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
 // Public mode rejection precedes SaveNVRAMMode, disable, timing/backing and
 // native notifications; even the selected fixed mode is not yet admitted.
 {Case c;auto before=modeCalls;assert(XE::wrapDSISetDisplayMode(&c.fb,0x10000,2)==kIOReturnNotReady);
  assert(modeCalls==before && !fbLock.depth && !owner);}
 {Case c;auto before=armCalls;XE::wrapDisplay13WMArmUpdate(&c.fb);assert(armCalls==before+1 && !fbLock.depth);}
 {Case c;xe.dsiStopAttempted=true;c.refuse();}
 {Case c;xe.dsiResourcesReady=false;c.refuse();}
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
 puts("PASS DSI public mode preflight and native update dispatch: stopped/faulted rejection, held FB serialization, nested cursor counts, native wait lock boundaries, peer and disabled-DSI forwarding");
}
'''.replace('SCOPE',scope).replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-update-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
