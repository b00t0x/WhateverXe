#!/usr/bin/env python3
"""Exercise the production presentation transaction with native allocation leaves mocked."""
from pathlib import Path
from production_source import read_source
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=read_source(root/'WhateverXe/kern_xe.cpp')
functions='\n'.join(re.search(r'(?:bool|uint32_t) XE::'+name+r'\(.*?\n\}',source,re.S)[0] for name in ['releaseDSILogical','setDSIPresentation'])
code=r'''
#include "kern_dsi_native_mode.hpp"
#include "kern_dsi_native_backing.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using Display13FBMemory::field;
using XELP::DSI::AppleAdapter::Timing;
using XELP::DSI::NativeBacking::Record;
struct IOService {alignas(8) uint8_t bytes[0x4B90]{};IOService *getMetaClass(){return this;}unsigned getClassSize(){return sizeof(bytes);}};
template<class T,unsigned N>constexpr unsigned arrsize(T(&)[N]){return N;}
using OSObject=IOService;
template<class T>void put(void *p,unsigned offset,T value){memcpy(static_cast<uint8_t *>(p)+offset,&value,sizeof(value));}
struct IOLock {unsigned depth=0;};
void IOLockLock(IOLock *l){++l->depth;}void IOLockUnlock(IOLock *l){assert(l->depth);--l->depth;}
void *current_thread(){return reinterpret_cast<void *>(1);}
constexpr uint32_t kIOReturnNotReady=0xE00002D8,kIOReturnSuccess=0;
uint64_t receiptCopy[16]{};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *,const void *p,unsigned n){assert(n==sizeof(receiptCopy) || n==13*sizeof(uint64_t));if(n==sizeof(receiptCopy))memcpy(receiptCopy,p,n);}};
struct {IOService *framebuffer=nullptr;Timing timing,presentation;XELP::DSI::Mode mode;
 uint32_t clientMode=0x80001000,activeMode=0;uintptr_t logical=0,changingThread=0;
 bool presentationReady=false,presentationFault=false;}dsiFixedMode;
struct {IOService *pipe=nullptr;}dsiPendingBacking;
struct {IOLock *lock=nullptr;unsigned dsiNativeWriters=0;struct {unsigned wmPending=0;bool wmCommitting=false;}pipe[1];
 struct {bool updating=false;}dbuf;}display13FBPower;
bool dsiModeTarget(IOService *f){return f==dsiFixedMode.framebuffer;}
bool dsiModeCoherent(IOService *){return true;}
bool dsiPublishedTarget(IOService *f){return field<uintptr_t>(f,0x4348)==0x100000;}
struct XE {
 XELP::DSI::Profile dsiProfile;uintptr_t dsiModeCPU[4]{},dsiComputeGeometry=0;
 bool dsiArmedBackingOwned(IOService *f,OSObject *){return dsiPublishedTarget(f);}
 bool dsiNativeUpdateAllowed(IOService *){return !dsiFixedMode.changingThread && !dsiFixedMode.presentationFault;}
 bool releaseDSILogical(IOService *);
 uint32_t setDSIPresentation(IOService *,uint32_t,const Timing &);
}xe;
FUNCTIONS
unsigned allocations=0,frees=0,caps=0;bool failAllocation=false,failRelease=false;
void compute(void *f,const Timing *t){
 auto path=Display13FBMemory::singlePath<void *>(f);
 const unsigned width=t->horizontalScaled?t->horizontalScaled:t->horizontalActive;
 const unsigned height=t->verticalScaled?t->verticalScaled:t->verticalActive;
 const unsigned pitch=(width*4+127)&~127U;
 for(unsigned off:{0x430CU,0x4320U,0x4328U})put(f,off,t->horizontalActive);
 for(unsigned off:{0x4310U,0x4324U,0x432CU})put(f,off,t->verticalActive);
 put(f,0x430C,width);put(f,0x4310,height);
 put(f,0x4314,pitch*t->verticalActive);put(f,0x4334,pitch);put(f,0x4338,t->scalerFlags?0x30U:((width!=t->horizontalActive || height!=t->verticalActive)?0x20U:1U));
 put(path,0xF0,uint64_t(0));put(path,0x100,uint64_t(0));
 for(unsigned off:{0xF8U,0x108U})put(path,off,t->horizontalActive);
 for(unsigned off:{0xFCU,0x10CU})put(path,off,t->verticalActive);
}
uint32_t allocate(IOService *f){
 assert(dsiFixedMode.changingThread && !field<uintptr_t>(f,0x4340));
 assert(field<uint32_t>(f,0x4250)==dsiFixedMode.activeMode);
 if(!(field<uint32_t>(f,0x4338)&0x30))return 0;
 ++allocations;auto r=new Record;r->kind=2;r->columns=(field<uint32_t>(f,0x430C)*4+127)/128;r->rows=(field<uint32_t>(f,0x4310)+31)/32;
 r->pitch=field<uint32_t>(f,0x4334);r->bytes=r->pitch*field<uint32_t>(f,0x4310);
 r->memory=0x200000;r->map=0x300000;
 put(f,0x4340,r);put(dsiPendingBacking.pipe,0x12B8,r->memory);put(dsiPendingBacking.pipe,0x12C0,r->map);
 return failAllocation?0xE00002BD:0;
}
void release(IOService *f){auto r=field<Record *>(f,0x4340);assert(r);++frees;delete r;
 put(f,0x4340,uintptr_t(0));if(!failRelease){put(dsiPendingBacking.pipe,0x12B8,uintptr_t(0));put(dsiPendingBacking.pipe,0x12C0,uintptr_t(0));}}
void initial(IOService *,uint32_t){}void capabilities(IOService *){++caps;}
int main(){
 IOService fb,path,pipe,other;IOLock lock;display13FBPower.lock=&lock;dsiPendingBacking.pipe=&pipe;
 auto &o=dsiFixedMode;o.framebuffer=&fb;xe.dsiProfile.status=XELP::DSI::ProfileStatus::Valid;
 xe.dsiProfile.mode={135255,1080,46,22,2,1920,82,40,2,86,154};o.mode=xe.dsiProfile.mode;
 assert(XELP::DSI::AppleAdapter::makeTiming(xe.dsiProfile,o.timing));
 put(&fb,0x3FD8,0U);put(&fb,0x4A00,1U);put(&fb,0x4A08,&path);put(&fb,0x4348,uintptr_t(0x100000));
 compute(&fb,&o.timing);xe.dsiComputeGeometry=reinterpret_cast<uintptr_t>(compute);
 xe.dsiModeCPU[0]=reinterpret_cast<uintptr_t>(allocate);xe.dsiModeCPU[1]=reinterpret_cast<uintptr_t>(initial);
 xe.dsiModeCPU[2]=reinterpret_cast<uintptr_t>(capabilities);xe.dsiModeCPU[3]=reinterpret_cast<uintptr_t>(release);
 auto turn=o.timing;turn.scalerFlags=0x30;turn.horizontalScaled=1920;turn.verticalScaled=1080;
 assert(xe.setDSIPresentation(&other,2,turn)==kIOReturnNotReady && !allocations);
 display13FBPower.pipe[0].wmPending=1;assert(xe.setDSIPresentation(&fb,2,turn)==kIOReturnNotReady && !allocations);
 display13FBPower.pipe[0].wmPending=0;
 assert(!xe.setDSIPresentation(&fb,2,turn) && o.logical && o.activeMode==2 && !o.changingThread);
 assert(field<uint32_t>(&fb,0x4338)==0x30 && dsiPublishedTarget(&fb));
 turn.scalerFlags=0x50;assert(!xe.setDSIPresentation(&fb,3,turn) && allocations==2 && frees==1);
 auto scaled=turn;scaled.horizontalScaled=2880;scaled.verticalScaled=1620;
 assert(!xe.setDSIPresentation(&fb,8,scaled) && allocations==3 && frees==2);
 assert(field<uint32_t>(&fb,0x430C)==2880 && field<Record *>(&fb,0x4340)->bytes==18662400);
 assert(!xe.setDSIPresentation(&fb,9,scaled) && allocations==3 && frees==2);
 assert(!xe.setDSIPresentation(&fb,3,turn) && allocations==4 && frees==3);
 assert(!xe.setDSIPresentation(&fb,4,o.timing) && !o.logical && frees==4 && field<uint32_t>(&fb,0x4338)==1);
 failAllocation=true;assert(xe.setDSIPresentation(&fb,5,turn)==kIOReturnNotReady);
 assert(o.activeMode==4 && !o.logical && !o.presentationFault && !o.changingThread && allocations==5 && frees==5);
 assert(field<uint32_t>(&fb,0x4338)==1 && dsiPublishedTarget(&fb) && receiptCopy[10]==1);
 failAllocation=false;
 auto portrait=o.timing;portrait.horizontalScaled=1620;portrait.verticalScaled=2880;
 assert(!xe.setDSIPresentation(&fb,10,portrait) && o.logical);
 assert(field<uint32_t>(&fb,0x4338)==0x20 && field<Record *>(&fb,0x4340)->bytes==18800640);
 assert(!xe.setDSIPresentation(&fb,11,portrait) && allocations==6 && frees==5);
 assert(!xe.setDSIPresentation(&fb,4,o.timing) && !o.logical && allocations==frees);
 assert(!xe.setDSIPresentation(&fb,6,turn));failRelease=true;
 assert(xe.setDSIPresentation(&fb,7,o.timing)==kIOReturnNotReady && o.presentationFault && !o.changingThread);
 assert(allocations==frees && !lock.depth && dsiPublishedTarget(&fb));
 puts("PASS production presentation: quarter-turns, native restore, partial allocation cleanup and failed-release admission closure");
}
'''.replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-presentation-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
