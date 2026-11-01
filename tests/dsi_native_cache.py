#!/usr/bin/env python3
"""Execute DSI cache hooks with real binding/boot helpers and bounded fake objects."""
from pathlib import Path
from production_source import read_source
import hashlib,re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
names=['updateDSIBootCache','wrapDSIUpdateCache','wrapDSIGetCRTC']
functions='\n'.join(re.search(r'(?:bool|void) XE::'+n+r'\([^;{]*\{.*?\n\}',s,re.S)[0] for n in names)
functions=re.search(r'static bool dsiCachePath\([^;{]*\{.*?\n\}',s,re.S)[0]+'\n'+functions
binding=re.search(r'static TGLFBBinding::Readout adlnInspectPathBinding\([^;{]*\{.*?\n\}',s,re.S)[0]
boot=(root/'tests/dsi_boot.cpp').read_text();io=boot[boot.index('struct IO {'):boot.index('Boot capture(')]
# Non-source cache-word drift must be caught by the two snapshots as well.
io=io.replace('bool driftOnWrite=false,strict=false;','bool driftOnWrite=false,strict=false,cacheDrift=false;unsigned cacheReads=0;')
io=io.replace('reads.push_back(reg);return regs[reg];','reads.push_back(reg);if(cacheDrift && reg==0x701CC && ++cacheReads==2)++regs[reg];return regs[reg];')
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes();assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 install=re.search(r'bool XE::processDisplay13DSICache\([^;{]*\{.*?\n\}',s,re.S)[0]
 offsets=[int(x,0) for x in re.search(r'offsets\[\]=\{([^}]+)',install)[1].split(',')]
 entries=re.search(r'entries\[\]\[16\]=\{(.*?)\};',install,re.S)[1]
 for offset,row in zip(offsets,re.findall(r'\{([^}]+)\}',entries)):
  assert b[offset:offset+16]==bytes(int(x,0) for x in row.split(','))
 # Native path+34 is pipe CSC mode, not DDI_BUF_CTL. Scaler tables use -10/-C.
 assert b[0x91315:0x9131e]==bytes.fromhex('8d9080a40400895734')
 import struct
 assert struct.unpack_from('<2I',b,0x99f30)==(0x68180,0x68280)
 assert struct.unpack_from('<2I',b,0x99f48)==(0x68170,0x68270)
 assert struct.unpack_from('<2I',b,0x99f60)==(0x68174,0x68274)
