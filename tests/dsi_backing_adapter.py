#!/usr/bin/env python3
"""Exercise the production pending-backing adapter with bounded native leaves."""
from pathlib import Path
from production_source import read_source
import hashlib,re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
functions='\n'.join(re.search(r'(?:bool|uint32_t|void) XE::'+name+r'\(.*?\n\}',s,re.S)[0] for name in
 ['wrapDSIBackingAllocate','wrapDSIBackingFree','prepareDSINativeBacking','validateDSINativeBacking','initializeDSINativeBacking','discardDSINativeBacking','probeDSINativeBacking'])
state=re.search(r'struct DSIPendingBackingOwner \{.*?\n\};',s,re.S)[0]
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes();assert hashlib.sha256(b).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
 installer=re.search(r'bool XE::processDSIBackingHooks\(.*?\n\}',s,re.S)[0]
 offsets=[int(x,0) for x in re.search(r'offsets\[\]=\{([^}]+)',installer)[1].split(',')]
 entries=re.search(r'entries\[\]\[16\]=\{(.*?)\};',installer,re.S)[1]
 for offset,row in zip(offsets,re.findall(r'\{([^}]+)\}',entries)):
  assert b[offset:offset+16]==bytes(int(x,0) for x in row.split(','))
 assert 'routeMultipleShort' in installer
