#!/usr/bin/env python3
"""Replay controller start through shared checks, provider properties and boot DDI selection."""
from pathlib import Path
import re,subprocess,tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'WhateverXe/kern_xe.cpp').read_text()
start=source.index('bool XE::wrapDisplay13FramebufferStart(')
end=source.index('const auto started=FunctionCast(',start)
start_wrapper=source[start:end]+'++nativeStarts;return true;\n}'
methods='\n'.join(re.search(r'bool XE::'+name+r'\(.*?\n\}',source,re.S)[0]
                  for name in ('prepareDisplay13BootScanout','display13PortReady'))+start_wrapper
code=r'''
#include <cassert>
#define SYSLOG(...) ((void)0)
#include <cstdint>
#include <cstring>
#include "kern_display13_fb_topology.hpp"
#include "kern_xelp_display_contract.hpp"
using C=XELP::DisplayContract;
Display13OutputWiring board;
#include <map>
#include <memory>
#include <string>
#include <vector>
unsigned step=0,failAt=0;
bool allowed(){return ++step!=failAt;}
struct OSObject {virtual ~OSObject()=default;};
std::vector<std::unique_ptr<OSObject>> objects;
template<class T> T *make(){if(!allowed())return nullptr;auto p=new T;objects.emplace_back(p);return p;}
struct OSNumber:OSObject {
 uint64_t value=0;
 static OSNumber *withNumber(uint64_t v,unsigned bits){assert(bits==32);auto p=make<OSNumber>();if(p)p->value=v;return p;}
};
struct OSData:OSObject {uint32_t value=0;};
struct OSDictionary:OSObject {
 std::map<std::string,OSObject *> items;
 static OSDictionary *withDictionary(OSDictionary *old){auto p=make<OSDictionary>();if(p)p->items=old->items;return p;}
 static OSDictionary *withCapacity(unsigned){return make<OSDictionary>();}
 bool setObject(const char *key,OSObject *v){if(!allowed())return false;items[key]=v;return true;}
};
struct IOService {
 virtual ~IOService()=default;
 std::map<std::string,OSObject *> items;bool workLoop=true;
 void *getWorkLoop(){return workLoop?this:nullptr;}
 OSObject *getProperty(const char *key){auto i=items.find(key);return i==items.end()?nullptr:i->second;}
 bool setProperty(const char *key,OSObject *v){if(!allowed())return false;items[key]=v;return true;}
 bool setProperty(const char *key,bool){return true;}
 bool setProperty(const char *key,void *v,unsigned size){
  if(!strcmp(key,"AAPL,GfxYTile")){
   assert(size==4);auto p=make<OSData>();if(!p)return false;memcpy(&p->value,v,size);return setProperty(key,p);
  }
  if(!strcmp(key,"WEXDisplay13BootPortContract")){
   assert(size==6*sizeof(uint64_t));const auto words=static_cast<const uint64_t *>(v);
   bootMissing=words[5];bootDDI=words[1];
  }
  return true;
 }
 uint64_t bootMissing=UINT64_MAX;unsigned bootDDI=UINT32_MAX;
};
#define OSDynamicCast(T,p) dynamic_cast<T *>(p)
#define OSSafeReleaseNULL(p) do{p=nullptr;}while(0)
struct IORegistryEntry:IOService {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry root;return &root;}};
unsigned nativeStarts=0,maps=0,preparations=0,tcCaptures=0;
bool memoryReady=true;
constexpr unsigned kIOMapInhibitCache=1;
void IODelay(unsigned){assert(false);}
struct IOPCIDevice:IOService {
 alignas(uint64_t) uint32_t trans=0x90000000; // Pipe A scans out on Combo B.
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(&trans);}
 size_t getLength(){return 0x1000000;}
 void release(){}
 uint16_t configRead16(unsigned off){assert(off==4);return 6;}
 IOPCIDevice *mapDeviceMemoryWithRegister(unsigned reg,unsigned flags){assert(reg==0x10 && flags==kIOMapInhibitCache);++maps;return this;}
};
namespace AlderLakeN {
 enum class ReadoutStatus {Complete,Unavailable};
 struct DisplayReadout {ReadoutStatus status;unsigned poweredPipes,activePipes,trans;};
 template<class IO> DisplayReadout readDisplay(IO &io,bool mapped){return {mapped?ReadoutStatus::Complete:ReadoutStatus::Unavailable,1,1,io.read(0)};}
 void value(const DisplayReadout &r,unsigned reg,unsigned &out){assert(reg==0x60400);out=r.trans;}
 enum class GOPProtocol {EDP,DP,TMDS};
 enum class LinearStatus {Failure,Success};
}
namespace Display13TC {
 struct Boot {
  template<class IO> static Boot capture(IO &,bool admitted,unsigned ddi,unsigned pipe,bool){
   assert(pipe==0);if(admitted){assert(ddi>=3);++tcCaptures;}return {};
  }
 };
}
namespace Display13Boot {
 struct Result {AlderLakeN::LinearStatus status;unsigned writes=0,restored=0;};
 template<class IO> Result prepareLinear(IO &,const AlderLakeN::DisplayReadout &,bool mapped,unsigned ddi,unsigned,
   const Display13TC::Boot *tc,AlderLakeN::GOPProtocol protocol){
  assert(mapped && ddi<7 && (ddi<3 || tc));++preparations;
  assert(protocol==(board.panel&(1U<<ddi)?AlderLakeN::GOPProtocol::EDP:
      board.dp&(1U<<ddi)?AlderLakeN::GOPProtocol::DP:AlderLakeN::GOPProtocol::TMDS));
  return {memoryReady?AlderLakeN::LinearStatus::Success:AlderLakeN::LinearStatus::Failure};
 }
}
namespace XELP::DSI {
 template<class IO,class Boot> bool verifyBootPrefix(IO &,const Boot &,unsigned){assert(false);return false;}
}
struct {void *lock=(void *)1,*retireLock=(void *)1;} display13FBSysMem;
bool adlnFixedSysMemEnabled=true;
bool checkKernelArgument(const char *){return false;}
bool xelpDisplay13Device(IOPCIDevice *){return true;}
bool xelpMemoryMatches(IOPCIDevice *){return true;}
struct XE {
 bool display13FramebufferRequested=true,display13BootReady=true,tglActive=true,adlpDisplaySelected=true;
 bool dsiFreePlane=true,dsiFreeScaler=true,dsiDetachPath=true,dsiRemovePath=true,dsiPipeMap=true;
 bool display13DSIModeHooksReady=true,dsiNativeWriterHooksReady=true,dsiBootProbeReady=true,dsiCacheHooksReady=true;
 bool adlnScanoutDisableFB=true,orgAlderLakeNSysMemAllocate=true,orgAlderLakeNSysMemFree=true;
 bool adlnWorkLoopInGate=true,adlnWorkLoopTryClose=true,adlnWorkLoopOpen=true;
 XELP::Profile xeProfile=XELP::identify(0x46A68086,1);
 struct {bool complete(){return true;}} dsiBoot;
 unsigned xelpMemory=0,display13PreparedDDI=UINT32_MAX;
 void *tglObservationController=nullptr;
 Display13OutputWiring adlnOutputWiring;
 Display13OutputWiring display13BoardWiring(IOService *){return board;}
 C::Mask installed=~C::DPAltBoot13;
 C::Mask display13Available() const{return installed;}
 bool display13PortReady(unsigned,bool=false) const;
 bool prepareDisplay13BootScanout(IOPCIDevice *);
 static bool wrapDisplay13FramebufferStart(IOService *,IOService *);
} handoff,*callbackXE=&handoff;
PRODUCTION
void check(bool oldFeatures,bool oldKey,uint32_t keyValue,unsigned failure,bool workLoop=true){
 objects.clear();step=failAt=0;IOService that;IOPCIDevice provider;provider.workLoop=workLoop;
 if(board.hdmi==8)provider.trans=0xA0000000; // Pipe A on TC1.
 handoff.adlnOutputWiring=board;
 OSObject untouched;OSData key;key.value=keyValue;OSDictionary prior;OSNumber disabled;disabled.value=0;
 prior.items={{"UnrelatedFeature",&untouched},{"YTiling",&disabled}};
 if(oldFeatures)that.items["FeatureControl"]=&prior;
 if(oldKey)provider.items["AAPL,GfxYTile"]=&key;
 step=0;failAt=failure;maps=0;const bool ok=XE::wrapDisplay13FramebufferStart(&that,&provider);
 if(failure || !workLoop){assert(!ok && !maps);return;}
 assert(maps==unsigned(handoff.adlpDisplaySelected || (board.hdmi&0x78)));
 assert(ok);auto f=dynamic_cast<OSDictionary *>(that.getProperty("FeatureControl"));assert(f);
 assert(dynamic_cast<OSNumber *>(f->items.at("YTiling"))->value==1);
 assert(dynamic_cast<OSNumber *>(f->items.at("ThreadingMode"))->value==0);
 if(oldFeatures){assert(f!=&prior && f->items.at("UnrelatedFeature")==&untouched);assert(prior.items.at("YTiling")==&disabled);}
 auto data=dynamic_cast<OSData *>(provider.getProperty("AAPL,GfxYTile"));assert(data);
 assert(oldKey ? data==&key && data->value==keyValue : data->value==1);
}
int main(){
 board.valid=true;
 for(bool mobile:{false,true})for(unsigned mask:{2U,8U}){
  board={};board.valid=true;board.hdmi=mask;board.ddc[mask==2?1:3]=mask==2?2:9;
  for(bool features:{false,true})for(bool key:{false,true})for(uint32_t value:{0U,1U}){
   handoff.adlpDisplaySelected=mobile;
   check(features,key,value,0);unsigned operations=step;
   for(unsigned failure=1;failure<=operations;++failure)check(features,key,value,failure);
   check(features,key,value,0,false);
  }
 }
 // Same board with eDP and two external routes. Drive the actual transcoder
 // selector to the internal panel, fixed DP or Alt DP before boot preparation.
 board={};board.valid=true;board.panel=1;board.dp=0x18;board.typeC=0x10;
 handoff.adlnOutputWiring=board;handoff.adlpDisplaySelected=true;
 IOService that;IOPCIDevice pci;const auto root=IORegistryEntry::getRegistryRoot();
 const auto run=[&](unsigned ddi,bool ok,unsigned captures,unsigned prepares){
  pci.trans=0x82000000|((ddi+1)<<27);
  const auto before=nativeStarts;tcCaptures=preparations=maps=0;
  assert(XE::wrapDisplay13FramebufferStart(&that,&pci)==ok && nativeStarts==before+ok);
  assert(tcCaptures==captures && preparations==prepares);
  if(ok)assert(handoff.display13PreparedDDI==ddi);
 };
 handoff.installed &= ~C::TCNative;
 run(0,true,0,1);assert(root->bootMissing==0 && root->bootDDI==0 && maps==1);
 run(3,false,0,0);assert(root->bootMissing==C::TCNative && root->bootDDI==3 && maps==1);
 handoff.installed |= C::TCNative;
 run(3,true,1,1);assert(root->bootMissing==0 && root->bootDDI==3);
 run(4,false,0,0);assert(root->bootMissing==C::DPAltBoot13 && root->bootDDI==4);
 for(auto missing:{C::IRQ13,C::PowerRefs13,C::ClockSet13}){
  handoff.installed &= ~missing;
  run(0,false,0,0);assert(!maps);
  handoff.installed |= missing;
 }
 display13FBSysMem.retireLock=nullptr;
 run(0,false,0,0);assert(!maps);
 display13FBSysMem.retireLock=(void *)1;
 board.typeC=0; // Same DDI mask, different transport contract since bootstrap.
 run(0,false,0,0);assert(!maps);
 board.typeC=0x10;
 memoryReady=false;
 run(0,false,0,1); // A qualified route cannot bypass a failed shared boot memory proof.
}
'''.replace('PRODUCTION',methods)
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: shared startup/retirement, provider properties, Combo/fixed TC boot selection and no-call refusals')
