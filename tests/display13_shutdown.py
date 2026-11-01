#!/usr/bin/env python3
"""Exercise the production shutdown adapter without powering hardware."""
from pathlib import Path
import re,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
body='\n'.join(re.search(r'(?:void|uint32_t) XE::'+n+r'\(.*?\n\}',s,re.S)[0] for n in ['finishDisplay13TCShutdown','wrapDisplay13Shutdown'])
code=r'''
#include <cstdint>
#include <cassert>
#include <cstring>
#include <vector>
struct Lock{bool held=false;};
void IOLockLock(Lock*l){assert(!l->held);l->held=true;}void IOLockUnlock(Lock*l){assert(l->held);l->held=false;}
struct IOWorkLoop{bool held=false,available=true;};
bool inGate(const IOWorkLoop*g){return g->held;}
bool tryGate(IOWorkLoop*g){if(!g->available)return false;g->held=true;return true;}
void openGate(IOWorkLoop*g){assert(g->held);g->held=false;}
struct Port{unsigned id,refs=1;void retain(){++refs;}void release(){assert(refs>1);--refs;}};
struct Bridge{bool begun=true;struct{bool suspended=false;}state;unsigned ddi[7]{},aux[7]{};
 unsigned nativeDDIRefs(unsigned d){return ddi[d];}unsigned nativeAUXRefs(unsigned d){return aux[d];}};
struct {Lock*lock;void*controller;IOWorkLoop*workLoop;bool tcSupported=true;Bridge bridge;Port*tcObject[4]{};unsigned tcBusy[4]{};bool tcSignalReady[4]{};}display13FBPower;
namespace Display13FBMemory{template<class T>T field(void*p,unsigned off){T v;memcpy(&v,(char*)p+off,sizeof(v));return v;}}
uint32_t published[8]{};struct IORegistryEntry{static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}void setProperty(const char*,const void*p,size_t n){assert(n==sizeof(published));memcpy(published,p,n);}};
std::vector<unsigned> events;unsigned modeCalls=0,nativeCalls=0;uint32_t nativeResult=0,modeResult=0;bool shutdownSuspends=false;
#define FunctionCast(fn,p) reinterpret_cast<decltype(&fn)>(p)
struct XE{bool display13FBTCHooksReady=true;uintptr_t orgDisplay13Shutdown;
 uintptr_t adlnWorkLoopInGate=(uintptr_t)inGate,adlnWorkLoopTryClose=(uintptr_t)tryGate,adlnWorkLoopOpen=(uintptr_t)openGate;
 static void finishDisplay13TCShutdown(void*);static uint32_t wrapDisplay13Shutdown(void*,void*);
 static uint32_t wrapDisplay13TCMode(Port*p,uint32_t mode){auto &c=display13FBPower;assert(!c.lock->held && c.workLoop->held && mode==0 && p->refs==2 && !c.bridge.state.suspended);++modeCalls;events.push_back(3);if(!modeResult)c.tcObject[p->id]=nullptr;return modeResult;}
};XE*callbackXE;
FUNCTIONS
uint32_t nativeShutdown(void*c,void*){++nativeCalls;events.push_back(2);
 if(shutdownSuspends){XE::finishDisplay13TCShutdown(c);display13FBPower.bridge.state.suspended=true;}
 return nativeResult;}
int main(){
 for(unsigned test=0;test<12;++test){Lock lock;IOWorkLoop gate;char controller[0xe10]{};auto ptr=&gate;memcpy(controller+0xe00,&ptr,sizeof(ptr));
  display13FBPower={};auto &c=display13FBPower;c.lock=&lock;c.controller=controller;c.workLoop=&gate;c.tcSupported=true;c.bridge.begun=true;
  Port idle{1},active{0};c.tcObject[0]=&active;c.tcObject[1]=&idle;c.tcSignalReady[0]=true;c.bridge.ddi[3]=1;
  XE x;callbackXE=&x;x.orgDisplay13Shutdown=(uintptr_t)nativeShutdown;
  modeCalls=nativeCalls=0;modeResult=nativeResult=0;events.clear();shutdownSuspends=false;
  if(test==1)gate.available=false;
  if(test==2)c.bridge.state.suspended=true;
  if(test==3)c.tcBusy[1]=1;
  if(test==4)c.bridge.ddi[4]=1;
  if(test==5)c.bridge.aux[4]=1;
  if(test==6)c.tcSignalReady[1]=true;
  if(test==7)nativeResult=0x1234;
  if(test==8)modeResult=0x4567;
  if(test==9)gate.held=true;
  if(test==10)shutdownSuspends=true;
  assert(XE::wrapDisplay13Shutdown(test==11?nullptr:controller,nullptr)==nativeResult);
  assert(nativeCalls==1 && events.front()==2 && events.size()==1+modeCalls);
  const bool attempted=test==0||test==8||test==9||test==10;
  assert(modeCalls==unsigned(attempted));assert(!lock.held && gate.held==(test==9));assert(idle.refs==1&&active.refs==1);
  assert(c.tcObject[0]==&active && c.bridge.ddi[3]==1);
  assert((c.tcObject[1]==nullptr)==(attempted&&test!=8));
  if(test==0){XE::wrapDisplay13Shutdown(controller,nullptr);assert(modeCalls==1);c.tcSignalReady[0]=false;c.bridge.ddi[3]=0;XE::wrapDisplay13Shutdown(controller,nullptr);assert(modeCalls==2&&!c.tcObject[0]);}
 }
}
'''.replace('FUNCTIONS',body)
with tempfile.TemporaryDirectory(prefix='wex-shutdown-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
override=re.search(r'void XE::wrapAlderLakeNPowerOverride\(.*?\n\}',s,re.S)[0]
assert override.index('display13ShutdownPowerCaller')<override.index('finishDisplay13TCShutdown')<override.index('wrapAlderLakeNEngineDisable')
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 install=re.search(r'bool XE::processDisplay13Shutdown\(.*?\n\}',s,re.S)[0]
 for name,offset in [('entry',0x5173a),('powerCall',0x51d24)]:
  raw=bytes(int(v,0) for v in re.search(r'const uint8_t '+name+r'\[\]=\{([^}]+)',install)[1].split(','))
  assert b[offset:offset+len(raw)]==raw
 assert b[0x339b8:0x339d3]==bytes.fromhex('48ff05a96f0a00b8100300e04939c6740ab8500200e04939c67570')
 assert b[0x339e4]==0xe8 and 0x339e9+int.from_bytes(b[0x339e5:0x339e9],'little',signed=True)==0x5173a
 assert 0x51d29+int.from_bytes(b[0x51d25:0x51d29],'little',signed=True)==0x5b464
print('PASS shutdown mode0: live-port exclusion, gate contention, native errors, retry/idempotence, cleanup before power suspend, donor power-off/restart callsites')
