#!/usr/bin/env python3
"""An unpublished DSI aperture stays unavailable while private scanout is armed."""
from pathlib import Path
from production_source import read_source
import hashlib,re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
body=re.search(r'IOMemoryDescriptor \*XE::wrapAlderLakeNAperture\(.*?\n\}',s,re.S)[0]
code=r'''
#include "kern_display13_fb_memory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <sys/mman.h>
using namespace Display13FBMemory;
using IOByteCount=uint64_t;
unsigned depth=0;struct IOLock{};void IOLockLock(IOLock*){assert(!depth++);}void IOLockUnlock(IOLock*){assert(depth--==1);}
struct OSObject {alignas(8) uint8_t data[0x4B90]{};unsigned refs=1;OSObject *heldParent=nullptr;uint64_t length=0x10000000,address=0x80000000;
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return sizeof(data);}
 uint64_t getLength(){return length;}uint64_t getPhysicalSegment(uint64_t off,uint64_t *span,unsigned){*span=getLength()-off;return address+off;}
 void retain(){++refs;}void release(){assert(refs>1);if(--refs==1 && heldParent){heldParent->release();heldParent=nullptr;}}
};
using IOService=OSObject;using IOMemoryDescriptor=OSObject;
#define OSDynamicCast(T,p) static_cast<T*>(p)
#define OSSafeReleaseNULL(p) do{if(p)(p)->release();p=nullptr;}while(0)
constexpr unsigned kIODirectionInOut=1,kIOMemoryThreadSafe=2,kIOMemoryMapperNone=0;
IOMemoryDescriptor child;unsigned returnedBytes=0;bool shortDescriptor=false;
struct IOSubMemoryDescriptor {
 static IOMemoryDescriptor *withSubRange(IOMemoryDescriptor *parent,unsigned base,unsigned bytes,unsigned options){
  assert(!depth && parent->refs==2 && options==(kIODirectionInOut|kIOMemoryThreadSafe));
  returnedBytes=bytes;child.length=bytes-shortDescriptor;child.address=parent->address+base;
  assert(!child.heldParent);child.heldParent=parent;parent->retain();child.retain();return &child;
 }
};
struct {IOLock *lock;void *controller;bool memoryFaulted=false;
 struct {IOService *memoryFramebuffer=nullptr;struct {Pool pool;}memory;}pipe[3];}display13FBPower;
bool associationOK=true,target=true,pending=false;
bool display13FBAssociations(void*,unsigned,void*,unsigned,uintptr_t){return associationOK;}
uintptr_t display13FBExpectedOwner(void*){return 1;}
bool dsiModeTarget(IOService*){return target;}bool dsiPendingTarget(IOService*){return pending;}
namespace Display13FBSysMem {enum class Status{Complete};}
struct Inspection{Display13FBSysMem::Status status=Display13FBSysMem::Status::Complete;};
Inspection adlnInspectSysMem(IOService*,IOService*,unsigned,const PoolLayout&){assert(false);return {};}
void adlnPublishMemory(const char*,unsigned,void*,unsigned,bool){}
void adlnFault(bool &v,unsigned){v=true;}
struct XE {bool display13FBMemoryHooksReady=true,dsiStopAttempted=false;struct {unsigned dsi=1;}adlnOutputWiring;
 static IOMemoryDescriptor *wrapAlderLakeNAperture(IOService*,int);
}*callbackXE;
BODY
template<class T>void put(void *p,unsigned off,T v){memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
int main(int argc,char **argv){
 uint8_t *image=nullptr;
 if(argc>1){
  std::ifstream in(argv[1],std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),{});
  image=static_cast<uint8_t*>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));
  assert(image!=MAP_FAILED && data.size()<0x400000);memcpy(image,data.data(),data.size());
 }

 for(unsigned problem=0;problem<7;++problem){IOService fb,controller,parent;IOLock lock;XE xe;callbackXE=&xe;
  display13FBPower={};auto &c=display13FBPower;c.lock=&lock;c.controller=&controller;c.pipe[0].memoryFramebuffer=&fb;
  put(&fb,0x1E0,uint8_t(1));put(&fb,0x4288,uint8_t(0));put(&controller,0xDF0,1U);
  put(&controller,0xDF4,uint8_t(1));put(&controller,0xDF5,uint8_t(3));put(&controller,0x1A10,1U);
  put(&controller,0x1A28,uint64_t(0x80000000));put(&controller,0x1A30,0x800000U);put(&controller,0x1A34,0x800U);
  put(&controller,0xC80,&parent);c.pipe[0].memory.pool=pool(&controller);
  pending=problem!=0;target=true;
  switch(problem){case 2:put(&fb,0x4314,1U);break;case 3:put(&fb,0x4330,1U);break;
   case 4:c.pipe[0].memory.pool.pages++;break;case 5:target=false;break;case 6:xe.dsiStopAttempted=true;break;}
  assert(!xe.wrapAlderLakeNAperture(&fb,0) && !depth && parent.refs==1);
  assert(c.memoryFaulted==(problem>=2));
 }
 // Reproduce the reported byte count at the native producer and check its last page.
 constexpr unsigned pitch=8960, height=1896, bytes=pitch*height;
 static_assert(bytes==0x1033800,"dimensions");
 for(unsigned i=0;i<6;++i) {
  IOService fb,ctl,parent;IOLock lock;XE xe;callbackXE=&xe;auto &c=display13FBPower;c={};
  c.lock=&lock;c.controller=&ctl;c.pipe[0].memoryFramebuffer=&fb;
  const unsigned base=4096,pages=(bytes+4095)/4096,rounded=pages*4096;
  put(&fb,0x1e0,uint8_t(1));put(&fb,0x4314,bytes);put(&fb,0x4330,base);
  put(&fb,0x4334,pitch);put(&fb,0x4324,height+(i==3));
  put(&fb,0x1d0,&ctl);put(&ctl,0xdf0,3U);put(&ctl,0xdf4,uint8_t(2));put(&ctl,0xdf5,uint8_t(3));
  put(&ctl,0x1a10,1U);put(&ctl,0x1a14,base);
  put(&ctl,0x1a30,rounded-(i==1?4096:0));put(&ctl,0x1a34,pages-(i==1));
  parent.length=uint64_t(base)+rounded-(i==2);
  put(&ctl,0xc80,&parent);c.pipe[0].memory.pool=pool(&ctl);
  if(i==4)put(&fb,0x4324,UINT32_MAX);
  shortDescriptor=i==5;returnedBytes=0;auto result=xe.wrapAlderLakeNAperture(&fb,0);
  if(!i){
   assert(result==&child && returnedBytes==bytes && child.length==bytes && parent.refs==2);
   if(image){
    // The donor returns only the claimed slot; an outstanding client
    // descriptor still holds the BAR, independently of this bitmap.
    reinterpret_cast<void(*)(void*)>(image+0x68c92)(&fb);
    assert(slotReturned(3,0,2,3,field<uint8_t>(&ctl,0xdf4),field<uint32_t>(&ctl,0xdf0),field<uint8_t>(&fb,0x4288)));
    assert(same(c.pipe[0].memory.pool,pool(&ctl)) && parent.refs==2);
   }
   result->release();
  } else assert(!result && (returnedBytes!=0)==shortDescriptor);
  assert(parent.refs==1 && child.refs==1 && !child.heldParent && !depth && c.memoryFaulted==(i>0 && i<5));
 }
 puts("PASS production DSI aperture: zero unpublished range before/after private surface stays unavailable; byte-exact partial-page subrange; pool/BAR bounds, mismatched dimensions and reference balance");
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='dsi-aperture-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1',*(['-arch','x86_64'] if len(sys.argv)>1 else ['-fsanitize=address,undefined']),'-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 args=[]
 if len(sys.argv)>1:
  donor=Path(sys.argv[1]);assert hashlib.sha256(donor.read_bytes()).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
  args.append(str(donor))
 subprocess.run([str(p/'test'),*args],check=True)
