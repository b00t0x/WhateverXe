#!/usr/bin/env python3
"""An unpublished DSI aperture stays unavailable while private scanout is armed."""
from pathlib import Path
from production_source import read_source
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
body=re.search(r'IOMemoryDescriptor \*XE::wrapAlderLakeNAperture\(.*?\n\}',s,re.S)[0]
code=r'''
#include "kern_display13_fb_memory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace Display13FBMemory;
using IOByteCount=uint64_t;
unsigned depth=0;struct IOLock{};void IOLockLock(IOLock*){assert(!depth++);}void IOLockUnlock(IOLock*){assert(depth--==1);}
struct OSObject {alignas(8) uint8_t data[0x4B90]{};unsigned refs=1;
 OSObject *getMetaClass(){return this;}unsigned getClassSize(){return sizeof(data);}
 uint64_t getLength(){return 0x10000000;}uint64_t getPhysicalSegment(uint64_t off,uint64_t *span,unsigned){*span=getLength();return 0x80000000+off;}
 void retain(){++refs;}void release(){assert(refs>1);--refs;}
};
using IOService=OSObject;using IOMemoryDescriptor=OSObject;
#define OSDynamicCast(T,p) static_cast<T*>(p)
#define OSSafeReleaseNULL(p) do{if(p)(p)->release();p=nullptr;}while(0)
constexpr unsigned kIODirectionInOut=1,kIOMemoryThreadSafe=2,kIOMemoryMapperNone=0;
struct IOSubMemoryDescriptor {static IOMemoryDescriptor *withSubRange(IOMemoryDescriptor*,unsigned,unsigned,unsigned){assert(false);return nullptr;}};
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
int main(){
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
 puts("PASS production DSI aperture: zero unpublished range before/after private surface stays unavailable; invalid nonzero/backing/target/stop still faults");
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='dsi-aperture-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
