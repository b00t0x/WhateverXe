#!/usr/bin/env python3
"""Exercise production clock transaction ownership and balanced native PW1 refs."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
state=re.search(r'struct ComboClockTransaction \{.*?\n\}',s,re.S).group()
state+=' comboClockTransaction;\n'+re.search(r'static bool comboClockAccess\(.*?\n\}',s,re.S).group()
functions='\n'.join(re.search(r'(?:bool|void) XE::'+n+r'\(.*?\n\}',s,re.S).group() for n in ['comboBeginClock','comboEndClock'])
code=r'''
#include "kern_adls_clock.hpp"
#include "kern_dsi_restart.hpp"
#include "kern_dsi_panel.hpp"
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_transcoder.hpp"
#include <cassert>
#include <cstdio>
#include <map>
struct IORecursiveLock {unsigned depth=0;};
void IORecursiveLockLock(IORecursiveLock *l){++l->depth;}
void IORecursiveLockUnlock(IORecursiveLock *l){assert(l->depth);--l->depth;}
uintptr_t thread=1;
void *current_thread(){return reinterpret_cast<void *>(thread);}
unsigned refs=0,getCount=0,putCount=0,reads=0;
std::map<unsigned long,uint32_t> mmio;
struct {Display13Transcoder::Bindings transcoders;XELP::DSI::Readout bootDSI;} display13FBPower;
alignas(8) uint8_t ctl[0xf00]{},port[0x700]{},access[0x80]{},power[0x100]{};
uint32_t config[6]{};
template<class T>T &at(void *p,unsigned off){return *reinterpret_cast<T *>(static_cast<uint8_t *>(p)+off);}
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry o;return &o;}
 void setProperty(const char *,void *,unsigned){}
};
uint32_t readNative(void *a,unsigned long reg){assert(a==access && refs && mmio.count(reg));++reads;return mmio.at(reg);}
void getPG(void *p,uint32_t pg){assert(p==power && pg==1);++refs;++getCount;}
void putPG(void *p,uint32_t pg){assert(p==power && pg==1 && refs);--refs;++putCount;}
struct XE {
 bool adlsDisplaySelected=true,display13FramebufferRequested=false;
 bool dsiStopAttempted=false,dsiIRQParked=false,dsiResourcesReady=false;
 XELP::DSI::Quiesce::Result dsiStop;XELP::DSI::Panel::Shutdown dsiPanelStop;
 struct {bool sleeping=false,failed=false;XELP::DSI::Restart::State host;} dsiRuntimePower;
 struct Native {
  bool ready=true;bool complete(){return ready;}
  void (*getPG)(void*,uint32_t)=::getPG,(*putPG)(void*,uint32_t)=::putPG;
 } tglTCNativeFunctions;
 uintptr_t orgTGLRegisterRead=reinterpret_cast<uintptr_t>(readNative);
 static uint32_t wrapTGLRegisterRead(void*,unsigned long);
 static uint8_t *adlsPowerTarget(void *p){return p==power?power:nullptr;}
 static bool comboClockPower(void *p,bool on){if(on)getPG(p,1);else putPG(p,1);return true;}
 static uint32_t comboClockRead(void *a,unsigned long r){return readNative(a,r);}
 static bool comboBeginClock(void*,void*,bool,bool&,void *other=nullptr);
 static void comboEndClock();
} xe,*callbackXE=&xe;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
STATE
FUNCTIONS
int main(){
 IORecursiveLock lock;comboClockTransaction.lock=&lock;
 at<void*>(ctl,0xc40)=access;at<void*>(ctl,0xc8)=power;at<void*>(port,0x548)=config;
 at<void*>(access,0x50)=ctl;at<void*>(power,0x10)=ctl;at<void*>(power,0x78)=access;
 mmio={{0x45404,3},{0x164280,0x01e07420},{0x1642bc,0x30},
       {0x46010,0xcc000000},{0x46014,0},{0x46018,0},{0x46030,0}};
 bool keep=false;
 assert(XE::comboBeginClock(ctl,port,true,keep));
 assert(refs==1 && lock.depth==1 && comboClockAccess(access));
 assert(!comboClockAccess(port) && comboClockTransaction.translation.pll==1);
 assert(comboClockTransaction.allocation.binding[0]==2);
 assert(!XE::comboBeginClock(ctl,port,true,keep));assert(refs==1 && lock.depth==1);
 thread=2;assert(!comboClockAccess(access));thread=1;
 // Partial enable: retain the powered PLL's reservation until native teardown.
 mmio[0x46014]=0x0c000000;XE::comboEndClock();
 assert(!refs && !lock.depth && !comboClockAccess(access));
 assert(XE::comboBeginClock(ctl,port,false,keep) && !keep);
 assert(comboClockTransaction.translation.pll==1);
 mmio[0x46014]=0;XE::comboEndClock();assert(!comboClockTransaction.allocation.binding[0]);
 // A gated port without an owned attempt must not stop its stale selector0.
 assert(XE::comboBeginClock(ctl,port,false,keep) && keep);XE::comboEndClock();
 assert(mmio[0x46010]==0xcc000000);
 // PW1 not acknowledged: no clock-bank reads or unbalanced refs.
 mmio[0x45404]=0;auto n=reads;
 assert(!XE::comboBeginClock(ctl,port,true,keep));assert(reads==n+1);
 mmio[0x45404]=3;mmio[0x164280]=UINT32_MAX;
 assert(!XE::comboBeginClock(ctl,port,true,keep));
 assert(!refs && !lock.depth && getCount==putCount && !comboClockTransaction.thread);
 // TGL and Display13 must never scan or allocate ADL-S extra PLL banks.
 for(bool d13:{false,true}) {
  xe.adlsDisplaySelected=false;xe.display13FramebufferRequested=d13;
  comboClockTransaction.allocation={};
  mmio={{0x45404,3},{0x164280,0x01000C00},{0x46010,0},{0x46014,0}};
  assert(XE::comboBeginClock(ctl,port,true,keep));
  assert(comboClockTransaction.translation.pll==0);
  XE::comboEndClock();
  assert(!refs && !lock.depth);
 }
 // Production transaction discovers/pins DSI's PLL1 before selecting B's
 // clock, even with both DDI gates closed. No donor call may own DSI PHY A.
 comboClockTransaction.allocation={};config[0]=1;at<uint32_t>(access,0x60)=0x1000000;
 display13FBPower.transcoders.dsi0Pipe=0;
 mmio={{0x45404,0xFC0F},{0x51004,0x40000000},{0x164280,0x1E07C01},{0x46010,0},{0x46014,0xCC000000},
  {0x7B008,0xC0000000},{0x6B400,0x80030006},{0x70008,0},{0x6B030,0x20130300},{0x6B094,1},
  {0x6B404,0},{0x6B034,0},{0x64000,0x80000000},{0x16428C,0x3001FB},{0x164290,0xE84},
  {0x6B000,0x04650437},{0x6B008,0x044F044D},{0x6B00C,0x07D1077F},{0x6B014,0x07A907A7}};
 display13FBPower.bootDSI=XELP::DSI::decodeReadout([&](uint32_t r,uint32_t &v){assert(mmio.count(r));v=mmio.at(r);return true;},38400);
 assert(XE::comboBeginClock(ctl,port,true,keep) && comboClockTransaction.translation.pll==0);
 XE::comboEndClock();assert(!refs && !lock.depth);
 assert(comboClockTransaction.allocation.external[0]==reinterpret_cast<uintptr_t>(ctl));
 assert(!comboClockTransaction.allocation.reset());
 config[0]=0;assert(!XE::comboBeginClock(ctl,port,true,keep) && !XE::comboBeginClock(ctl,port,false,keep));
 alignas(8) uint8_t otherPort[0x700]{};uint32_t otherConfig[6]{1};at<void*>(otherPort,0x548)=otherConfig;
 mmio[0x164280]&=~0x80CU;mmio[0x46010]=0xCC000000;
 assert(!XE::comboBeginClock(ctl,port,true,keep,otherPort)); // sharing also cannot overwrite DSI's target
 assert(comboClockTransaction.allocation.binding[0]==2);
 config[0]=1;mmio[0x16428C]^=0x400;assert(!XE::comboBeginClock(ctl,port,true,keep));
 assert(!refs && !lock.depth && getCount==putCount);
 puts("PASS production common Combo clock scope: balanced power, failed-enable retention, DSI source revalidation and external PLL ownership");
}
'''.replace('STATE',state).replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='adls-clock-runtime-') as d:
 d=Path(d);(d/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
