#!/usr/bin/env python3
"""Replay production DSI fixed-mode ownership/dispatch with IOKit leaves mocked."""
from pathlib import Path
from production_source import read_source
import hashlib, re, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[1]
source = read_source(root/'WhateverXe/kern_xe.cpp')
owner = source[source.index('struct DSIFixedModeOwner {'):source.index('// AlderLakeN start bypasses')]
names = ['recordDSIClient', 'releaseDSIFixedMode', 'bindDSIFixedMode', 'wrapDSISetDetailedTimings', 'wrapDSIValidateMode',
         'wrapDSIGetModes', 'wrapDSIGetModeCount', 'wrapDSIGetModeInformation', 'wrapDSIGetCurrentMode', 'wrapDSIGetOnlineInfo', 'wrapDSIDDC']
functions = '\n'.join(re.search(r'(?:void|bool|uint32_t) XE::'+name+r'\(.*?\n\}', source, re.S)[0] for name in names)
if len(sys.argv) == 2:
    binary = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(binary).hexdigest() == 'c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
    installer = re.search(r'bool XE::processDisplay13DSIModes\(.*?\n\}', source, re.S)[0]
    offsets = [int(x, 0) for x in re.search(r'offsets\[\]=\{([^}]+)', installer)[1].split(',')]
    entries = re.search(r'entries\[\]\[16\]=\{(.*?)\};', installer, re.S)[1]
    entries = [bytes(int(x, 0) for x in row.split(',')) for row in re.findall(r'\{([^}]+)\}', entries)]
    assert len(entries) == len(offsets) == 9
    for offset, entry in zip(offsets, entries):
        assert binary[offset:offset+16] == entry, hex(offset)

