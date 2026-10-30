#!/usr/bin/env python3
"""Qualify DSI boot recognition; optionally replay the exact native consumer."""
from pathlib import Path
from production_source import read_source
import hashlib, re, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
source=read_source(root/'WhateverXe/kern_xe.cpp')
body=re.search(r'uint32_t XE::wrapDSIBootProbe\(.*?\n\}',source,re.S)[0]
boot=(root/'tests/dsi_boot.cpp').read_text()
io=boot[boot.index('struct IO {'):boot.index('Boot capture(')]
donor=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else None
if donor:
    b=donor.read_bytes()
    assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
    installer=re.search(r'bool XE::processDisplay13DSIBoot\(.*?\n\}',source,re.S)[0]
    for name,offset in [('entry',0x453d6),('call',0x4362a)]:
        expected=bytes(int(x,0) for x in re.search(name+r'\[\]=\{([^}]+)',installer)[1].split(','))
        assert b[offset:offset+len(expected)]==expected
    for at,target in [(0x4362a,0x453d6),(0x436bc,0x1b1ea),(0x436ea,0x66ea0)]:
        assert b[at]==0xe8 and at+5+int.from_bytes(b[at+1:at+5],'little',signed=True)==target
code=r'''
#include "kern_dsi_boot.hpp"
#include "kern_display13_power_bridge.hpp"
#include "kern_display13_fb_topology.hpp"
#include "kern_display13_transcoder.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <fstream>
#include <sys/mman.h>
using namespace XELP::DSI;
IO_FIXTURE
struct OSObject {alignas(8) uint8_t bytes[0x4B90]{};unsigned size=sizeof(bytes);
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}};
using IOService=OSObject;
struct IOLock {bool held=false;};void IOLockLock(IOLock *l){assert(!l->held);l->held=true;}
void IOLockUnlock(IOLock *l){assert(l->held);l->held=false;}
namespace Display13FBMemory {template<class T>T &field(void *p,unsigned o){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+o);}}
struct Display13FBBootIO {IO *map;bool readable(uint32_t r){return map && map->readable(r);}uint32_t read(uint32_t r){return map->read(r);}};
struct {IOLock *lock;IOService *controller;OSObject *access,*dsiPort;IO *map;
 Display13Transcoder::Bindings transcoders;Readout bootDSI;Display13Power::NativeBridge bridge;
 bool memoryFaulted=false,clockFaulted=false;} display13FBPower;
XELPMemory::Layout xelpMemory;bool display13Offline=false;unsigned fallbacks=0,allocations=0;
IOService *nativeFB;OSObject *nativePort;
uint32_t nativeProbe(IOService*,bool *internal,uint32_t *ddi){++fallbacks;if(internal)*internal=false;if(ddi)*ddi=4;return 2;}
OSObject *getPort(uint32_t ddi){assert(ddi==0);return nativePort;}
bool allocate(IOService *fb,uint32_t port){assert(fb==nativeFB && port==0);++allocations;return true;}
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct XE {
 bool dsiBootProbeReady=true,dsiResourcesReady=true;IOService *adlnTopologyController;
 Display13OutputWiring adlnOutputWiring;alignas(8) uint8_t adlnPlatform[0x1C0]{};
 uintptr_t orgDSIBootProbe=reinterpret_cast<uintptr_t>(nativeProbe);Boot dsiBoot;
 static uint32_t wrapDSIBootProbe(IOService *,bool *,uint32_t *);
} xe,*callbackXE=&xe;
BODY
void jump(void *image,unsigned offset,uintptr_t fn){auto p=static_cast<uint8_t*>(image)+offset;
 const uint8_t prefix[]={0xff,0x25,0,0,0,0};memcpy(p,prefix,6);memcpy(p+6,&fn,8);}
int main(int argc,char **argv){
 IOLock lock;IO io;IOService controller,other,fb;OSObject port,access;
 auto &c=display13FBPower;c.lock=&lock;c.map=&io;c.controller=xe.adlnTopologyController=&controller;
 c.access=&access;c.dsiPort=&port;c.transcoders.dsi0Pipe=0;
 xe.adlnOutputWiring.valid=true;xe.adlnOutputWiring.dsi=1;xe.adlnOutputWiring.hdmi=2;xe.adlnOutputWiring.ddc[1]=2;
 xe.adlnPlatform[0x12]=9;uint32_t panel[]={0,0,0,0,2,0x18};memcpy(xe.adlnPlatform+0x20,panel,sizeof(panel));
 auto config=reinterpret_cast<uint32_t*>(xe.adlnPlatform+0x20);
 Display13FBMemory::field<uint32_t*>(&port,0x548)=config;
 Display13FBMemory::field<void*>(&port,0x578)=&access;Display13FBMemory::field<uint32_t>(&port,0x550)=1;
 xelpMemory=io.memory;xe.dsiBoot=captureBoot(io,AlderLakeN::readDisplay(io,true),profile(),io.memory,true);
 assert(verifyBootPrefix(io,xe.dsiBoot,io.memory));c.bootDSI=xe.dsiBoot.source;
 io.regs[0x45454]=3;io.regs[0x42000]=0x8E380000;
 auto &b=c.bridge;auto &s=b.state;b.begun=true;b.selectedPorts=3;s.ready=true;s.allowed=Display13Power::All;
 const auto pg=Display13Power::dependencies(Display13Power::nativeScope(1));
 const auto ddi=Display13Power::dependencies(Display13Power::ddiDomain(0));s.borrowed=pg|ddi;
 for(unsigned i=0;i<Display13Power::Count;++i)if(s.borrowed&(1U<<i))s.refs[i]=2;
 b.pgRefs[0]=1;b.pgLease[0].owner=&s;b.pgLease[0].domains=pg;
 b.ddiRefs[0]=1;b.ddiLease[0].owner=&s;b.ddiLease[0].domains=ddi;
 const auto reject=[&]{bool internal=true;uint32_t d=3;
  assert(XE::wrapDSIBootProbe(&controller,&internal,&d)==0xffff && !internal && d==UINT32_MAX && !lock.held && !fallbacks);};
 xe.dsiBootProbeReady=false;reject();xe.dsiBootProbeReady=true;
 xe.dsiResourcesReady=false;reject();xe.dsiResourcesReady=true;
 display13Offline=true;reject();display13Offline=false;
 b.ddiRefs[0]=0;reject();b.ddiRefs[0]=1;
 s.suspended=true;reject();s.suspended=false;
 c.memoryFaulted=true;reject();c.memoryFaulted=false;
 c.controller=&other;reject();c.controller=&controller;
 c.dsiPort=nullptr;reject();c.dsiPort=&port;
 c.map=nullptr;reject();c.map=&io;
 port.size=0x580;reject();port.size=sizeof(port.bytes);
 Display13FBMemory::field<void*>(&port,0x578)=&other;reject();Display13FBMemory::field<void*>(&port,0x578)=&access;
 uint32_t copy[6];memcpy(copy,config,sizeof(copy));Display13FBMemory::field<uint32_t*>(&port,0x548)=copy;
 reject();Display13FBMemory::field<uint32_t*>(&port,0x548)=reinterpret_cast<uint32_t*>(1);
 reject();Display13FBMemory::field<uint32_t*>(&port,0x548)=config;
 io.regs[0x6B000]^=1;reject();io.regs[0x6B000]^=1;
 io.regs[0x7019C]=4096;reject();io.regs[0x7019C]=0;
 bool internal=false;uint32_t d=3;
 assert(XE::wrapDSIBootProbe(&controller,&internal,&d)==0 && internal && d==0);
 assert(XE::wrapDSIBootProbe(&controller,nullptr,nullptr)==0 && io.writes.empty());
 xe.adlnOutputWiring.dsi=0;assert(XE::wrapDSIBootProbe(&controller,&internal,&d)==2 && !internal && d==4 && fallbacks==1);
 xe.adlnOutputWiring.dsi=1;
 if(argc>1){
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
  void *image=mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);assert(image!=MAP_FAILED);
  memcpy(image,bytes.data(),bytes.size());jump(image,0x453d6,reinterpret_cast<uintptr_t>(XE::wrapDSIBootProbe));
  jump(image,0x1b1ea,reinterpret_cast<uintptr_t>(getPort));jump(image,0x66ea0,reinterpret_cast<uintptr_t>(allocate));
  nativeFB=&fb;nativePort=&port;
  Display13FBMemory::field<uint32_t>(&controller,0xD5C)=1;
  Display13FBMemory::field<IOService*>(&controller,0xD60)=&fb;
  Display13FBMemory::field<IOService*>(&fb,0x1D0)=&controller;
  auto setup=reinterpret_cast<uint32_t(*)(IOService*)>(static_cast<uint8_t*>(image)+0x4360a);
  for(unsigned builtin=0;builtin<2;++builtin){
   Display13FBMemory::field<uint8_t>(&controller,0xE55)=builtin;
   assert(!setup(&controller) && allocations==builtin+1);
   assert(Display13FBMemory::field<uint32_t>(&controller,0xD58)==0 && fb.bytes[0x44DE]==1 && fb.bytes[0x1E0]==1);
  }
  xe.dsiResourcesReady=false;assert(!setup(&controller) && allocations==2);
  assert(Display13FBMemory::field<uint32_t>(&controller,0xD58)==0xffff);
  munmap(image,0x400000);
 }
 assert(!lock.held && io.writes.empty());
 puts("PASS DSI boot probe: qualified source/leases/port, cleared failure outputs, ordinary fallback and zero MMIO writes");
 if(argc>1)puts("PASS exact donor setupBootDisplay: pipe/DDI 0, internal FB selection, native allocation callback and rejected boot skips allocation");
}
'''.replace('IO_FIXTURE',io).replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='dsi-boot-probe-') as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')]+([str(donor)] if donor else []),check=True)