code=r'''
#include "kern_dsi_native_backing.hpp"
#include "kern_dsi_quiesce.hpp"
#include "kern_dsi_clock.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace XELP::DSI;
using namespace Display13FBMemory;
constexpr uint32_t kIOReturnNotReady=0xE00002D8;
template<class T>void put(void *p,unsigned off,T v){memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
unsigned fbDepth=0,ownerDepth=0,gpuDepth=0,allocCalls=0,freeCalls=0,buffers=0,expectedFlags=0;
uintptr_t thread=1;void *current_thread(){return reinterpret_cast<void*>(thread);}
struct IOLock{};struct IORecursiveLock{};
void IOLockLock(IOLock*){assert(fbDepth && !ownerDepth && gpuDepth<=1);++ownerDepth;}
void IOLockUnlock(IOLock*){assert(ownerDepth==1);--ownerDepth;}
void *IOMalloc(size_t n){assert(!ownerDepth && !gpuDepth);++buffers;return calloc(1,n);}
void IOFree(void *p,size_t n){assert(!ownerDepth && !gpuDepth && n==64 && buffers);--buffers;free(p);}
alignas(4096) uint32_t cpuImage[8355840/4];
bool failCPUMap=false;unsigned cpuMaps=0;
constexpr unsigned kIOMapAnywhere=1;void *kernel_task=reinterpret_cast<void*>(1);
struct CPUMap {
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(cpuImage);}
 uint64_t getLength(){return sizeof(cpuImage);}
 void release(){assert(gpuDepth==1 && !ownerDepth && cpuMaps==1);--cpuMaps;}
}cpuMap;
struct OSObject {
 alignas(8) uint8_t bytes[0x4B90]{};unsigned size=sizeof(bytes),refs=1;const char *kind="object";
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}
 bool metaCast(const char *name){return !strcmp(kind,name);}void retain(){++refs;}void release(){assert(refs>1);--refs;}
 uint32_t callPlatformFunction(const char*,bool,void*,void*,void*,void*);
 uint64_t getLength(){return sizeof(cpuImage);}
 CPUMap *createMappingInTask(void *task,uintptr_t address,unsigned options,uint64_t offset,uint64_t bytes){
  assert(gpuDepth==1 && fbDepth && !ownerDepth && !cpuMaps && task==kernel_task && !address && options==kIOMapAnywhere && !offset && bytes==sizeof(cpuImage));
  if(failCPUMap)return nullptr;++cpuMaps;return &cpuMap;}

};
using IOService=OSObject;using IOMemoryDescriptor=OSObject;
#define OSDynamicCast(T,p) static_cast<T *>(p)
uint64_t receipt[16]{};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry root;return &root;}
 void setProperty(const char *name,const void *p,unsigned n){assert(!strcmp(name,"WEXDSIBackingTrial") && n==sizeof(receipt));memcpy(receipt,p,n);}};
OSObject nativeMapping,nativeMemory,nativeDescriptor;
IOService *modeFB=nullptr;bool dsiModeTarget(IOService *fb){return fb && modeFB==fb;}
struct DSIModeScope {IORecursiveLock *lock;explicit DSIModeScope(IOService *fb):lock(field<IORecursiveLock*>(fb,0x4220)){if(lock)++fbDepth;}
 ~DSIModeScope(){assert(!ownerDepth && !gpuDepth);if(lock){assert(fbDepth);--fbDepth;}}};
STATE
DSIPendingBackingOwner dsiPendingBacking;
bool dsiPendingTarget(IOService *fb){return fb && dsiPendingBacking.framebuffer==fb;}
struct Map {uintptr_t getVirtualAddress(){return 1;}uint64_t getLength(){return 0x1000000;}};
struct {IOLock *lock;IOService *controller;Map *map;unsigned dsiNativeWriters=0;bool memoryFaulted=false,clockFaulted=false;}display13FBPower;
struct {uint32_t sharedPoolPrefix=0x6192000;}display13FBGTT;
XELPMemory::Layout xelpMemory=XELPMemory::decode(0x03C1,0x7A800001,0);
bool binding=true,source=true,available=true,badPTE=false,failAllocate=false,failFree=false,dropMap=false,changePipe=false,detachAfterAllocate=false;
OSObject *nativePipe=nullptr,*alternatePipe=nullptr;
uintptr_t display13FBExpectedOwner(IOService *controller){return field<uintptr_t>(controller,0xC0);}
bool adlnSharedBinding(void *controller,void *owner){return binding && controller==display13FBPower.controller && field<void*>(controller,0xC0)==owner;}
bool adlnSysMemOwner(void*,const Pool&){assert(ownerDepth);return true;}
Display13FBMemory::Readout display13FBInspectMemory(IOService*,bool,unsigned,bool){assert(ownerDepth);Display13FBMemory::Readout r;r.status=source?Status::Complete:Status::PTE;return r;}
struct Display13SurfaceIO {
 Map *map;unsigned pipe;bool memoryReady(uintptr_t m,uintptr_t mem,uintptr_t owner,uint64_t n){
  assert(ownerDepth && gpuDepth==1 && map);return m==reinterpret_cast<uintptr_t>(&nativeMapping) && mem==reinterpret_cast<uintptr_t>(&nativeMemory) && owner && n==8355840;}
 uint64_t physical(uintptr_t,uint64_t off,uint64_t &span){span=4096;return 0x180000000ULL+off;}
 uint64_t readPTE(uint32_t reg){const uint64_t off=(uint64_t(reg)-0x800000)*512-0x10000000;
  return (0x180000000ULL+off+(badPTE?4096:0))|0x99;}
};
uint32_t nativeAllocate(OSObject *pipe,void *p,uint32_t flags){
 assert(gpuDepth && !ownerDepth && flags==expectedFlags);++allocCalls;if(failAllocate)return 0xE00002BC;
 auto &r=*static_cast<NativeBacking::Record*>(p);r.refs=1;r.map=reinterpret_cast<uintptr_t>(&nativeMapping);r.memory=reinterpret_cast<uintptr_t>(&nativeMemory);r.gpu=0x10000000;r.pitch=4352;
 nativeMapping.retain();nativeMemory.retain();
 put(pipe,0x1298,r.memory);put(pipe,0x12A0,r.map);if(dropMap)display13FBPower.map=nullptr;return 0;
}
uint32_t nativeFree(OSObject *pipe,void *p,uint32_t flags){
 assert(gpuDepth && !ownerDepth && flags==expectedFlags);++freeCalls;if(failFree)return 0xE00002BC;
 auto &r=*static_cast<NativeBacking::Record*>(p);if(r.refs){nativeMapping.release();nativeMemory.release();}r.refs=0;r.map=r.memory=r.gpu=0;r.pitch=0;
 put(pipe,0x1298,uintptr_t(0));put(pipe,0x12A0,uintptr_t(0));return 0;
}
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct XE {
 bool dsiBackingHooksReady=true;Profile dsiProfile;struct{unsigned dsi=1;}adlnOutputWiring;
 uintptr_t orgDSIBacking[2]{reinterpret_cast<uintptr_t>(nativeAllocate),reinterpret_cast<uintptr_t>(nativeFree)};
 static bool dsiNativeUpdateAllowed(IOService *fb){assert(fbDepth);return available && !dsiPendingTarget(fb);}
 static uint32_t wrapDSIBackingAllocate(OSObject*,void*,uint32_t);
 static uint32_t wrapDSIBackingFree(OSObject*,void*,uint32_t);
 bool initializeDSINativeBacking();Session dsiSession;
 bool publishDSINativeBacking(){assert(gpuDepth==1 && fbDepth && !ownerDepth);return true;}
 bool switchDSINativeSurface(){assert(gpuDepth==1 && fbDepth && !ownerDepth);return true;}
 bool probeDSIModeGeometry(IOService*){assert(fbDepth && !ownerDepth && !gpuDepth);return true;}
 bool prepareDSINativeBacking(IOService*);bool validateDSINativeBacking();bool discardDSINativeBacking(IOService*);
 void probeDSINativeBacking(IOService*);bool dsiBackingAttempted=false;uint32_t dsiBackingStage=0;
}*callbackXE;
uint32_t OSObject::callPlatformFunction(const char *name,bool wait,void *record,void *index,void *flags,void *extra){
 assert(fbDepth && !ownerDepth && !gpuDepth && !wait && !extra && *static_cast<uint32_t*>(index)==0);
 ++gpuDepth;uint32_t r;
 if(!strcmp(name,"allocscanmem"))r=XE::wrapDSIBackingAllocate(nativePipe,record,*static_cast<uint32_t*>(flags));
 else {assert(!strcmp(name,"freescanmem"));XE::wrapDSIBackingFree(changePipe?alternatePipe:nativePipe,record,*static_cast<uint32_t*>(flags));r=0;}
 --gpuDepth;if(detachAfterAllocate && !strcmp(name,"allocscanmem"))put(nativePipe,0x1298,uintptr_t(0));return r;
}
FUNCTIONS
struct Case {
 IOService fb,owner,controller,machine,pipe,other;IOLock lock;IORecursiveLock fbLock;Map map;XE xe;
 Case(){
  assert(!dsiPendingBacking.framebuffer && !buffers && !cpuMaps);failCPUMap=false;put(&nativeMemory,0xD0,&nativeDescriptor);callbackXE=&xe;modeFB=&fb;nativePipe=&pipe;alternatePipe=&other;
  binding=source=available=true;badPTE=failAllocate=failFree=dropMap=changePipe=detachAfterAllocate=false;thread=1;
  display13FBPower={&lock,&controller,&map};display13FBGTT.sharedPoolPrefix=0x6192000;
  xe.dsiProfile.status=ProfileStatus::Valid;xe.dsiProfile.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
  owner.size=0x1600;machine.kind="IGAccelDisplayMachine";pipe.kind="IGAccelDisplayPipe";
  put(&owner,0x378,&machine);put(&machine,0x128,&owner);put(&owner,0x1470,3U);put(&owner,0x13C0,display13FBGTT.sharedPoolPrefix);
  put(&controller,0xC0,&owner);put(&controller,0xD5C,1U);put(&controller,0xD60,&fb);put(&fb,0x1D0,&controller);
  put(&controller,0xDF0,1U);put(&controller,0xDF4,uint8_t(1));put(&controller,0xDF5,uint8_t(3));
  put(&controller,0x1A10,1U);put(&controller,0x1A28,xelpMemory.base);put(&controller,0x1A30,0x800000U);put(&controller,0x1A34,0x800U);
  put(&fb,0x4220,&fbLock);put(&pipe,0x88,&owner);put(&other,0x88,&owner);
 }
 ~Case(){assert(!dsiPendingBacking.framebuffer && !buffers && fb.refs==1 && owner.refs==1 && pipe.refs==1 && nativeMapping.refs==1 && nativeMemory.refs==1 && !fbDepth && !ownerDepth && !gpuDepth);}
 void discard(){assert(xe.discardDSINativeBacking(&fb));}
};
int main(){
 {Case c;failCPUMap=true;assert(!c.xe.prepareDSINativeBacking(&c.fb));assert(!dsiPendingBacking.initialized && !cpuMaps);c.discard();}
 {Case c;memset(cpuImage,0xA5,sizeof(cpuImage));assert(c.xe.prepareDSINativeBacking(&c.fb));
  assert(dsiPendingBacking.initialized && !cpuMaps);
  for(auto pixel:cpuImage)assert(pixel==0);
  c.discard();assert(!dsiPendingBacking.initialized);}

 {Case c;const auto a=allocCalls,f=freeCalls;DSIModeScope scope(&c.fb);
  available=false;c.xe.probeDSINativeBacking(&c.fb);assert(!c.xe.dsiBackingAttempted && allocCalls==a && receipt[2]==1);
  available=true;c.xe.probeDSINativeBacking(&c.fb);
  assert(c.xe.dsiBackingAttempted && allocCalls==a+1 && freeCalls==f+1 && !dsiPendingTarget(&c.fb));
  assert(receipt[1]==3 && receipt[2]==6 && receipt[3] && receipt[11]==2040 && receipt[13] && !receipt[14]);
  c.xe.probeDSINativeBacking(&c.fb);assert(allocCalls==a+1 && freeCalls==f+1);
  assert(!field<uintptr_t>(&c.fb,0x4348));}
 {Case c;failFree=true;const auto a=allocCalls;{DSIModeScope scope(&c.fb);c.xe.probeDSINativeBacking(&c.fb);
  assert(c.xe.dsiBackingAttempted && receipt[14] && receipt[15] && !receipt[13]);
  c.xe.probeDSINativeBacking(&c.fb);assert(allocCalls==a+1);}
  failFree=false;c.discard();}

 {Case c;NativeBacking::Record r;assert(NativeBacking::request(c.xe.dsiProfile,3,r));
  gpuDepth=1;expectedFlags=4;assert(!XE::wrapDSIBackingAllocate(&c.pipe,&r,4));
  expectedFlags=2;assert(!XE::wrapDSIBackingFree(&c.pipe,&r,2));
  expectedFlags=0;failAllocate=true;assert(XE::wrapDSIBackingAllocate(&c.pipe,&r,0)==0xE00002BC);gpuDepth=0;}
 {Case c;assert(c.xe.prepareDSINativeBacking(&c.fb));assert(dsiPendingBacking.proof.verifiedPages==2040);
  assert(nativeMapping.refs==3 && nativeMemory.refs==3);
  assert(dsiPendingBacking.pipe==&c.pipe && c.fb.refs==2 && c.owner.refs==2 && c.pipe.refs==2);
  assert(!field<uintptr_t>(&c.fb,0x4348) && allocatedSlot(&c.controller,&c.fb)==0);
  assert(!c.xe.prepareDSINativeBacking(&c.fb));c.discard();assert(!c.xe.discardDSINativeBacking(&c.fb));}
 for(unsigned failure=0;failure<11;++failure){Case c;const auto before=allocCalls;
  switch(failure){case 0:c.xe.dsiBackingHooksReady=false;break;case 1:binding=false;break;case 2:source=false;break;
   case 3:display13FBPower.dsiNativeWriters=1;break;case 4:put(&c.owner,0x1470,2U);break;
   case 5:c.machine.kind="wrong";break;case 6:put(&c.machine,0x128,&c.other);break;
   case 7:put(&c.fb,0x4220,static_cast<IORecursiveLock*>(nullptr));break;case 8:put(&c.owner,0x13C0,0U);break;case 9:modeFB=&c.other;break;case 10:c.xe.adlnOutputWiring.dsi=0;break;}
  assert(!c.xe.prepareDSINativeBacking(&c.fb) && allocCalls==before && !dsiPendingBacking.framebuffer);}
 for(unsigned failure=0;failure<4;++failure){Case c;
  switch(failure){case 0:failAllocate=true;break;case 1:badPTE=true;break;case 2:dropMap=true;break;case 3:put(&c.pipe,0x1298,uintptr_t(7));break;}
  assert(!c.xe.prepareDSINativeBacking(&c.fb) && dsiPendingBacking.framebuffer==&c.fb);c.discard();}
 {Case c;detachAfterAllocate=true;assert(!c.xe.prepareDSINativeBacking(&c.fb));
  assert(nativeMapping.refs==3 && nativeMemory.refs==3 && !c.xe.discardDSINativeBacking(&c.fb));
  put(&c.pipe,0x1298,reinterpret_cast<uintptr_t>(&nativeMemory));c.discard();}
 {Case c;assert(c.xe.prepareDSINativeBacking(&c.fb));failFree=true;
  assert(!c.xe.discardDSINativeBacking(&c.fb) && dsiPendingBacking.record && c.pipe.refs==2);
  failFree=false;changePipe=true;const auto before=freeCalls;assert(!c.xe.discardDSINativeBacking(&c.fb) && freeCalls==before);
  changePipe=false;dsiPendingBacking.armed=true;assert(!c.xe.discardDSINativeBacking(&c.fb));dsiPendingBacking.armed=false;c.discard();}
 {Case c;assert(c.xe.prepareDSINativeBacking(&c.fb));NativeBacking::Record foreign;assert(NativeBacking::request(c.xe.dsiProfile,3,foreign));
  thread=2;fbDepth=gpuDepth=1;const auto before=allocCalls;
  assert(XE::wrapDSIBackingAllocate(&c.pipe,&foreign,0)==kIOReturnNotReady && allocCalls==before);
  assert(XE::wrapDSIBackingFree(&c.pipe,&foreign,0)==kIOReturnNotReady);
  assert(!XE::wrapDSIBackingAllocate(&c.other,&foreign,0) && allocCalls==before+1);
  assert(!XE::wrapDSIBackingFree(&c.other,&foreign,0));fbDepth=gpuDepth=0;thread=1;c.discard();}
 puts("PASS production DSI backing adapter: actual native pipe capture, pending PTE proof, retained failures, discard, peer forwarding and lock boundaries");
}
'''.replace('STATE',state).replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-backing-adapter-') as directory:
 d=Path(directory);(d/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