code = r'''
#include "kern_dsi_apple_timing.hpp"
#include "kern_dsi_readout.hpp"
#include "kern_display13_transcoder.hpp"
#include "kern_display13_output_wiring.hpp"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
constexpr uint32_t kIOReturnSuccess=0,kIOReturnNotReady=0xe00002d8U,
 kIOReturnUnsupported=0xe00002c7U,kIOReturnBadArgument=0xe00002c2U,kIOReturnNoResources=0xe00002bdU;
void *current_thread(){return reinterpret_cast<void *>(1);}
#define arrsize(x) (sizeof(x)/sizeof(*(x)))
unsigned alive=0,calls[5]{};bool dataOK=true,arrayOK=true,publishOK=true,geometryOK=true,clearOK=true,capacityOK=true,appendOK=true;
struct Meta {unsigned getClassSize(){return 0x4b90;}} meta;
struct OSObject {
 unsigned refs=1;OSObject(){++alive;}virtual ~OSObject(){--alive;}
 void retain(){assert(refs);++refs;}void release(){assert(refs);if(!--refs)delete this;}
 Meta *getMetaClass(){return &meta;}
};
struct OSNumber:OSObject {uint64_t value=0;uint64_t unsigned64BitValue(){return value;}};
struct OSData:OSObject {
 std::vector<uint8_t> bytes;
 static OSData *withCapacity(unsigned n){if(!capacityOK)return nullptr;auto d=new OSData;d->bytes.reserve(n);return d;}
 bool appendBytes(const void *p,unsigned n){if(!appendOK)return false;auto b=static_cast<const uint8_t *>(p);bytes.insert(bytes.end(),b,b+n);return true;}
 static OSData *withBytes(const void *p,unsigned n){if(!dataOK)return nullptr;auto d=new OSData;
  d->bytes.assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);return d;}
 unsigned getLength(){return bytes.size();}const void *getBytesNoCopy(){return bytes.data();}
};
struct OSArray:OSObject {
 std::vector<OSObject*> objects;
 static OSArray *withCapacity(unsigned n){assert(n>=1 && n<=2);return arrayOK?new OSArray:nullptr;}
 bool setObject(OSObject *p){p->retain();objects.push_back(p);return true;}
 unsigned getCount(){return objects.size();}OSObject *getObject(unsigned i){return i<objects.size()?objects[i]:nullptr;}
 ~OSArray(){for(auto p:objects)p->release();}
};
struct IOService:OSObject {
 alignas(8) uint8_t storage[0x4c00]{};std::map<std::string,OSObject*> properties;
 bool setProperty(const char *k,OSObject *p){if(!publishOK)return false;p->retain();removeProperty(k);properties[k]=p;return true;}
 void setProperty(const char *,bool){}
 bool setProperty(const char *k,const void *p,unsigned n){auto d=OSData::withBytes(p,n);bool ok=setProperty(k,d);d->release();return ok;}
 OSObject *getProperty(const char *k){auto i=properties.find(k);return i==properties.end()?nullptr:i->second;}
 void removeProperty(const char *k){auto i=properties.find(k);if(i!=properties.end()){i->second->release();properties.erase(i);}}
 ~IOService(){for(auto i:properties)i.second->release();}
};
struct IORegistryEntry {void setProperty(const char *key,OSData *d){assert((!strcmp(key,"WEXDSITimingRejected") && d->getLength()==160) || (!strcmp(key,"WEXDSITimingList") && !(d->getLength()%160)));}
 void removeProperty(const char *key){assert(!strcmp(key,"WEXDSITimingList"));}
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *key,const void *p,unsigned n){if(!strncmp(key,"WEXDSIModeInfo",14)){assert(n==48);return;}if(!strcmp(key,"WEXDSITimingList")){assert(!(n%160));return;}if(!strcmp(key,"WEXDSITimingRejected")){assert(n==160);return;}assert(n==128);if(!strcmp(key,"WEXDSITimingOffer"))return;const auto w=static_cast<const uint64_t *>(p);assert(w[0]==1 && w[1]<13 && w[2]);}};
struct IORecursiveLock {unsigned depth=0;};struct IOLock {unsigned depth=0;};
void IORecursiveLockLock(IORecursiveLock *l){++l->depth;}void IORecursiveLockUnlock(IORecursiveLock *l){assert(l->depth);--l->depth;}
void IOLockLock(IOLock *l){++l->depth;}void IOLockUnlock(IOLock *l){assert(l->depth);--l->depth;}
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
#define OSSafeReleaseNULL(p) do{if(p){p->release();p=nullptr;}}while(0)
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
OSObject *testPath=nullptr;
namespace Display13FBMemory {
 template<class T>T &field(void *p,unsigned o){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+o);}
 template<class T>T singlePath(void *){return static_cast<T>(testPath);}
}
namespace TGLFBBinding {enum class Status {Bad,Complete};}
struct Binding {TGLFBBinding::Status status=TGLFBBinding::Status::Complete;uint32_t ddi=0,pipe=0;} binding;
Binding adlnInspectPathBinding(IOService *,OSObject *path,void *,const Display13OutputWiring &){assert(path);return binding;}
struct {IOLock *lock;void *controller;Display13Transcoder::Bindings transcoders;XELP::DSI::Readout bootDSI;
 bool memoryFaulted=false;struct {struct {bool faulted=false;} state;} bridge;} display13FBPower;
struct XE {
 bool dsiPanelOnly=false;bool display13DSIModeHooksReady=true;Display13OutputWiring adlnOutputWiring;void *adlnTopologyController=nullptr;
 uint8_t adlnPlatform[0x1c0]{};uintptr_t orgDSIMode[9]{};
 static bool updateReady;
 static bool dsiNativeUpdateAllowed(IOService *){return updateReady;}
 static uint32_t wrapDSIDDC(IOService *,uint32_t,uint8_t *,uint64_t *,uint32_t,OSObject *);
 static uint32_t wrapDSIGetCurrentMode(IOService *,int32_t *,int32_t *);
 static uint32_t wrapDSIGetOnlineInfo(IOService *,OSObject *,uint8_t *,uint8_t *);
 static void recordDSIClient(IOService *,unsigned,uint64_t,uint32_t);
 bool releaseDSIFixedMode(IOService *);
 bool bindDSIFixedMode(IOService *,const XELP::DSI::Profile &);
 static uint32_t wrapDSISetDetailedTimings(IOService *,OSArray *);
 static uint32_t wrapDSIValidateMode(IOService *,int32_t,const void **,const void **);
 static uint32_t wrapDSIGetModes(IOService *,int32_t *);
 static uint32_t wrapDSIGetModeCount(IOService *);
 static uint32_t wrapDSIGetModeInformation(IOService *,int32_t,void *);
} xe,*callbackXE=&xe;
bool XE::updateReady=true;
OWNER
FUNCTIONS
uint32_t publish(IOService *fb,OSArray *a){++calls[0];if(a)fb->setProperty("IOFBDetailedTimings",a);else {if(!clearOK)return 0;fb->removeProperty("IOFBDetailedTimings");}
 Display13FBMemory::field<OSArray*>(fb,0x4260)=a;return 0;}
uint32_t validate(IOService *fb,int32_t mode,const void **desc,const void **timing){++calls[1];
 auto a=Display13FBMemory::field<OSArray*>(fb,0x4260);assert(a && (mode&1023)==0);
 if(desc)*desc=reinterpret_cast<void*>(1);if(timing)*timing=static_cast<OSData*>(a->getObject(0))->getBytesNoCopy();return 0;}
uint32_t modes(IOService *,int32_t *p){++calls[2];if(p)*p=42;return 0;}
uint32_t count(IOService *){++calls[3];return 3;}
uint32_t information(IOService *fb,int32_t mode,void *p){++calls[4];const void *t=nullptr;
 auto r=XE::wrapDSIValidateMode(fb,mode,nullptr,&t);if(r)return r;
 auto timing=static_cast<const XELP::DSI::AppleAdapter::Timing*>(t);
 auto info=static_cast<XELP::DSI::AppleAdapter::ModeInformation*>(p);*info={};
 info->width=geometryOK?(timing->horizontalScaled?timing->horizontalScaled:timing->horizontalActive):1;info->height=timing->verticalScaled?timing->verticalScaled:timing->verticalActive;info->depth=2;return 0;}
unsigned nativeCurrentCalls=0,nativeOnlineCalls=0,nativeDDCCalls=0;
uint32_t nativeDDC(IOService *,uint32_t block,uint8_t *data,uint64_t *length,uint32_t flags,OSObject *path){
 assert(block==2 && flags==1 && path==testPath);++nativeDDCCalls;*length=128;data[0]=42;return 9;}

uint32_t nativeCurrent(IOService *,int32_t *m,int32_t *d){++nativeCurrentCalls;if(m)*m=77;if(d)*d=3;return 7;}
uint32_t nativeOnline(IOService *,OSObject *,uint8_t *on,uint8_t *edid){++nativeOnlineCalls;if(on)*on=9;if(edid)edid[0]=42;return 8;}
int main(){
 using namespace XELP::DSI;using namespace AppleAdapter;IORecursiveLock fbLock;IOLock ownerLock;
 auto fb=new IOService;auto peer=new IOService;testPath=peer;
 Display13FBMemory::field<IORecursiveLock*>(fb,0x4220)=&fbLock;
 display13FBPower.lock=&ownerLock;display13FBPower.controller=xe.adlnTopologyController=peer;
 display13FBPower.transcoders.dsi0Pipe=0;xe.adlnOutputWiring.valid=true;xe.adlnOutputWiring.dsi=1;
 xe.orgDSIMode[0]=reinterpret_cast<uintptr_t>(publish);xe.orgDSIMode[1]=reinterpret_cast<uintptr_t>(validate);
 xe.orgDSIMode[2]=reinterpret_cast<uintptr_t>(modes);xe.orgDSIMode[3]=reinterpret_cast<uintptr_t>(count);
 xe.orgDSIMode[4]=reinterpret_cast<uintptr_t>(information);xe.orgDSIMode[8]=reinterpret_cast<uintptr_t>(nativeDDC);
 xe.orgDSIMode[6]=reinterpret_cast<uintptr_t>(nativeCurrent);xe.orgDSIMode[7]=reinterpret_cast<uintptr_t>(nativeOnline);
 Display13FBMemory::field<uint32_t>(peer,0x1DC)=1;
 Profile p;p.status=ProfileStatus::Valid;p.lanes=4;p.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
 auto plan=linkPlan(p);auto &r=display13FBPower.bootDSI;r.status=ReadoutStatus::Complete;r.identity={0,0,0,1};
 r.afeKHz=811500;r.htotal=plan.htotal;r.hsync=plan.hsync;r.vtotal=plan.vtotal;r.vsync=plan.vsync;
 for(unsigned failure=0;failure<4;++failure){const auto before=alive;dataOK=failure!=0;arrayOK=failure!=1;
  binding.status=failure==2?TGLFBBinding::Status::Bad:TGLFBBinding::Status::Complete;
  display13FBPower.memoryFaulted=failure==3;
  assert(!xe.bindDSIFixedMode(fb,p) && !dsiModeTarget(fb) && alive==before && !calls[0]);}
 dataOK=arrayOK=true;display13FBPower.memoryFaulted=false;
 assert(xe.bindDSIFixedMode(fb,p) && fb->refs==2 && dsiFixedMode.array->refs==2 && dsiFixedMode.data->refs==2);
 // Profile-derived enumeration never calls native EDID/AUX.
 uint8_t online=0,edid[128];memset(edid,0xCC,sizeof(edid));uint64_t length=128;
 assert(!XE::wrapDSIDDC(fb,1,edid,&length,0,nullptr) && length==128 && edid[0]==0 && edid[1]==255 && !nativeDDCCalls);
 uint8_t expected[128];assert(makePanelEDID(p.mode,expected) && !memcmp(edid,expected,128));
 length=127;memset(edid,0xCC,128);assert(XE::wrapDSIDDC(fb,1,edid,&length,0,nullptr)==kIOReturnBadArgument && !length && edid[0]==0xCC);
 length=128;assert(XE::wrapDSIDDC(fb,2,edid,&length,0,nullptr)==kIOReturnUnsupported && !length);
 {const auto saved=dsiFixedMode.mode;dsiFixedMode.mode={136887,1200,68,40,12,1920,239,205,2,141,226};
  length=128;assert(!XE::wrapDSIDDC(fb,1,edid,&length,0,nullptr) && edid[126]==1);
  length=128;assert(!XE::wrapDSIDDC(fb,2,edid,&length,0,nullptr) && length==128 && edid[0]==0x70);
  uint8_t block[128];assert(makePanelEDID(dsiFixedMode.mode,block,2) && !memcmp(block,edid,128));
  dsiFixedMode.mode=saved;length=128;assert(!XE::wrapDSIDDC(fb,1,edid,&length,0,nullptr));}
 length=128;assert(XE::wrapDSIDDC(fb,1,edid,&length,1,nullptr)==kIOReturnUnsupported && !length);
 assert(XE::wrapDSIDDC(peer,2,edid,&length,1,testPath)==9 && length==128 && nativeDDCCalls==1);
 xe.adlnOutputWiring.dsi=0;assert(XE::wrapDSIDDC(fb,2,edid,&length,1,testPath)==9 && nativeDDCCalls==2);xe.adlnOutputWiring.dsi=1;
 assert(!XE::wrapDSIGetOnlineInfo(fb,peer,&online,edid) && online==1 && !nativeOnlineCalls);
 assert(!memcmp(edid,expected,128));
 assert(XE::wrapDSIGetOnlineInfo(fb,fb,&online,edid)==kIOReturnNotReady && !online);
 assert(XE::wrapDSIGetOnlineInfo(fb,peer,nullptr,edid)==kIOReturnBadArgument);
 int32_t current=-9,depth=-9;
 Display13FBMemory::field<uint8_t>(fb,0x1E0)=1;
 Display13FBMemory::field<uint32_t>(fb,0x4250)=uint32_t(-5);
 Display13FBMemory::field<uint32_t>(fb,0x4254)=0;
 assert(!XE::wrapDSIGetCurrentMode(fb,&current,&depth) && current==FixedModeID && !depth && !nativeCurrentCalls);
 assert(Display13FBMemory::field<uint32_t>(fb,0x4250)==FixedModeID);
 XE::updateReady=false;current=-9;
 assert(XE::wrapDSIGetCurrentMode(fb,&current,&depth)==kIOReturnNotReady && current==-9);
 assert(XE::wrapDSIGetOnlineInfo(fb,peer,&online,edid)==kIOReturnNotReady && !online);XE::updateReady=true;
 Display13FBMemory::field<uint8_t>(fb,0x1E0)=0;
 assert(XE::wrapDSIGetCurrentMode(fb,&current,&depth)==kIOReturnNotReady);
 Display13FBMemory::field<uint8_t>(fb,0x1E0)=1;
 assert(XE::wrapDSIGetCurrentMode(peer,&current,&depth)==7 && current==77 && depth==3);
 assert(XE::wrapDSIGetOnlineInfo(peer,peer,&online,edid)==8 && online==9 && edid[0]==42);
 xe.dsiPanelOnly=true;auto beforeOnline=nativeOnlineCalls;
 assert(!XE::wrapDSIGetOnlineInfo(peer,peer,&online,edid) && !online && !edid[0] && nativeOnlineCalls==beforeOnline);
 assert(XE::wrapDSIGetOnlineInfo(peer,peer,nullptr,edid)==kIOReturnBadArgument);xe.dsiPanelOnly=false;
 xe.adlnOutputWiring.dsi=0;assert(XE::wrapDSIGetCurrentMode(fb,&current,&depth)==7);
 assert(XE::wrapDSIGetOnlineInfo(fb,peer,&online,edid)==8);xe.adlnOutputWiring.dsi=1;
 assert(nativeCurrentCalls==2 && nativeOnlineCalls==2);
 auto callsBeforeRepeat=calls[0];assert(xe.bindDSIFixedMode(fb,p) && calls[0]==callsBeforeRepeat && fb->refs==2 && dsiFixedMode.array->refs==2);
 auto other=p;++other.mode.clockKHz;assert(!xe.bindDSIFixedMode(fb,other));
 assert(XE::wrapDSIGetModeCount(fb)==1 && XE::wrapDSIGetModeCount(peer)==3);
 int32_t peerMode=0;assert(!XE::wrapDSIGetModes(peer,&peerMode) && peerMode==42);
 assert(XE::wrapDSIGetModes(fb,nullptr)==kIOReturnBadArgument);
 int32_t mode=0;assert(!XE::wrapDSIGetModes(fb,&mode) && mode==FixedModeID);
 const void *timing=nullptr,*description=nullptr;
 assert(!XE::wrapDSIValidateMode(fb,mode,&description,&timing) && timing==dsiFixedMode.data->getBytesNoCopy());
 auto validations=calls[1];assert(XE::wrapDSIValidateMode(fb,mode+0x400,&description,&timing)==kIOReturnBadArgument);
 assert(!description && !timing && calls[1]==validations);
 ModeInformation info;assert(!XE::wrapDSIGetModeInformation(fb,mode,&info));
 assert(info.width==1080 && info.height==1920 && info.widthMM==86 && info.heightMM==154 && info.depth==0);
 assert(!(info.flags&0x00400000));
 auto invalid=p.mode;invalid.width=0;assert(!annotateMode(invalid,info));
 assert(XE::wrapDSIGetModeInformation(fb,mode,nullptr)==kIOReturnBadArgument);
 assert(info.refresh>59*65536 && info.refresh<61*65536 && (info.flags&0x02000407)==0x02000407);
 geometryOK=false;assert(XE::wrapDSIGetModeInformation(fb,mode,&info)==kIOReturnNotReady);geometryOK=true;
 auto replacement=OSArray::withCapacity(1);auto data=OSData::withBytes(&dsiFixedMode.timing,sizeof(Timing));
 replacement->setObject(data);assert(!XE::wrapDSISetDetailedTimings(fb,replacement));
 assert(Display13FBMemory::field<OSArray*>(fb,0x4260)==dsiFixedMode.array && replacement->refs==1);
 data->bytes.resize(1);assert(XE::wrapDSISetDetailedTimings(fb,replacement)==kIOReturnUnsupported);
 assert(XE::wrapDSISetDetailedTimings(fb,nullptr)==kIOReturnUnsupported);
 data->release();replacement->release();
 auto offered=dsiFixedMode.timing;offered.reservedA[0]=0x80001001;offered.signalLevels=1;
 offered.pixelClock=offered.minPixelClock=offered.maxPixelClock=135260000;
 auto list=OSArray::withCapacity(1);auto wrong=offered;wrong.horizontalActive=640;
 auto wrongData=OSData::withBytes(&wrong,sizeof(wrong));auto offeredData=OSData::withBytes(&offered,sizeof(offered));
 list->setObject(wrongData);list->setObject(offeredData);
 assert(!XE::wrapDSISetDetailedTimings(fb,list));assert(dsiFixedMode.clientMode==0x80001001 && list->refs==1);
 assert(dsiModeCoherent(fb) && Display13FBMemory::field<OSArray*>(fb,0x4260)==dsiFixedMode.array);
 assert(!XE::wrapDSIValidateMode(fb,static_cast<int32_t>(0x80001001),nullptr,&timing));
 assert(timing==dsiFixedMode.data->getBytesNoCopy());
 assert(XE::wrapDSIValidateMode(fb,static_cast<int32_t>(0x80001000),nullptr,&timing)==kIOReturnBadArgument);
 assert(!XE::wrapDSIGetModes(fb,&mode) && static_cast<uint32_t>(mode)==0x80001001);
 assert(!XE::wrapDSIGetCurrentMode(fb,&current,&depth) && static_cast<uint32_t>(current)==0x80001001);
 assert(!XE::wrapDSIGetModeInformation(fb,mode,&info) && info.width==1080 && info.height==1920 && !info.depth);
 offeredData->bytes[64]^=1;
 assert(XE::wrapDSISetDetailedTimings(fb,list)==kIOReturnUnsupported && dsiFixedMode.clientMode==0x80001001);
 offered.reservedA[0]=0x80003000;
 auto renewed=OSArray::withCapacity(1);auto renewedData=OSData::withBytes(&offered,sizeof(offered));renewed->setObject(renewedData);
 assert(!XE::wrapDSISetDetailedTimings(fb,renewed) && dsiFixedMode.clientMode==0x80003000);
 assert(dsiModeCoherent(fb) && renewed->refs==1);
 assert(XE::wrapDSIValidateMode(fb,static_cast<int32_t>(0x80001001),nullptr,&timing)==kIOReturnBadArgument);
 assert(!XE::wrapDSIValidateMode(fb,static_cast<int32_t>(0x80003000),nullptr,&timing) && timing==dsiFixedMode.data->getBytesNoCopy());
 assert(!XE::wrapDSIGetModes(fb,&mode) && static_cast<uint32_t>(mode)==0x80003000);
 assert(!XE::wrapDSIGetCurrentMode(fb,&current,&depth) && static_cast<uint32_t>(current)==0x80003000);
 // Rotation offers must survive validation without changing the retained wire mode.
 auto rotated=offered;rotated.reservedA[0]=0x80005000;rotated.scalerFlags=0x30;rotated.horizontalScaled=1920;rotated.verticalScaled=1080;
 auto rotationList=OSArray::withCapacity(2);auto rotationData=OSData::withBytes(&rotated,sizeof(rotated));
 rotationList->setObject(renewedData);rotationList->setObject(rotationData);
 assert(!XE::wrapDSISetDetailedTimings(fb,rotationList));
 assert(XE::wrapDSIGetModeCount(fb)==2);int32_t pair[2];assert(!XE::wrapDSIGetModes(fb,pair));
 assert(static_cast<uint32_t>(pair[1])==rotated.reservedA[0]);
 assert(!XE::wrapDSIValidateMode(fb,pair[1],nullptr,&timing));
 assert(static_cast<const Timing *>(timing)->scalerFlags==0x30 && !dsiFixedMode.timing.scalerFlags);
 assert(!XE::wrapDSIGetModeInformation(fb,pair[1],&info) && info.width==1920 && info.height==1080);
 // A large OS list with many distinct scaled dimensions must not be
 // rejected by the former 32-mode capacity or 1024-index gates.
 auto many=OSArray::withCapacity(1);
 for(unsigned i=0;i<1100;++i){auto t=rotated;t.reservedA[0]=0x80006000+i;
  t.horizontalScaled=1280+2*i;t.verticalScaled=720+i;
  t.pixelClock=t.minPixelClock=t.maxPixelClock=(i&1)?135255000:135260000;
  auto data=OSData::withBytes(&t,sizeof(t));many->setObject(data);data->release();}
 auto previous=dsiFixedMode.offeredData;const auto beforePublish=calls[0];
 capacityOK=false;
 assert(XE::wrapDSISetDetailedTimings(fb,many)==kIOReturnNoResources);
 capacityOK=true;appendOK=false;
 assert(XE::wrapDSISetDetailedTimings(fb,many)==kIOReturnNoResources);
 appendOK=true;
 assert(dsiFixedMode.offeredData==previous && XE::wrapDSIGetModeCount(fb)==2 && calls[0]==beforePublish);
 assert(!XE::wrapDSISetDetailedTimings(fb,many) && XE::wrapDSIGetModeCount(fb)==1100);
 int32_t modes[1100];assert(!XE::wrapDSIGetModes(fb,modes));
 for(unsigned i=0;i<1100;++i){assert(static_cast<uint32_t>(modes[i])==0x80006000+i);
  assert(!XE::wrapDSIValidateMode(fb,modes[i],nullptr,&timing));
  assert(static_cast<const Timing *>(timing)->horizontalScaled==1280+2*i);
  assert(!XE::wrapDSIGetModeInformation(fb,modes[i],&info) && info.width==1280+2*i && info.height==720+i);}
 many->release();assert(!XE::wrapDSISetDetailedTimings(fb,rotationList));
 rotationData->release();rotationList->release();
 // Legacy UI attributes alone must never fabricate a presentation timing.
 auto orientation=new OSNumber;fb->setProperty("IOFBTransform",orientation);
 for(auto transform:{3ULL,5ULL,0ULL}) {
  orientation->value=transform;
  assert(!XE::wrapDSIValidateMode(fb,static_cast<int32_t>(offered.reservedA[0]),nullptr,&timing));
  assert(static_cast<const Timing *>(timing)->scalerFlags==0);
  assert(!static_cast<const Timing *>(timing)->horizontalScaled);
  assert(!static_cast<const Timing *>(timing)->verticalScaled);
  assert(!dsiFixedMode.timing.scalerFlags && static_cast<const Timing *>(timing)->horizontalActive==1080);
 }
 orientation->value=7;
 assert(!XE::wrapDSIValidateMode(fb,static_cast<int32_t>(offered.reservedA[0]),nullptr,&timing));
 assert(!static_cast<const Timing *>(timing)->scalerFlags);
 fb->removeProperty("IOFBTransform");orientation->release();
 renewedData->bytes[64]^=1;
 assert(XE::wrapDSISetDetailedTimings(fb,renewed)==kIOReturnUnsupported && dsiFixedMode.clientMode==0x80003000);
 renewedData->release();renewed->release();
 wrongData->release();offeredData->release();list->release();
 fb->removeProperty("IOFBDetailedTimings");assert(!XE::wrapDSIGetModeCount(fb) && dsiFixedMode.array->refs==1);
 assert(XE::wrapDSIValidateMode(fb,mode,nullptr,nullptr)==kIOReturnNotReady);
 const auto cleanup=[&]{
  assert(!xe.releaseDSIFixedMode(peer));dsiFixedMode.ready=true;
  assert(!xe.releaseDSIFixedMode(fb));dsiFixedMode.ready=false;
  const auto refs=dsiFixedMode.array->refs;clearOK=false;
  assert(!xe.releaseDSIFixedMode(fb) && dsiModeTarget(fb) && dsiFixedMode.array->refs==refs && fb->refs==2);
  clearOK=true;
  assert(xe.releaseDSIFixedMode(fb) && !dsiModeTarget(fb) && fb->refs==1);
  assert(!Display13FBMemory::field<OSArray*>(fb,0x4260) && !fb->getProperty("IOFBDetailedTimings"));
  assert(xe.releaseDSIFixedMode(fb));};
 cleanup();publishOK=false;assert(!xe.bindDSIFixedMode(fb,p));
 assert(dsiModeTarget(fb) && dsiFixedMode.array->refs==1 && Display13FBMemory::field<OSArray*>(fb,0x4260)==dsiFixedMode.array);
 assert(!XE::wrapDSIGetModeCount(fb));cleanup();
 assert(!fbLock.depth && !ownerLock.depth);fb->release();peer->release();assert(!alive);
 puts("PASS production DSI fixed mode: retained native array, publication failure, dynamic enumeration, metadata and foreign FB dispatch");
}
'''.replace('OWNER', owner).replace('FUNCTIONS', functions)
with tempfile.TemporaryDirectory(prefix='dsi-apple-adapter-') as directory:
    path = Path(directory)
    (path/'test.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-fsanitize=address,undefined',
                    '-I'+str(root/'WhateverXe'), str(path/'test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
