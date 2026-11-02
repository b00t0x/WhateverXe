#!/usr/bin/env python3
"""Run production lifecycle entry points; no DSI request may reach donor writes."""
from pathlib import Path
from production_source import read_source
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
functions='\n'.join(re.search(r'(?:static bool|uint32_t|void) (?:XE::)?'+name+r'\([^;{]*\{.*?\n\}',s,re.S)[0]
 for name in ['dsiCachePath','dsiLifecycleTarget','wrapDisplay13TCSetMode','wrapAlderLakeNDisableDisplay'])
code=r'''
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_cursor.hpp"
#include "kern_display13_tc_lightup.hpp"
#include <cassert>
#include <cstdio>
struct OSObject {alignas(8) uint8_t bytes[0x4B90]{};unsigned size=sizeof(bytes);
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}};
using IOService=OSObject;using IOLock=int;
bool hostStopOK=false;
unsigned depth=0,bindingCalls=0,modeCalls=0,stopCalls=0,notifications=0,modeResult=0,hostStops=0;
void IOLockLock(IOLock*){assert(!depth);++depth;}void IOLockUnlock(IOLock*){assert(depth==1);--depth;}
void adlnFault(bool &v,unsigned){assert(depth==1);v=true;}
void IOSleep(unsigned){assert(false);}
struct IOMemoryMap {uintptr_t getVirtualAddress(){assert(false);return 0;}size_t getLength(){return 0;}};
struct {IOLock *lock=nullptr;IOService *controller=nullptr;IOMemoryMap *map=nullptr;bool memoryFaulted=false,clockFaulted=false;
 struct {struct {bool faulted=false;}state;}bridge;struct {unsigned updating=0;}dbuf;
 struct {IOService *memoryFramebuffer=nullptr;bool wmProgramming=false,wmCommitting=false;}pipe[3];
 Display13Transcoder::Bindings transcoders;}display13FBPower;
namespace TGLFBBinding {enum class Status{Unavailable,Complete};struct Readout{Status status{};unsigned pipe=0;};}
TGLFBBinding::Readout adlnInspectPathBinding(IOService*,OSObject*,void*,unsigned){++bindingCalls;return {};}
bool adlnWriterMappingBound(){return false;}
void adlnPublishMemory(const char*,unsigned,void*,size_t,bool=false){}
IOService *panel=nullptr;bool dsiModeTarget(IOService *fb){return fb && fb==panel;}
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
uint32_t nativeMode(void*,void*,const void*){++modeCalls;return modeResult;}
void nativeStop(IOService*,IOService*,OSObject*,uint8_t){++stopCalls;}
void notify(void*,void*,uint32_t value){assert(value==1);++notifications;}
struct XE {
 struct Wiring{unsigned dsi=0;operator unsigned()const{return dsi;}}adlnOutputWiring;void *adlnPlatform=nullptr;
 uintptr_t orgDisplay13TCSetMode=reinterpret_cast<uintptr_t>(nativeMode),display13TCNotify=reinterpret_cast<uintptr_t>(notify);
 uintptr_t orgAlderLakeNDisableDisplay=reinterpret_cast<uintptr_t>(nativeStop);
 bool quiesceDSIPath(IOService*,IOService*,OSObject*){++hostStops;return hostStopOK;}
 static uint32_t wrapDisplay13TCSetMode(void*,void*,const void*);
 static void wrapAlderLakeNDisableDisplay(IOService*,IOService*,OSObject*,uint8_t);
}xe,*callbackXE=&xe;
FUNCTIONS
template<class T>void put(void *p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
int main(){
 IOService controller,fb;OSObject path,other,port;IOLock lock;
 auto &c=display13FBPower;c.lock=&lock;c.controller=&controller;
 put(&fb,0x4A00,1U);put(&fb,0x4A08,&path);put(&path,0x3648,&port);
 const auto reject=[&](IOService *f,OSObject *p){
  const auto m=modeCalls,n=notifications,d=stopCalls,b=bindingCalls,h=hostStops;
  assert(XE::wrapDisplay13TCSetMode(&controller,f,nullptr)==kIOReturnNotReady);
  assert(modeCalls==m && notifications==n);
  c.memoryFaulted=false;XE::wrapAlderLakeNDisableDisplay(&controller,f,p,0);
  assert(c.memoryFaulted && stopCalls==d && bindingCalls==b && hostStops==h+1 && !depth);
 };
 xe.adlnOutputWiring.dsi=1;reject(&fb,&path); // actual pipe A / port A
 hostStopOK=true;c.memoryFaulted=false;XE::wrapAlderLakeNDisableDisplay(&controller,&fb,&path,0);
 assert(!c.memoryFaulted && stopCalls==0);hostStopOK=false;
 put(&path,0x3640,1U);put(&port,0x120,3U);
 panel=&fb;reject(&fb,&path);panel=nullptr; // retained identity survives corrupted path
 put(&path,0x3640,3U);reject(&fb,&path);
 put(&path,0x3640,1U);put(&port,0x120,0U);reject(&fb,&path);
 put(&port,0x120,3U);port.size=0x100;reject(&fb,&path);port.size=sizeof(port.bytes);
 put(&path,0x3648,static_cast<OSObject*>(nullptr));reject(&fb,&path);put(&path,0x3648,&port);
 path.size=0x100;reject(&fb,&path);path.size=sizeof(path.bytes);
 put(&fb,0x4A00,0U);reject(&fb,nullptr);put(&fb,0x4A00,2U);reject(&fb,&path);
 put(&fb,0x4A00,1U);fb.size=0x100;reject(&fb,&path);fb.size=sizeof(fb.bytes);reject(nullptr,nullptr);
 // An otherwise external FB with a mismatched supplied stop path is ambiguous.
 c.memoryFaulted=false;const auto stops=stopCalls;
 XE::wrapAlderLakeNDisableDisplay(&controller,&fb,&other,1);assert(c.memoryFaulted && stops==stopCalls);
 // Qualified external path keeps native mode return/notification behavior.
 c.memoryFaulted=false;auto m=modeCalls,n=notifications,d=stopCalls;
 assert(!XE::wrapDisplay13TCSetMode(&controller,&fb,nullptr));assert(modeCalls==m+1 && notifications==n);
 modeResult=7;assert(XE::wrapDisplay13TCSetMode(&controller,&fb,nullptr)==7);assert(notifications==n+1);
 XE::wrapAlderLakeNDisableDisplay(&controller,&fb,&path,0);assert(stopCalls==d+1 && !c.memoryFaulted);
 // With DSI disabled no additional native-object interpretation is introduced.
 xe.adlnOutputWiring.dsi=0;panel=&fb;modeResult=0;
 assert(!XE::wrapDisplay13TCSetMode(nullptr,nullptr,nullptr));
 XE::wrapAlderLakeNDisableDisplay(nullptr,nullptr,nullptr,0);assert(stopCalls==d+2 && !c.memoryFaulted);
 puts("PASS production DSI mode/disable dispatch: no donor entry or notifications, fault retention, ambiguous object rejection, external return behavior");
}
'''.replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-lifecycle-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
