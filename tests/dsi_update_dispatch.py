#!/usr/bin/env python3
"""Execute production public-mode/armUpdate dispatch before native side effects."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'WhateverXe/kern_xe.cpp').read_text()
functions='\n'.join(re.search(r'(?:static bool|bool|uint32_t|void) (?:XE::)?'+name+r'\(.*?\n\}',s,re.S)[0]
 for name in ['dsiCachePath','dsiLifecycleTarget','wrapDSISetDisplayMode','dsiNativeUpdateAllowed','wrapDisplay13WMArmUpdate'])
scope=re.search(r'struct DSIModeScope \{.*?\n\};',s,re.S)[0]
code=r'''
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_transcoder.hpp"
#include <cassert>
#include <cstdio>
struct OSObject {alignas(8) uint8_t bytes[0x4B90]{};unsigned size=sizeof(bytes);
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}};
using IOService=OSObject;using IOLock=int;
struct IORecursiveLock {unsigned depth=0;};
unsigned owner=0,modeCalls=0,armCalls=0;IORecursiveLock fbLock;bool expectDSI=false;
void IOLockLock(IOLock*){assert(fbLock.depth && !owner);++owner;}void IOLockUnlock(IOLock*){assert(owner==1);--owner;}
void IORecursiveLockLock(IORecursiveLock *p){assert(!owner);++p->depth;}
void IORecursiveLockUnlock(IORecursiveLock *p){assert(!owner && p->depth);--p->depth;}
struct {IOLock *lock=nullptr;IOService *controller=nullptr;OSObject *dsiPort=nullptr;
 bool memoryFaulted=false,clockFaulted=false;
 struct {struct {bool faulted=false,suspended=false;}state;}bridge;struct {bool faulted=false;}dbuf;
 struct {IOService *memoryFramebuffer=nullptr;bool destroyingPath=false;}pipe[3];
 Display13Transcoder::Bindings transcoders;}display13FBPower;
IOService *panel=nullptr;bool dsiModeTarget(IOService *f){return f && f==panel;}
constexpr uint32_t kIOReturnNotReady=0xe00002d8U;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
uint32_t nativeMode(IOService*,int32_t m,int32_t d){assert(!fbLock.depth && !owner);assert(m==0x10000 && d==2);++modeCalls;return 7;}
struct XE {
 struct {unsigned dsi=1;}adlnOutputWiring;bool dsiStopAttempted=false,dsiResourcesReady=true;
 uintptr_t orgDSIMode[6]{};
 static uint32_t wrapDSISetDisplayMode(IOService*,int32_t,int32_t);
 static bool dsiNativeUpdateAllowed(IOService*);
 static void wrapDisplay13WMArmUpdate(IOService*);
 static void display13WMArmUpdate(IOService*){assert(!owner);assert(bool(fbLock.depth)==expectDSI);++armCalls;}
}xe,*callbackXE=&xe;
SCOPE
FUNCTIONS
template<class T>void put(void *p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
struct Case {
 IOService fb,ctl,peer;OSObject path,port;IOLock lock;
 Case(){xe={};xe.orgDSIMode[5]=reinterpret_cast<uintptr_t>(nativeMode);display13FBPower={};auto &c=display13FBPower;
  c.lock=&lock;c.controller=&ctl;c.dsiPort=&port;c.pipe[0].memoryFramebuffer=&fb;c.transcoders.dsi0Pipe=0;panel=&fb;
  put(&fb,0x4220,&fbLock);put(&fb,0x1D0,&ctl);put(&fb,0x4A00,1U);put(&fb,0x4A08,&path);
  put(&path,0x3640,0U);put(&path,0x3644,2U);put(&path,0x32B0,&fb);put(&path,0x3648,&port);put(&port,0x550,1U);
  expectDSI=true;
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
 puts("PASS DSI public mode preflight and native update dispatch: stopped/faulted rejection, held FB serialization, peer and disabled-DSI forwarding");
}
'''.replace('SCOPE',scope).replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-update-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