code=r'''
#include "kern_dsi_native_cache.hpp"
#include "kern_tgl_fb_binding.hpp"
#include "kern_display13_power_bridge.hpp"
#include "kern_display13_transcoder.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace XELP::DSI;
IO_FIXTURE
struct OSObject {alignas(8) uint8_t bytes[0x4B90]{};unsigned size=sizeof(bytes);
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}};
using IOService=OSObject;
unsigned level=0,faults=0,nativeCache=0,nativeCRTC=0;
struct IOLock{};struct IORecursiveLock{};
void IOLockLock(IOLock*){assert(level==0 || level==1);level+=2;}void IOLockUnlock(IOLock*){assert(level>=2);level-=2;}
void adlnFault(bool &flag,unsigned){assert(level>=2);flag=true;++faults;}
struct DSIModeScope {IORecursiveLock *lock;explicit DSIModeScope(IOService *fb):lock(Display13FBMemory::field<IORecursiveLock*>(fb,0x4220)){assert(level==0);if(lock)level=1;}
 ~DSIModeScope(){assert(level==(lock?1U:0U));level=0;}};
struct Display13FBBootIO {IO *map;bool readable(uint32_t r){return map && map->readable(r);}uint32_t read(uint32_t r){return map->read(r);}};
struct {IOLock *lock;IOService *controller;OSObject *access,*dsiPort;IO *map;
 Display13Transcoder::Bindings transcoders;Readout bootDSI;Display13Power::NativeBridge bridge;
 bool memoryFaulted=false,clockFaulted=false;} display13FBPower;
uintptr_t display13FBExpectedOwner(IOService*){return 0;}
uintptr_t dsiLogicalSurface(const void*){return 0;}
BINDING
XELPMemory::Layout xelpMemory;bool display13Offline=false;
void originalCache(IOService*,IOService*,OSObject*){++nativeCache;}
void originalCRTC(IOService*,IOService*,OSObject*){++nativeCRTC;}
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
bool publishedBacking=false;bool dsiPublishedTarget(IOService*){return publishedBacking;}
struct XE {
 bool dsiCacheHooksReady=true,dsiResourcesReady=true;IOService *adlnTopologyController;
 Display13OutputWiring adlnOutputWiring;alignas(8) uint8_t adlnPlatform[0x1C0]{};
 uintptr_t orgDSICache[2]{reinterpret_cast<uintptr_t>(originalCache),reinterpret_cast<uintptr_t>(originalCRTC)};Boot dsiBoot;
 bool updateDSIBootCache(IOService *,IOService *,OSObject *);
 static void wrapDSIUpdateCache(IOService *,IOService *,OSObject *);
 static void wrapDSIGetCRTC(IOService *,IOService *,OSObject *);
} xe,*callbackXE=&xe;
FUNCTIONS
template<class T>void put(void *p,unsigned off,T value){memcpy(static_cast<uint8_t*>(p)+off,&value,sizeof(value));}
int main(){
 IOLock lock;IORecursiveLock fbLock;IO io;IOService controller,fb;OSObject path,plane,port,scaler,access;
 auto &c=display13FBPower;c.lock=&lock;c.map=&io;c.controller=xe.adlnTopologyController=&controller;
 c.access=&access;c.dsiPort=&port;c.transcoders.dsi0Pipe=0;
 xe.adlnOutputWiring.valid=true;xe.adlnOutputWiring.dsi=1;xe.adlnOutputWiring.hdmi=2;xe.adlnOutputWiring.ddc[1]=2;
 xe.adlnPlatform[0x12]=9;uint32_t panel[]={0,0,0,0,2,0x18};memcpy(xe.adlnPlatform+0x20,panel,sizeof(panel));
 uint32_t hdmi[]={1,2,2,0,0x800,0x202};memcpy(xe.adlnPlatform+0x38,hdmi,sizeof(hdmi));
 put(&port,0x548,reinterpret_cast<uint32_t*>(xe.adlnPlatform+0x20));put(&port,0x550,1U);
 put(&controller,0xD5C,1U);put(&controller,0xD60,&fb);put(&controller,0xC40,&access);put(&controller,0xF58,&plane);
 put(&fb,0x1D0,&controller);put(&fb,0x4A40,&access);put(&fb,0x4220,&fbLock);
 put(&path,0x32B0,&fb);put(&path,0x32C8,&plane);put(&path,0x3648,&port);put(&path,0x32C0,&scaler);put(&path,0x34,0x4A480U);
 put(&plane,0x68,&fb);put(&plane,0x70,&path);put(&plane,0x90,&access);put(&plane,0xA0,0x7027CU);put(&plane,0xA4,0x7017CU);
 for(unsigned i=0;i<6;++i)put(&plane,NativeCache::PlaneAddressFields[i],NativeCache::PlaneRegisters[i]);
 put(&scaler,0x28,&access);put(&scaler,0x30,0x68180U);put(&scaler,0x34,0x68170U);put(&scaler,0x38,0x68174U);
 xelpMemory=io.memory;xe.dsiBoot=captureBoot(io,AlderLakeN::readDisplay(io,true),profile(),io.memory,true);
 assert(verifyBootPrefix(io,xe.dsiBoot,io.memory));c.bootDSI=xe.dsiBoot.source;
 io.regs[0x45454]=3;io.regs[0x42000]=0x8E380000;
 io.regs[0x6B010]=c.bootDSI.vtotal;io.regs[0x6001C]=0x0437077F;io.regs[0x70030]=0x20;
 io.regs[0x4A480]=0x1234;io.regs[0x701CC]=0x5000;io.regs[0x68170]=0x11;io.regs[0x68174]=0x22;
 auto &b=c.bridge;auto &s=b.state;b.begun=true;b.selectedPorts=3;s.ready=true;s.allowed=Display13Power::All;
 const auto pg=Display13Power::dependencies(Display13Power::nativeScope(1));
 const auto ddi=Display13Power::dependencies(Display13Power::ddiDomain(0));s.borrowed=pg|ddi;
 for(unsigned i=0;i<Display13Power::Count;++i)if(s.borrowed&(1U<<i))s.refs[i]=2;
 b.pgRefs[0]=1;b.pgLease[0].owner=&s;b.pgLease[0].domains=pg;
 b.ddiRefs[0]=1;b.ddiLease[0].owner=&s;b.ddiLease[0].domains=ddi;
 const auto unchanged=[&]{auto before=path,oldPlane=plane,oldScaler=scaler;
  assert(!xe.updateDSIBootCache(&controller,&fb,&path));
  assert(!memcmp(path.bytes,before.bytes,sizeof(path.bytes)) && !memcmp(plane.bytes,oldPlane.bytes,sizeof(plane.bytes)) &&
   !memcmp(scaler.bytes,oldScaler.bytes,sizeof(scaler.bytes)) && !level && !c.memoryFaulted);};
 b.ddiRefs[0]=0;unchanged();b.ddiRefs[0]=1;
 xe.dsiResourcesReady=false;unchanged();xe.dsiResourcesReady=true;
 put(&path,0x34,0x64000U);unchanged();put(&path,0x34,0x4A480U);
 put(&plane,0x9C,0x70184U);unchanged();put(&plane,0x9C,0x701CCU);
 put(&scaler,0x34,0x68190U);unchanged();put(&scaler,0x34,0x68170U);
 io.regs[0x6001C]^=1;unchanged();io.regs[0x6001C]^=1;
 io.regs[0x6B010]^=1;unchanged();io.regs[0x6B010]^=1;
 io.missing=0x701CC;unchanged();io.missing=UINT32_MAX;
 io.cacheDrift=true;unchanged();io.cacheDrift=false;
 assert(xe.updateDSIBootCache(&controller,&fb,&path));
 assert(Display13FBMemory::field<uint32_t>(&path,0x138)==c.bootDSI.htotal && Display13FBMemory::field<uint32_t>(&path,0x144)==c.bootDSI.vtotal);
 assert(!Display13FBMemory::field<uint32_t>(&path,0x13C) && !Display13FBMemory::field<uint32_t>(&path,0x12C) && !Display13FBMemory::field<uint32_t>(&path,0x168));
 assert(Display13FBMemory::field<uint32_t>(&path,0x18C)==0x1234 && Display13FBMemory::field<uint32_t>(&plane,0x104)==0x5001);
 assert(Display13FBMemory::field<uint32_t>(&scaler,0x48)==0x11 && Display13FBMemory::field<uint32_t>(&scaler,0x4C)==0x22);
 XE::wrapDSIGetCRTC(&controller,&fb,&path);assert(!nativeCache && !nativeCRTC && !faults);
 auto originalRegs=io.regs;publishedBacking=true;
 io.regs[0x7019C]=0x40400000;io.regs[0x70180]=0x94001008;io.regs[0x70188]=34;io.regs[0x70080]=0x27;
 assert(xe.updateDSIBootCache(&controller,&fb,&path));
 assert(Display13FBMemory::field<uint32_t>(&plane,0x120)==0x40400000 && Display13FBMemory::field<uint32_t>(&path,0x170)==0x27);
 io.regs[0x6B000]^=1;unchanged();io.regs=originalRegs;publishedBacking=false;
 assert(xe.updateDSIBootCache(&controller,&fb,&path));
 b.ddiRefs[0]=0;XE::wrapDSIGetCRTC(&controller,&fb,&path);assert(faults==1 && c.memoryFaulted);b.ddiRefs[0]=1;c.memoryFaulted=false;
 put(&path,0x3640,1U);put(&port,0x120,1U);XE::wrapDSIGetCRTC(&controller,&fb,&path);XE::wrapDSIUpdateCache(&controller,&fb,&path);
 assert(nativeCRTC==1 && nativeCache==1);
 put(&path,0x3640,3U);XE::wrapDSIGetCRTC(&controller,&fb,&path);
 assert(nativeCRTC==1 && c.memoryFaulted && faults==2);c.memoryFaulted=false;
 XE::wrapDSIUpdateCache(&controller,&fb,nullptr);assert(nativeCache==1 && c.memoryFaulted && faults==3);
 xe.adlnOutputWiring.dsi=0;
 XE::wrapDSIGetCRTC(&controller,&fb,nullptr);assert(nativeCRTC==2);
 assert(io.writes.empty() && !level);for(auto reg:io.reads)assert(reg<0xC7200 || reg>0xC8258);
 puts("PASS production DSI cache: native object binding, split timing/pipe fields, coherent snapshots, no partial publication, no PWM, failure latch and external fallback");
}
'''.replace('IO_FIXTURE',io).replace('BINDING',binding).replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-native-cache-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
