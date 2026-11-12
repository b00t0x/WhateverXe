#!/usr/bin/env python3
"""Execute the production wake notifier with native-before/after interleavings.

The native Sleep -> Doze/Wake contract is from prepareToExitSleep in the
identified framebuffer donor. This fixture does not emulate native MMIO or
IOGraphics thread scheduling; those remain live validation requirements.
"""
from pathlib import Path
import re, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'WhateverXe/kern_display13_framebuffer_power.inc').read_text()
function = re.search(r'bool XE::finishDisplay13SystemWake\([^;{]*\{.*?\n\}', source, re.S)[0]
code = r'''
#include "kern_display13_fb_memory.hpp"
#include <cassert>
#include <cstdio>
struct IOService {
 alignas(8) uint8_t bytes[0x4B90]{}; unsigned refs=1;
 IOService *getMetaClass(){return this;}
 unsigned getClassSize(){return sizeof(bytes);}
 void retain(){++refs;} void release(){assert(refs>1);--refs;}
};
using IOLock=int; unsigned held=0;
void IOLockLock(IOLock*){assert(!held);++held;}
void IOLockUnlock(IOLock*){assert(held==1);--held;}
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char*,const void*,unsigned){}
};
constexpr uint32_t kIOReturnSuccess=0;
struct XE {
 uintptr_t display13FramebufferEvent=0; IOService *adlnTopologyController=nullptr;
 bool display13PagingLayoutKnown=true,dsiPowerOffNotified=true;
 uint32_t dsiPowerOffEvents=1,dsiPowerOnEvents=0;
 struct {IOLock *lock=nullptr;uint32_t restoreEpoch=1;}display13FBPower;
 struct {bool dsi=true;}adlnOutputWiring;
 uint32_t displayWakeEventBusy=0,displayWakeEventEpoch=0,displayWakeEventErrors=0;
 uint32_t displayWakeEventPhase[3]{};
 bool restoreDisplay13Ports(IOService*){assert(!held);return true;}
 bool finishDisplay13SystemWake(IOService*,bool=false);
};
FUNCTION
template<class T> void put(IOService &fb,unsigned offset,T value){memcpy(fb.bytes+offset,&value,sizeof(value));}
unsigned wakes[3]{},ons[3]{}; bool powerThread=true;
uint32_t event(IOService *fb,int what,void *arg){
 assert(!held);
 if(!powerThread)return 0; // handleEvent's successful no-delivery path
 const auto i=Display13FBMemory::field<uint32_t>(fb,0x1DC);
 if(what==4){assert(arg==reinterpret_cast<void*>(1));++wakes[i];fb->bytes[0x199]|=0x20;}
 else {assert(what==8 && !arg);++ons[i];}
 return 0;
}
int main(){
 IOService ctl,fb[3]; IOLock lock=0;XE xe;
 xe.adlnTopologyController=&ctl;xe.display13FBPower.lock=&lock;
 xe.display13FramebufferEvent=reinterpret_cast<uintptr_t>(&event);
 for(unsigned i=0;i<3;++i){put(ctl,0xD60+8*i,&fb[i]);put(fb[i],0x1D0,&ctl);put(fb[i],0x1DC,i);}
 auto reset=[&](bool nativeFirst){
  ++xe.display13FBPower.restoreEpoch;xe.dsiPowerOffNotified=true;
  for(unsigned i=0;i<3;++i){wakes[i]=ons[i]=0;fb[i].bytes[0x199]=0;}
  put(fb[0],0x4284,2U);put(fb[1],0x4284,0U);put(fb[2],0x4284,1U);
  if(nativeFirst){event(&fb[1],4,reinterpret_cast<void*>(1));event(&fb[1],8,nullptr);put(fb[1],0x4284,2U);}
 };
 // Same topology, either callback order: each client receives one wake.
 // FB2 is parked in Doze; native Doze -> Wake emits no DidWake.
 for(bool nativeFirst:{false,true}){
  reset(nativeFirst);assert(xe.finishDisplay13SystemWake(&fb[0]));
  assert(wakes[1]==unsigned(nativeFirst));
  if(!nativeFirst){event(&fb[1],4,reinterpret_cast<void*>(1));event(&fb[1],8,nullptr);put(fb[1],0x4284,2U);}
  assert(xe.finishDisplay13SystemWake(&fb[1]));
  // Native Doze -> Wake owns DidPowerOn even though it emits no DidWake.
  assert(wakes[2]==1 && ons[2]==0);event(&fb[2],8,nullptr);put(fb[2],0x4284,2U);
  for(unsigned i=0;i<3;++i)assert(wakes[i]==1 && ons[i]==1 && fb[i].refs==1);
 }
 // An inactive native client already parked in Wake also needs one wake;
 // no new native transition will notify it.
 reset(false);put(fb[1],0x4284,2U);assert(xe.finishDisplay13SystemWake(&fb[0]));assert(wakes[1]==1);
 // No-DSI controller retains native ownership, including its FB0.
 reset(false);xe.adlnOutputWiring.dsi=false;put(fb[0],0x4284,0U);
 assert(xe.finishDisplay13SystemWake(&fb[1]));assert(!wakes[0] && !wakes[1] && wakes[2]==1);
 // A successful return without paging activation must remain retryable.
 reset(false);xe.adlnOutputWiring.dsi=true;powerThread=false;
 assert(!xe.finishDisplay13SystemWake(&fb[0]));assert(!wakes[0]);
 powerThread=true;assert(xe.finishDisplay13SystemWake(&fb[0]));assert(wakes[0]==1);
 assert(!xe.displayWakeEventBusy && !held);
 puts("production wake notification ownership: OK");
}
'''.replace('FUNCTION',function).replace('#include <cstdio>', '#include <cstdio>\n#include <initializer_list>')
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp);(path/'fixture.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(path/'fixture.cpp'),'-o',str(path/'fixture')],check=True)
    subprocess.run([path/'fixture'],check=True)
