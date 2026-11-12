#!/usr/bin/env python3
"""Replay native power callbacks through the real common wake completion.

Oracle: the donor's Sleep exit owns DidWake; Wake entry owns DidPowerOn,
including Doze -> Wake. Parked subscribers need completion after a shared restore, with
or without DSI. Hardware restoration is the boundary mock. No DSI panel,
GPU, kernel scheduling or physical link behavior is emulated here.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'WhateverXe/kern_display13_framebuffer_power.inc').read_text()
functions = '\n'.join(re.search(r'(?:bool|uint32_t) XE::'+name+r'\([^;{]*\{.*?\n\}', source, re.S)[0]
                      for name in ['ownedDisplay13Framebuffer', 'finishDisplay13SystemWake', 'wrapDisplay13SetPowerState'])
code = r'''
#include "kern_display13_fb_memory.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <algorithm>
struct IOService {
 alignas(8) uint8_t bytes[0x4B90]{}; unsigned refs=1;
 IOService *getMetaClass(){return this;}
 unsigned getClassSize(){return sizeof(bytes);}
 const char *getClassName(){return "AppleIntelFramebuffer";}
 void retain(){++refs;} void release(){assert(refs>1);--refs;}
};
using OSObject=IOService;using IOLock=int;using IORecursiveLock=int;
unsigned held=0;void IOLockLock(IOLock*){assert(!held);++held;}
void IOLockUnlock(IOLock*){assert(held==1);--held;}
void IORecursiveLockLock(IORecursiveLock*){assert(false);}
void IORecursiveLockUnlock(IORecursiveLock*){assert(false);}
struct DSIModeScope {IORecursiveLock *lock=nullptr;DSIModeScope(IOService*){assert(false);}};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 template<class...T>void setProperty(T...){};};
constexpr uint32_t kIOReturnSuccess=0,kIOReturnNotReady=1,kIOReturnUnsupported=2;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct {IOLock *lock=nullptr;uint32_t restoreEpoch=0;}display13FBPower;
bool dsiModeTarget(IOService*){assert(false);return false;}
bool dsiLifecycleTarget(IOService*,OSObject*){assert(false);return false;}
bool dsiNativeUpdateAllowed(IOService*){assert(false);return false;}
bool hardwareReady=true,portsReady=true;unsigned nativeCalls=0;uint32_t nativeResult=0;
uint32_t nativePower(IOService*,uint32_t);
struct XE {
 uintptr_t orgDSIAttributes[4]{},display13FramebufferEvent=0;IOService *adlnTopologyController=nullptr;
 bool display13FBLifecycleHooksReady=true,display13FBABIReady=true,display13PagingLayoutKnown=true;
 struct {bool dsi=false;}adlnOutputWiring;
 struct {bool sleeping=false,failed=false;}dsiRuntimePower;
 bool dsiSystemWakeNotify=false,dsiPowerOffNotified=false,dsiPowerTest=false,dsiStopTest=false;
 bool dsiStopAttempted=false,dsiMemoryReleased=false,dsiDDIReleased=false,dsiNativeWriterHooksReady=false;
 uint32_t dsiPowerCallbackBusy=0,dsiWakeIncomplete=0,dsiPowerOffEvents=0,dsiPowerOnEvents=0;
 struct {bool complete(){return false;}}dsiClockStop;struct {bool complete=false;}dsiPanelStop;
 uint32_t displayWakeEventBusy=0,displayWakeEventEpoch=0,displayWakeEventErrors=0,displayWakeEventPhase[3]{};
 bool resumeDSIPath(IOService*,IOService*,OSObject*){assert(false);return false;}
 bool quiesceDSIPath(IOService*,IOService*,OSObject*,bool=false){assert(false);return false;}
 bool restoreDisplay13Ports(IOService*){assert(!held);return portsReady;}
 static bool prepareDisplay13Wake(IOService*){return hardwareReady;}
 static void recordDSIClient(IOService*,unsigned,uint32_t,uint32_t){}
 bool ownedDisplay13Framebuffer(IOService*)const;
 bool finishDisplay13SystemWake(IOService*,bool=false);
 static uint32_t wrapDisplay13SetPowerState(IOService*,uint32_t);
};XE *callbackXE;
FUNCTIONS
template<class T>void put(IOService &fb,unsigned at,T value){memcpy(fb.bytes+at,&value,sizeof(value));}
unsigned wakes[3]{},ons[3]{},reentries=0;bool reenter=false;
uint32_t event(IOService *fb,int what,void *arg){
 assert(!held);auto i=Display13FBMemory::field<uint32_t>(fb,0x1DC);assert(i<3);
 if(what==4){assert(arg==reinterpret_cast<void*>(1));++wakes[i];fb->bytes[0x199]|=0x20;
  if(reenter){reenter=false;++reentries;assert(XE::wrapDisplay13SetPowerState(fb,2)==kIOReturnNotReady);}
 }
 else {assert(what==8 && !arg);++ons[i];}return 0;
}
uint32_t nativePower(IOService *fb,uint32_t state){
 ++nativeCalls;if(nativeResult)return nativeResult;
 auto old=Display13FBMemory::field<uint32_t>(fb,0x4284);
 // Contract observed in the fixed donor's prepareToExitSleep/EnterWake.
 if(old==0 && state!=0)event(fb,4,reinterpret_cast<void*>(1));
 if(old!=2 && state==2)event(fb,8,nullptr);
 put(*fb,0x4284,state);return 0;
}
struct Fixture {
 XE xe;IOService ctl,fb[3];IOLock lock=0;
 Fixture(){callbackXE=&xe;hardwareReady=portsReady=true;nativeCalls=0;nativeResult=0;reenter=false;reentries=0;
  display13FBPower={&lock,1};xe.adlnTopologyController=&ctl;
  xe.display13FramebufferEvent=reinterpret_cast<uintptr_t>(&event);
  xe.orgDSIAttributes[2]=reinterpret_cast<uintptr_t>(&nativePower);
  for(unsigned i=0;i<3;++i){wakes[i]=ons[i]=0;put(ctl,0xD60+8*i,&fb[i]);
   put(fb[i],0x1D0,&ctl);put(fb[i],0x1DC,i);}
 }
};
int main(){
 unsigned order[]={0,1,2};do{for(unsigned states=0;states<27;++states){
  Fixture f;unsigned power=states;
  for(auto &fb:f.fb){put(fb,0x4284,power%3);power/=3;}
  for(auto i:order)assert(XE::wrapDisplay13SetPowerState(&f.fb[i],2)==0);
  for(unsigned i=0;i<3;++i){
   // Every parked subscriber must have one effective completion, even when
   // no panel/DSI callback exists to initiate the common notifier.
   if(wakes[i]!=1 || ons[i]!=1 || f.fb[i].refs!=1){
    fprintf(stderr,"missing/duplicate completion: states=%u first=%u fb=%u wake=%u on=%u refs=%u\n",
     states,order[0],i,wakes[i],ons[i],f.fb[i].refs);return 1;
   }
   assert(XE::wrapDisplay13SetPowerState(&f.fb[i],2)==0);
   assert(wakes[i]==1 && ons[i]==1);
  }
 }}while(std::next_permutation(order,order+3));
 {Fixture f;
  for(unsigned generation=1;generation<=3;++generation){
   display13FBPower.restoreEpoch=generation;
   for(auto &fb:f.fb){fb.bytes[0x199]=0;put(fb,0x4284,2U);}
   for(auto &fb:f.fb)assert(XE::wrapDisplay13SetPowerState(&fb,2)==0);
   for(unsigned i=0;i<3;++i)assert(wakes[i]==generation && ons[i]==generation);
  }
 }
 {Fixture f;portsReady=false;reenter=true;
  // A disconnected TC peer must not strand IOGraphics subscribers. A
  // notification reentry can retry later without consuming a second wake.
  for(auto &fb:f.fb)put(fb,0x4284,1U);
  for(auto &fb:f.fb)assert(XE::wrapDisplay13SetPowerState(&fb,2)==0);
  for(unsigned i=0;i<3;++i)assert(wakes[i]==1 && ons[i]==1);
  assert(reentries==1 && !f.xe.displayWakeEventBusy);
 }
 {Fixture f;display13FBPower.restoreEpoch=0;
  for(auto &fb:f.fb)put(fb,0x4284,2U);
  assert(XE::wrapDisplay13SetPowerState(&f.fb[0],2)==0);
  assert(!wakes[0] && !wakes[1] && !wakes[2]); // initial start is not a wake
 }
 {Fixture f;hardwareReady=false;assert(XE::wrapDisplay13SetPowerState(&f.fb[0],2)==kIOReturnNotReady);
  assert(!nativeCalls && !wakes[0]);}
 {Fixture f;nativeResult=99;assert(XE::wrapDisplay13SetPowerState(&f.fb[0],2)==99);
  assert(nativeCalls==1 && !wakes[0] && !wakes[1]);}
 {Fixture f;IOService foreign;put(foreign,0x4284,2U);
  assert(XE::wrapDisplay13SetPowerState(&foreign,2)==0 && !wakes[0]);}
 assert(!held);puts("PASS production native power -> common completion, all subscriber states/orders; no DSI required");
}
'''.replace('FUNCTIONS', functions)
with tempfile.TemporaryDirectory(prefix='wex-native-power-') as tmp:
    p = Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined', '-I'+str(root/'WhateverXe'),
                    str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
