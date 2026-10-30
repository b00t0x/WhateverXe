#!/usr/bin/env python3
"""Replay production DSI port/probe with real source and power qualification.

Optional exact donor executes its base mode setter; HAL/AUX/reset are forbidden.
The topology, power leases and register values are synthetic CPU fixtures.
"""
from pathlib import Path
from production_source import read_source
import hashlib, re, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
source=read_source(root/'WhateverXe/kern_xe.cpp')
functions='\n'.join(re.search(r'uint32_t XE::'+name+r'\(.*?\n\}',source,re.S)[0]
                    for name in ['setDSIPortMode','wrapAlderLakeNPortState','wrapDisplay13ProbeMode'])
dispatch=re.search(r'uint32_t XE::wrapDisplay13TCMode\(.*?\n\}',source,re.S)[0]
assert dispatch.index('return setDSIPortMode(port,wanted)')<dispatch.index('self->orgDisplay13TCMode')
donor=None
if len(sys.argv)>1:
    donor=Path(sys.argv[1]).resolve()
    assert hashlib.sha256(donor.read_bytes()).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
code=r'''
#include "kern_dsi_readout.hpp"
#include "kern_display13_power_bridge.hpp"
#include "kern_display13_transcoder.hpp"
#include "kern_display13_fb_topology.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <map>
#include <sys/mman.h>
constexpr uint32_t kIOReturnSuccess=0,kIOReturnNotReady=0xe00002d8U,kIOReturnUnsupported=0xe00002c7U;
struct OSObject {
 alignas(8) uint8_t bytes[0x6B0]{};unsigned refs=1;unsigned size=sizeof(bytes);
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}void retain(){++refs;}
};
struct IOLock {bool held=false;};void IOLockLock(IOLock *p){assert(!p->held);p->held=true;}
void IOLockUnlock(IOLock *p){assert(p->held);p->held=false;}
namespace Display13FBMemory {template<class T>T &field(void *p,unsigned off){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+off);}}
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *,bool){}void setProperty(const char *,const void *,unsigned){}};
using Registers=std::map<uint32_t,uint32_t>;
struct Display13FBBootIO {
 Registers *map;
 bool readable(uint32_t r){return map && map->count(r);}
 uint32_t read(uint32_t r){assert(readable(r));return map->at(r);}
};
struct {IOLock *lock;void *controller,*access;Registers *map;OSObject *dsiPort=nullptr;
 Display13Transcoder::Bindings transcoders;XELP::DSI::Readout bootDSI;Display13Power::NativeBridge bridge;
 bool memoryFaulted=false,clockFaulted=false;} display13FBPower;
unsigned nativeCalls=0,providerCalls=0,modeCalls=0;
uint32_t nativeProbe(OSObject *){++nativeCalls;return 1;}
uint32_t baseMode(OSObject *p,uint32_t m){assert(m==1);++modeCalls;Display13FBMemory::field<uint32_t>(p,0x550)=m;return 0;}
struct XE {
 bool dsiResourcesReady=false;uintptr_t xelpProviderProbe=reinterpret_cast<uintptr_t>(nativeProbe);
 bool display13FBTCHooksReady=true,display13FBTopologyHooksReady=true;
 uintptr_t display13TCBaseMode=reinterpret_cast<uintptr_t>(baseMode);
 void *adlnTopologyController;alignas(8) uint8_t adlnPlatform[0x1C0]{};Display13OutputWiring adlnOutputWiring;
 uint32_t (*orgAlderLakeNPortState)(OSObject*)=nativeProbe;
 bool xelpBindProbe(void *){++providerCalls;return true;}
 static uint32_t setDSIPortMode(OSObject *,uint32_t);
 static uint32_t wrapDisplay13ProbeMode(OSObject *);
 static uint32_t wrapAlderLakeNPortState(OSObject *);
} xe,*callbackXE=&xe;
bool display13Offline=false;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
FUNCTIONS
void noReset(void *){assert(false && "DSI must not call native reset");}
int main(int argc,char **argv){
 void *image=nullptr;
 if(argc==2){std::ifstream f(argv[1],std::ios::binary);std::vector<char> b((std::istreambuf_iterator<char>(f)),{});
  image=mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);assert(image!=MAP_FAILED);
  assert(b.size()<0x400000);memcpy(image,b.data(),b.size());xe.display13TCBaseMode=reinterpret_cast<uintptr_t>(image)+0x1b4c8;}
 Registers r={{0x7B008,0xC0000000},{0x6B400,0x80030006},{0x45404,0xFC0F},{0x70008,0},
  {0x6B030,0x20130300},{0x6B094,1},{0x6B404,0},{0x6B034,0},{0x64000,0x80000000},{0x164280,0x1E07801},
  {0x46014,0xCC000000},{0x16428C,0x3001FB},{0x164290,0xE84},
  {0x6B000,0x04650437},{0x6B008,0x044F044D},{0x6B00C,0x07D1077F},{0x6B014,0x07A907A7},
  {0x51004,0x40000000},{0x45454,3},{0x42000,0x0EFFFFFF}};
 auto &c=display13FBPower;IOLock lock;c.lock=&lock;c.map=&r;
 OSObject controller,access,port,peer;uintptr_t vtable[64]{};vtable[0x120/8]=reinterpret_cast<uintptr_t>(noReset);
 Display13FBMemory::field<void*>(port.bytes,0)=vtable;
 c.controller=xe.adlnTopologyController=&controller;c.access=&access;
 xe.adlnOutputWiring.valid=true;xe.adlnOutputWiring.dsi=1;xe.adlnOutputWiring.hdmi=2;xe.adlnOutputWiring.ddc[1]=2;
 xe.adlnPlatform[0x12]=9;
 uint32_t panel[]={0,0,0,0,2,0x18};memcpy(xe.adlnPlatform+0x20,panel,sizeof(panel));
 uint32_t hdmi[]={1,2,2,0,0x800,0x202};memcpy(xe.adlnPlatform+0x38,hdmi,sizeof(hdmi));
 auto config=reinterpret_cast<uint32_t*>(xe.adlnPlatform+0x20);
 Display13FBMemory::field<uint32_t*>(port.bytes,0x548)=config;
 Display13FBMemory::field<void*>(port.bytes,0x578)=&access;
 Display13FBMemory::field<uint32_t*>(peer.bytes,0x548)=reinterpret_cast<uint32_t*>(xe.adlnPlatform+0x38);
 c.transcoders.dsi0Pipe=0;
 c.bootDSI=XELP::DSI::decodeReadout([&](uint32_t a,uint32_t &v){if(!r.count(a))return false;v=r.at(a);return true;},38400);
 assert(c.bootDSI.complete());
 auto &bridge=c.bridge;bridge.begun=true;bridge.selectedPorts=3;bridge.bootPort=0;
 auto &state=bridge.state;state.ready=true;state.allowed=Display13Power::All;
 const auto pg=Display13Power::dependencies(Display13Power::nativeScope(1));
 const auto ddi=Display13Power::dependencies(Display13Power::ddiDomain(0));
 state.borrowed=pg|ddi;
 for(unsigned i=0;i<Display13Power::Count;++i)if(state.borrowed&(1U<<i))state.refs[i]=2;
 bridge.pgRefs[0]=1;bridge.pgLease[0].owner=&state;bridge.pgLease[0].domains=pg;
 bridge.ddiRefs[0]=1;bridge.ddiLease[0].owner=&state;bridge.ddiLease[0].domains=ddi;
 // Firmware-on is not a power reference. Each failure leaves mode/state/refcount unchanged.
 const auto reject=[&]{assert(XE::setDSIPortMode(&port,1)==kIOReturnNotReady);
  assert(!Display13FBMemory::field<uint32_t>(&port,0x550) && port.refs==1 && !c.dsiPort && !lock.held);};
 bridge.ddiRefs[0]=0;reject();bridge.ddiRefs[0]=1;
 bridge.pgRefs[0]=0;reject();bridge.pgRefs[0]=1;
 c.memoryFaulted=true;reject();c.memoryFaulted=false;
 c.clockFaulted=true;reject();c.clockFaulted=false;
 c.transcoders.dsi0Pipe=1;reject();c.transcoders.dsi0Pipe=0;
 r[0x6B000]^=1;reject();r[0x6B000]^=1;
 r[0x164280]^=1;reject();r[0x164280]^=1;
 Display13FBMemory::field<void*>(&port,0x578)=&controller;reject();Display13FBMemory::field<void*>(&port,0x578)=&access;
 uint32_t copied[6];memcpy(copied,config,sizeof(copied));Display13FBMemory::field<uint32_t*>(&port,0x548)=copied;
 reject();Display13FBMemory::field<uint32_t*>(&port,0x548)=config;
 assert(XE::setDSIPortMode(&port,0)==kIOReturnUnsupported && !c.dsiPort);
 display13Offline=true;assert(!XE::wrapAlderLakeNPortState(&port) && !c.dsiPort);display13Offline=false;
 assert(!XE::wrapDisplay13ProbeMode(&port));xe.dsiResourcesReady=true;
 assert(XE::wrapDisplay13ProbeMode(&port)==1 && !nativeCalls && !providerCalls);
 assert(XE::wrapAlderLakeNPortState(&port)==2 && !nativeCalls && !providerCalls);
 assert(Display13FBMemory::field<uint32_t>(&port,0x550)==1 && Display13FBMemory::field<uint32_t>(&port,0x554)==2);
 assert(c.dsiPort==&port && port.refs==2);
 assert(XE::wrapAlderLakeNPortState(&port)==2 && port.refs==2);
 assert(XE::setDSIPortMode(&port,0)==kIOReturnUnsupported && Display13FBMemory::field<uint32_t>(&port,0x550)==1);
 state.suspended=true;assert(!XE::wrapAlderLakeNPortState(&port));state.suspended=false;
 assert(port.refs==2 && c.dsiPort==&port && !nativeCalls && !providerCalls);
 assert(XE::wrapAlderLakeNPortState(&peer)==1 && nativeCalls==1 && providerCalls==1);
 assert(!lock.held);if(image)munmap(image,0x400000);
 puts("PASS production DSI port/probe: source and lease qualification, no HPD/AUX/reset, retained identity, external fallback");
}
'''.replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-port-') as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    command=['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')]
    if donor: command[1:1]=['-arch','x86_64']
    subprocess.run(command,check=True)
    subprocess.run([str(p/'test')]+([str(donor)] if donor else []),check=True)
