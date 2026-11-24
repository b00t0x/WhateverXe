#!/usr/bin/env python3
"""Real setup-workloop -> SST selection -> boot AUX adoption -> port teardown.

IRQ provider registration and the already-qualified framebuffer memory are OS
boundaries here. Actual port/path associations, source readout, power bridge,
AUX lease transfer and stop/enable wrappers are production code.
"""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
def fn(sig):return re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group()
base=(r/'tests/display13_sst_fixture.cpp').read_text().split('int main(')[0]
base=base.replace('// NATIVE_OBJECTS',(r/'tests/tgl_fb_binding.cpp').read_text().split('int main()')[0].replace('port[0x580]','port[0x700]'))
base=base.replace('struct IOService:OSObject {};', '''
struct IOService:OSObject {void *getProvider(){return this;}};
struct IOPCIDevice:IOService {};
struct IOWorkLoop:OSObject {};
struct IOFilterInterruptEventSource:OSObject {
 void *provider;IOWorkLoop *loop;bool enabled=true;
 void *getProvider(){return provider;}int getIntIndex(){return 0;}
 IOWorkLoop *getWorkLoop(){return loop;}bool isEnabled(){return enabled;}
};
#define OSDynamicCast(T,p) reinterpret_cast<T*>(p)
constexpr unsigned kIOReturnSuccess=0;
struct IORegistryEntry {static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}
 template<class... T>void setProperty(T...){} };
''')
base=r'''
#include <cassert>
#include "kern_display13_handoff.hpp"
#include "kern_display13_panel.hpp"
#include "kern_dsi_panel.hpp"
#include "kern_dsi_restart.hpp"
#include "kern_display13_transcoder.hpp"
#include "kern_xelp_combo_clock.hpp"

// Model the display and Combo clock locks independently.
struct IORecursiveLock { unsigned depth {}; };
void IORecursiveLockLock(IORecursiveLock *lock) { ++lock->depth; }
void IORecursiveLockUnlock(IORecursiveLock *lock) { assert(lock->depth); --lock->depth; }
void *current_thread() { static int thread; return &thread; }
void IODelay(unsigned us) { assert(us==8); }
'''+base
base=base.replace('return regs.at(reg);','return regs[reg];')
base=base.replace('struct OSObject {','std::map<const void *,unsigned> objectSizes;\nstruct OSObject {').replace(
 'unsigned getClassSize() const{return size;}', 'unsigned getClassSize() const{auto it=objectSizes.find(this);return it==objectSizes.end()?size:it->second;}')
base=base.replace('struct Context {','''struct Context {
 IOWorkLoop *workLoop=nullptr;AlderLakeN::DisplayReadout displayScratch;Display13FBMemory::Readout firmwarePlane;
 bool bootPanel=true;struct {unsigned refKHz=24000;}clock;
 bool bootPanelDSCCheckPending=false,bootPanelCMTGSecondary=false;
 Display13Transcoder::Bindings transcoders;
 XELP::DSI::Readout bootDSI;
 struct {bool destroyingPath=false;IOService *memoryFramebuffer=nullptr;Display13FBMemory::Readout memory;} pipe[3];
''')
base=base.replace('struct XE {','''struct XE {
 bool display13FBIRQReady=true,display13FBBootHandoffHookReady=true;
 uintptr_t orgAlderLakeNSetupWorkLoop=0,orgBootModeCompare=0,orgBootResourceInit=0,bootModeCaller=0;
 uintptr_t orgTGLPathLookup=0,orgDisplay13PortTraining=0;
 bool display13FramingReady=true;
 static bool wrapBootModeCompare(void*,void*,void*,void*,const void*,void*);
 static bool selectBootMode(void*,void*,void*,void*,const void*,void*);
 static void *wrapBootResourceInit(void*,uint32_t,void*);
 static bool display13AUXReady(void*,bool){return true;}
 static uint32_t wrapDisplay13ReadAUX(void*,uint32_t,void*,uint32_t);
 static uint32_t wrapDisplay13WriteAUX(void*,uint32_t,void*,uint32_t);
 static bool display13BootPanelDSCOff(void*,unsigned);
 static uint32_t wrapDisplay13PortTraining(void*,void*);
 uintptr_t display13DPImage=0,framebufferClockImage=0,tglPortByDDI=0;
 uintptr_t orgDisplay13TCEnableClocks=0,orgDisplay13TCDisableClocks=0;
 bool adlsDisplaySelected=false,dsiStopAttempted=false,dsiIRQParked=false,dsiResourcesReady=false;
 bool tglActive=false;void *tglObservationController=nullptr,*tglPlatformCopy=nullptr;
 XELP::DSI::Quiesce::Result dsiStop;
 XELP::DSI::Panel::Shutdown dsiPanelStop;
 struct {bool sleeping=false,failed=false;XELP::DSI::Restart::State host;} dsiRuntimePower;
 static bool adlsClockTarget(void*,void*,void*){assert(false);return false;}
 static bool comboClockTarget(void*,void*,void*);
 static bool comboClockPower(void*,bool);
 static uint32_t comboClockRead(void*,unsigned long);
 static void comboClockWrite(void*,unsigned long,uint32_t);
 static bool comboBeginClock(void*,void*,bool,bool&,void *other=nullptr);
 static void comboEndClock();
 static void comboClockTranslateWrite(void*,unsigned long&,uint32_t&,uintptr_t);
 static bool display13DeferClock(void*,uint64_t,uint32_t,uintptr_t);
 static void wrapDisplay13TCEnableClocks(void*,void*,void*,uint32_t*);
 static void wrapDisplay13TCDisableClocks(void*,void*,void*);

 static void wrapAlderLakeNSetupWorkLoop(IOService*,IOWorkLoop*);
 static unsigned wrapDisplay13FBEnableIRQ(IOService*){assert(!locked);return 0;}
 static void wrapDisplay13FBDisableIRQ(IOService*){assert(!locked);}
 bool adoptDSIBootPath(IOService*,OSObject*){assert(false);return false;}
''')
base=base.replace('// BINDING',fn('static TGLFBBinding::Readout adlnInspectPathBinding('))
methods='\n'.join(fn(x) for x in ['uint8_t *XE::display13SSTPort(', 'bool XE::display13HDMIPort(', 'bool XE::display13MainLinkAUX(', 'uint32_t XE::wrapDisplay13EnablePort(', 'uint32_t XE::wrapDisplay13DisablePort(', 'uint32_t XE::wrapDisplay13SignalLevels(', 'uint32_t XE::wrapDisplay13TrainingPattern(', 'void XE::wrapAlderLakeNSetupWorkLoop(', 'bool XE::wrapBootModeCompare(', 'bool XE::selectBootMode(', 'void *XE::wrapBootResourceInit(', 'bool XE::display13BootPanelDSCOff(', 'uint32_t XE::wrapDisplay13PortTraining('])
boundaries='''
struct Display13FBBootIO:Display13FBPowerIO {
 bool readable(uint32_t r){return r<0x1000000;}
};
void *accelOwner=nullptr;
bool adlnSharedBinding(void *ctl,void *accel){return ctl==display13FBPower.controller && accel==accelOwner;}
bool adlnIRQOwner(IOService*p){return p==display13FBPower.controller;}
Display13FBMemory::Readout memoryResult;
Display13FBMemory::Readout display13FBInspectMemory(IOService*){return memoryResult;}
void adlnFault(bool&f,unsigned){f=true;}
void display13FBPowerRecord(uint64_t*,unsigned,unsigned,const Display13Power::Result&){}
void adlnPublishPowerRecord(const char*,uint64_t*){}
'''
base=base.replace('static bool display13StopTranscoder(void*){assert(false);return false;}',
                  'static bool display13StopTranscoder(void*);')
clock_state=re.search(r'struct ComboClockTransaction .*?\} comboClockTransaction;',s,re.S).group()
deferred_clocks=re.search(r'struct Display13DeferredClock .*?static Display13DeferredClock display13DeferredClocks\[3\];',s,re.S).group()
clock_methods='\n'.join(fn(x) for x in [
 'bool XE::comboClockTarget(', 'bool XE::comboBeginClock(', 'void XE::comboEndClock(',
 'void XE::comboClockTranslateWrite(', 'bool XE::display13DeferClock(', 'bool XE::display13StopTranscoder('])
# Only the Combo branch is executed here. TC's independent backend has its own fixtures.
combo_frontend='\n'.join(fn('void XE::'+name).split(marker,1)[0]+'\n    assert(false);\n}' for name,marker in [
 ('wrapDisplay13TCEnableClocks(', '\n    if(!c.lock)return;'),
 ('wrapDisplay13TCDisableClocks(', '\n    // Native AGDC End')])
base=base.replace('// FUNCTIONS',boundaries+clock_state+fn('static bool comboClockAccess(')+
                  deferred_clocks+clock_methods+combo_frontend+methods)
main=r'''
void nativeSetup(IOService *controller,IOWorkLoop*loop){assert(!locked);put(controller,0xe00,loop);}
// 11 is the original unread-DSC case. 12..20 preserve its same-mode
// proposal while independently losing alignment, lock, config/readout,
// source proof, capability/revision, ownership, or the port before comparison.
unsigned fault=0,comparisons=0,sinkReads=0;
bool unreadDSC(){return fault>=11;}
enum Sink { Supported, Unsupported, Legacy, AlreadyOff };
Sink stoppedSink=Supported;
unsigned identityFailure=0,optionalReads=0;
unsigned dscReads=0,dscWrites=0,trains=0,nativeDSCWrites=0;
uint8_t dscEnable=0;
bool restarting=false,dscReadFails=false,dscWriteFails=false,downstream=false;
bool nativeDSCWriteFails=false;
enum Stage { PanelOn, Clocks, Training, Plane, Scaler, Pipe, Brightness };
std::vector<Stage> lightStages;
bool replayCleanup=false;
void *reconstructionPort=nullptr;
#include "display13_reconstruction_fixture.hpp"
uint8_t *donor=nullptr,*accelerator=nullptr;
uint32_t XE::wrapDisplay13ReadAUX(void*,uint32_t reg,void *out,uint32_t n) {
 assert(!locked);auto bytes=static_cast<uint8_t*>(out);memset(out,0,n);
 AUXCommand command(replayCleanup);
 switch(reg){
 case 0x100:assert(n==3);++sinkReads;bytes[0]=10;bytes[1]=0x84;
  if(fault==1 || fault==14 || (fault==7 && sinkReads>1))bytes[1]=0x82;
  if(fault==15)return kIOReturnNotReady;
  if(fault==8)bytes[0]=0;
  break;
 case 0x202:assert(n==3);bytes[0]=bytes[1]=0x77;
  bytes[2]=(fault==2 || fault==12)?0:1;
  if(fault==13)bytes[1]=0x76;
  if(fault==16)return kIOReturnNotReady;
  break;
 case 0:
  if((restarting && identityFailure==1) || (!restarting && fault==19))return kIOReturnNotReady;
  bytes[0]=restarting && identityFailure==2?0xff:stoppedSink==Legacy?0x12:0x14;break;
 case 0x60:
  if((restarting && identityFailure==3) || (!restarting && fault==18))return kIOReturnNotReady;
  assert(!restarting || stoppedSink!=Legacy);
  bytes[0]=stoppedSink!=Unsupported;break;
 case 0x160:
  assert(stoppedSink!=Unsupported && stoppedSink!=Legacy);
  if(restarting) {
   ++optionalReads;++dscReads;
   if(dscReadFails)return kIOReturnNotReady;
   bytes[0]=dscEnable;break;
  }
  if(unreadDSC())return kIOReturnNotReady;
  bytes[0]=fault==3;break;
 case 0x005:assert(restarting && n==1);bytes[0]=downstream;break;
 case 0x10a:bytes[0]=fault==10;break;
 case 0x115:bytes[0]=1;break;
 case 0x12:assert(n==2);bytes[0]=0xbc;bytes[1]=0x34;break; // 13500 * 20 kHz
 default:assert(false);
 }
 return 0;
}
uint32_t XE::wrapDisplay13WriteAUX(void*,uint32_t reg,void *data,uint32_t n) {
 assert(!locked && restarting && reg==0x160 && n==1);
 AUXCommand command(replayCleanup);
 assert(stoppedSink!=Unsupported && stoppedSink!=Legacy);
 ++dscWrites;
 if(dscWriteFails)return kIOReturnNotReady;
 dscEnable=*static_cast<uint8_t*>(data);
 return 0;
}
uint32_t trainAfterStop(void *port,void*) {
 assert(!locked && !(dscEnable&1));
 lightStages.push_back(Training);
 ++trains;
 return XE::wrapDisplay13EnablePort(port);
}
uint32_t lightPower(void *,unsigned power) {
 assert(!locked && power==2);lightStages.push_back(PanelOn);
 if(replayCleanup) {
  auto &c=display13FBPower;
  assert(XE::comboClockPower(c.power,true));
  assert(XE::comboClockPower(c.power,false));
 }
 return 0;
}
void lightPlane(void *,bool) {assert(!locked);lightStages.push_back(Plane);}
void lightScaler(void *,void *) {assert(!locked);lightStages.push_back(Scaler);}
void lightPipe(void *,void *,void *,const void *) {assert(!locked);lightStages.push_back(Pipe);}
void lightBacklight(void *,uint32_t) {assert(!locked);lightStages.push_back(Brightness);}
void lightDelay(unsigned us) {assert(!locked && us==10000);}
bool nativeCompare(void *ctl,void *fb,void *path,void *params,const void*t,void*scaler) {
 assert(!locked);++comparisons;
 if(fault==6)display13FBPower.map->regs[0x46010]=0;
 if(donor){
  put(donor,0x138c50+0x120,uintptr_t(fault==4));
  return reinterpret_cast<decltype(&nativeCompare)>(donor+0x4f044)(ctl,fb,path,params,t,scaler);
 }
 return fault==4 || memcmp(params,static_cast<uint8_t*>(path)+0x128,0x48);
}
void *nativeResource(void *pipe,uint32_t,void*resource) {
 assert(!locked);
 if(accelerator){
  uint8_t pixels[0xac]{};put(pixels,0,7680U);
  memcpy(pixels+0x58,accelerator+0x9bb53,64);
  reinterpret_cast<void(*)(void*,void*,const void*,bool)>(accelerator+0x7ee9a)(pipe,resource,pixels,false);
 }
 return Display13FBMemory::field<void*>(pipe,0x388);
}
void resources(Fixture &f,Map &m) {
 auto &c=display13FBPower;
 assert(c.firmwarePlane.status==Display13FBMemory::Status::Complete);
 alignas(8) uint8_t pipe[0x1300]{},accel[0x1500]{},resource[0x200]{};
 accelOwner=accel;put(pipe,0x88,accel+0);put(pipe,0x388,resource+0);put(pipe,0x98,f.fb+0);put(pipe,0x128c,7680U);
 put(pipe,0x12d0,uint64_t(memoryResult.surface));
 put(f.ctl,0xDF0,1U);put(f.ctl,0xDF4,uint8_t(1));put(f.ctl,0xDF5,uint8_t(3));
 put(f.fb,0x4314,7680U*1080);put(f.fb,0x4330,memoryResult.surface);
 m.regs[0x7019c]=m.regs[0x701ac]=memoryResult.surface;
 m.regs[0x70180]=memoryResult.control;m.regs[0x70188]=120;m.regs[0x70190]=memoryResult.size;
 put(resource,0xb0,uint16_t(1920));put(resource,0xb2,uint16_t(1080));put(resource,0xb8,uint64_t(7680));
 put(resource,0xd8,uint8_t(4));put(resource,0x190,0xc0U);
 state.orgBootResourceInit=reinterpret_cast<uintptr_t>(nativeResource);
 const unsigned tile=fault%2?2:3;
 put(accel,0x1470,tile);put(resource,0x108,0x88000000U);put(resource,0x194,tile);
 // A foreign FB can make its resource first without consuming pipe A.
 put(pipe,0x284,1U);assert(XE::wrapBootResourceInit(pipe,0,resource)==resource);
 assert(Display13FBMemory::field<unsigned>(resource,0x194)==tile);
 put(pipe,0x284,0U);
 if(fault==4)m.regs[0x70180]|=0x1000;
 if(fault==5)put(resource,0xb8,uint64_t(4096));
 if(fault==6)put(f.plane,0x100,memoryResult.control|0x1000U); // queued, not yet live
 assert(XE::wrapBootResourceInit(pipe,0,resource)==resource);
 const bool linear=fault!=4 && fault!=5 && fault!=6;
 assert(Display13FBMemory::field<unsigned>(resource,0x194)==(linear?0:tile));
 if(linear){assert(Display13FBMemory::field<unsigned>(resource,0x188)==15);
  assert(Display13FBMemory::field<unsigned>(resource,0x108)==0x80000000U && (resource[0x1ca]&8));}
 assert(c.firmwarePlane.status==Display13FBMemory::Status::Unavailable);
 // Reuse even the same object and address while MMIO still shows the old
 // linear surface. The second producer owns the native tiling.
 if(accelerator){
  reinterpret_cast<void(*)(void*,uint32_t,void*)>(accelerator+0x7f436)(pipe,0,resource);
  assert(!Display13FBMemory::field<void*>(pipe,0x388) && !Display13FBMemory::field<uint64_t>(pipe,0x12d0));
  put(pipe,0x388,resource+0);put(pipe,0x12d0,uint64_t(memoryResult.surface));
 }
 put(accel,0x1470,3U);put(resource,0x194,3U);assert(XE::wrapBootResourceInit(pipe,0,resource)==resource);
 assert(Display13FBMemory::field<unsigned>(resource,0x194)==3);
}
void firstMode(Fixture &f,Map &m) {
 alignas(8) uint8_t timing[0xa0]{};uint32_t params[0xf0/4]{},original[0xf0/4]{};
 state.orgBootModeCompare=reinterpret_cast<uintptr_t>(nativeCompare);
 params[1]=m.regs[0x60400];params[10]=m.regs[0x6001c];
 for(unsigned i=0;i<6;++i)params[4+i]=m.regs[0x60000+4*i];
 params[14]=m.regs[0x60040];params[15]=m.regs[0x60044];
 params[16]=m.regs[0x60030];params[17]=m.regs[0x60034];
 memcpy(f.path+0x128,params,sizeof(params));
 // Cases 11..20 retain the same native mode proposal. Link proof alone
 // changes; unknown DSC must remain a reconstruction responsibility.
 if(!unreadDSC() || fault==21){params[1]=(params[1]&~0x10eU)|2;params[14]/=2;}
 if(fault==5)params[4]++;
 put(timing,0x28,uint64_t(135000000));put(timing,0x88,uint16_t(1));put(timing,0x8a,uint16_t(2));
 if(fault==9)put(timing,0x28,uint64_t(270000000));
 memcpy(original,params,sizeof(params));sinkReads=comparisons=0;
 // The non-modeset caller must see exactly the unmodified donor plan.
 assert(XE::wrapBootModeCompare(f.ctl,f.fb,f.path,params,timing,nullptr)==(!unreadDSC() || fault==21));
 assert(!sinkReads && !memcmp(original,params,sizeof(params)));
 if(fault==17)m.regs[0x46010]=0; // source proof fails before receiver reads
 if(fault==20)put(f.path,0x3644,1U); // inherited ownership no longer matches
 if(fault==21)assert(!XE::wrapDisplay13DisablePort(f.port)); // retire before comparison
 bool full=XE::selectBootMode(f.ctl,f.fb,f.path,params,timing,nullptr);
 bool keep=fault==0 || fault==8;
 assert(full==!keep);
 assert(bool(display13FBPower.firmwareLink)==keep);
 assert(display13FBPower.bootPanelDSCCheckPending); // independent of keeping the link
 if(fault==17)m.regs[0x46010]=0xcc000000;
 if(fault==20 || fault==21)put(f.path,0x3644,2U);
 if(keep){assert(!memcmp(params,f.path+0x128,sizeof(params)));
  assert(Display13FBMemory::field<uint64_t>(f.fb,0x4358)==270000000);}
 else assert(!memcmp(original,params,sizeof(params)));
}
void restartPanel(Fixture &f,Map &map,unsigned ddi) {
 auto &c=display13FBPower;
 // Model the native stop and reacquisition around the real port wrappers.
 // Keep the AUX read failure from the mode comparison for the unread case.
 restarting=true;identityFailure=optionalReads=0;dscReadFails=unreadDSC();dscWriteFails=false;downstream=false;
 nativeDSCWriteFails=true;nativeDSCWrites=0;
 dscEnable=stoppedSink==Supported?(unreadDSC()?1:3):0;dscReads=dscWrites=trains=0;
 state.orgTGLPathLookup=reinterpret_cast<uintptr_t>(+[](void *port)->OSObject* {
  return static_cast<OSObject *>(firstPath(port));
 });
 state.orgDisplay13PortTraining=reinterpret_cast<uintptr_t>(trainAfterStop);
 currentPath=f.path;f.port[0x115]=1;
 assert(XE::wrapDisplay13PortTraining(f.port,nullptr)==kIOReturnNotReady);
 assert(!dscReads && !dscWrites && !trains && c.bootPanelDSCCheckPending);
 if(donor) {
  replayCleanup=true;
  setupReconstruction(f,map,ddi,donor);
  nativeStop(f,donor);
  assert(nativeDSCWrites==(unreadDSC() && stoppedSink!=Unsupported && stoppedSink!=Legacy?1U:0U));
  assert(!objectRefs.count(f.port) && c.bootPanelDSCCheckPending);
  assertStopped(f,map,ddi);
  memcpy(retainedPowerRefs,c.bridge.state.refs,sizeof(retainedPowerRefs));
  // The standalone preflight controls below borrow DDI power without starting
  // the PLL. Restore that baseline before replaying the full native modeset.
  nativeGetDDI(f.port);
 } else {
  map.regs[0x70008]=map.regs[0x60400]=map.regs[0x60540]=0;
  assert(!XE::wrapDisplay13DisablePort(f.port) && !objectRefs.count(f.port));
 }
 // Qualify inherited CMTG at adoption, then vary each register independently.
 map.regs[0x60404]=map.regs[0x6fa88]=0;
 // A previous owner may leave DSC or CMTG active even with the pipe off.
 for(auto reg:{0x60404U,0x6fa88U,0x78004U}) {
  map.regs[reg]=reg==0x60404?8:0x80000000U;
  assert(XE::wrapDisplay13PortTraining(f.port,nullptr)==kIOReturnNotReady);
  assert(!dscReads && !dscWrites && !trains && c.bootPanelDSCCheckPending);
  map.regs[reg]=0;
 }
 // Native DSC owns compressed training. It must neither consume the plain
 // duty nor run WEX DSC-off reads/writes, even with unreadable capability.
 auto nativeTraining=state.orgDisplay13PortTraining;
 state.orgDisplay13PortTraining=reinterpret_cast<uintptr_t>(+[](void*,void*)->uint32_t{return 0x1234;});
 f.port[0x11c]=1;identityFailure=1;
 assert(XE::wrapDisplay13PortTraining(f.port,nullptr)==0x1234);
 assert(c.bootPanelDSCCheckPending && !dscReads && !dscWrites);
 f.port[0x11c]=0;state.orgDisplay13PortTraining=nativeTraining;
 for(identityFailure=1;identityFailure<=(stoppedSink==Legacy?2U:3U);++identityFailure) {
  assert(XE::wrapDisplay13PortTraining(f.port,nullptr)==kIOReturnNotReady);
  assert(!optionalReads && !dscWrites && !trains && c.bootPanelDSCCheckPending);
 }
 identityFailure=0;
 if(stoppedSink==Supported && dscReadFails) {
  downstream=true;
  assert(XE::wrapDisplay13PortTraining(f.port,nullptr)==kIOReturnNotReady);
  assert(!dscWrites && !trains && c.bootPanelDSCCheckPending);
  downstream=false;
 }
 if(!ddi && fault==12) {
  map.regs[0x60404]=8;map.regs[0x6fa88]=0x80800000;
  // Even the qualified firmware pair cannot discharge an unknown sink
  // or authorize training while the source DSC block is still active.
  identityFailure=1;
  assert(XE::wrapDisplay13PortTraining(f.port,nullptr)==kIOReturnNotReady);
  identityFailure=0;
  map.regs[0x78004]=0x80000000U;
  assert(XE::wrapDisplay13PortTraining(f.port,nullptr)==kIOReturnNotReady);
  map.regs[0x78004]=0;
  assert(!dscWrites && !trains && c.bootPanelDSCCheckPending);
  if(stoppedSink==Supported || stoppedSink==AlreadyOff) {
   assert(XE::wrapDisplay13PortTraining(f.port,nullptr)==kIOReturnNotReady);
   assert(!dscWrites && !trains && c.bootPanelDSCCheckPending);
   map.regs[0x60404]=map.regs[0x6fa88]=0;
  }
 }
 if(donor)nativePutDDI(f.port);
 const auto lightUp=[&] {
  lightStages.clear();
  if(!donor)return XE::wrapDisplay13PortTraining(f.port,nullptr);
  alignas(8) uint8_t timing[0xa0]{};
  using ModeSet=uint32_t (*)(void *,void *,void *,const void *);
  return reinterpret_cast<ModeSet>(donor+0x4f9d4)(f.ctl,f.fb,f.path,timing);
 };
 if(stoppedSink!=Supported) {
  // These identities come from fresh post-stop AUX replies, independently
  // of the failed pre-stop receiver proof. Optional DSC is never accessed
  // on non-supporting sinks; readable off needs no write either.
  dscReadFails=false;
  assert(!lightUp());
  assert(trains==1 && !dscWrites && !c.bootPanelDSCCheckPending);
  assert(optionalReads==(stoppedSink==AlreadyOff?1U:0U));
 } else {
 dscWriteFails=true;
 if(unreadDSC() && ddi==1)nativeDSCWriteFails=false;
 assert(lightUp()==kIOReturnNotReady);
 if(donor)assert((lightStages==std::vector<Stage>{PanelOn,Clocks}));
 assert(dscWrites==1 && !trains && c.bootPanelDSCCheckPending && !c.mainLink[ddi].owner);
 if(donor) {
  assertStopped(f,map,ddi);
  assert(!memcmp(retainedPowerRefs,c.bridge.state.refs,sizeof(retainedPowerRefs)));
  assert(nativeDSCWrites==(unreadDSC()?2U:0U));
 }
 dscWriteFails=false;
 if(unreadDSC() && ddi==1) {
  if(!donor)dscEnable=0; // The native replay obtained its ACK in disableDisplay.
  assert(!dscEnable);
  dscReadFails=false;
 }
 assert(!lightUp());
 assert(dscWrites==(unreadDSC() && ddi==1?1U:2U));
 assert(dscEnable==(unreadDSC()?0:2) && trains==1 && !c.bootPanelDSCCheckPending);
 }
 if(donor)assert((lightStages==std::vector<Stage>{PanelOn,Clocks,Training,Plane,Scaler,Pipe,Brightness}));
 assert(c.mainLink[ddi].owner==reinterpret_cast<uintptr_t>(f.port));
 // Duty was discharged once. A later plain training does not repeat checks.
 const auto reads=optionalReads,writes=dscWrites;
 assert(!XE::wrapDisplay13PortTraining(f.port,nullptr));
 assert(optionalReads==reads && dscWrites==writes);
 if(donor) {
  nativeStop(f,donor);
  assertStopped(f,map,ddi);
  assert(!memcmp(retainedPowerRefs,c.bridge.state.refs,sizeof(retainedPowerRefs)));
  replayCleanup=false;
  state.display13ClockOrderReady=false;
  // The common caller also tests independent port enable/disable below.
  nativeGetDDI(f.port);
 } else assert(!XE::wrapDisplay13DisablePort(f.port));
 assert(!c.mainLink[ddi].owner && !objectRefs.count(f.port));
 restarting=false;
}
unsigned noMode(void*,int,void*,void*){return 1;}
bool noSDP(void*,void*,const void*){return false;}
int main(int argc,char **argv){
 if(argc>1){
  std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
  donor=static_cast<uint8_t*>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));
  assert(donor!=MAP_FAILED && bytes.size()<0x400000);memcpy(donor,bytes.data(),bytes.size());
  jump(donor+0x30de2,reinterpret_cast<uintptr_t>(noMode));jump(donor+0x4eefc,reinterpret_cast<uintptr_t>(noSDP));
  // Replay the installed LightUpEDP body with the real training wrapper.
  // The separate panel-runtime mode exercises the actual patch installer and PPS wrapper.
  for(const auto &patch:Display13Panel::lightPatches) {
   assert(!memcmp(donor+patch.offset,patch.before,patch.size));
   memcpy(donor+patch.offset,patch.after,patch.size);
  }
  jump(donor+0x48652,reinterpret_cast<uintptr_t>(lightPower));
  jump(donor+0x1ba02,reinterpret_cast<uintptr_t>(XE::wrapDisplay13PortTraining));
  jump(donor+0x1108,reinterpret_cast<uintptr_t>(lightPlane));
  jump(donor+0x65fd4,reinterpret_cast<uintptr_t>(lightScaler));
  jump(donor+0x48bb6,reinterpret_cast<uintptr_t>(lightPipe));
  jump(donor+0x4a56a,reinterpret_cast<uintptr_t>(lightBacklight));
  redirect(donor,0x507b1,0x300000,reinterpret_cast<uintptr_t>(lightDelay));
  installReconstructionReplay(donor);
 }

 if(argc>2){
  std::ifstream input(argv[2],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
  accelerator=static_cast<uint8_t*>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));
  assert(accelerator!=MAP_FAILED && bytes.size()<0x400000);memcpy(accelerator,bytes.data(),bytes.size());
  redirect(accelerator,0x7ef0f,0x300000,reinterpret_cast<uintptr_t>(strncmp));
 }
 for(fault=0;fault<=21;++fault)for(unsigned ddi:{0U,1U})for(unsigned mode:{0U,1U,3U,4U})for(bool foreign:{false,true})
 for(auto sink:{Supported,Unsupported,Legacy,AlreadyOff}) {
  if(sink!=Supported && fault!=12 && fault!=0)continue;
  stoppedSink=sink;
  objectRefs.clear();objectSizes.clear();realDisable=nullptr;
  Fixture f(0,0,ddi);Map map;setup(f,map,ddi,0,4,20,true);
  auto &c=display13FBPower;c.bridge.~NativeBridge();new(&c.bridge)Display13Power::NativeBridge;
  c.bootPanelDSCCheckPending=false;
  map.regs[0x45404]=0xfc0f;
  // For the error-unwind replay, begin() owns the DDI well before capture.
  // The remaining handoff cases retain the pre-existing firmware requests.
  map.regs[0x45454]=donor && (fault!=0 && fault!=8) ? 0 : 15;
  map.regs[0x45444]=1U<<(2*ddi);map.regs[0x42000]=0x8e380000;
  map.regs[0x60400]=0x82030006|((ddi+1)<<27);map.regs[0x70008]=0xc0000000;
  map.regs[0x64000+ddi*0x100]=0x80000006;map.regs[0x46140]=(ddi+1)<<28;
  if(!ddi && fault==12) {
   map.regs[0x60404]=8;map.regs[0x6fa88]=0x80800000;
  }
  map.regs[0x60540]=0x80040300;map.regs[0x60030]=0x7e300000;
  map.regs[0x60034]=map.regs[0x60044]=0x800000;map.regs[0x60040]=0x400000;
  map.regs[0x46010]=0xcc000000;map.regs[0x164284]=(0x4000U<<10)|0x151;
  map.regs[0x164288]=(2U<<2)|(2U<<6);map.regs[0x6001c]=0x077f0437;
  uint32_t timings[]={0x0897077f,0x0897077f,0x080307d7,0x04640437,0x04640437,0x0440043b};
  for(unsigned i=0;i<6;++i)map.regs[0x60000+4*i]=timings[i];
  Display13FBBootIO io{&map};assert(c.bridge.begin(io,ddi,3).status==Display13Power::Status::Complete);
  auto boot=AlderLakeN::readDisplay(io,true);
  assert(AlderLakeN::holdDPBootAUX(io,boot,c.bridge,c.mainLink[ddi],ddi,true).status==Display13Power::Status::Complete);
  assert(Display13Power::publishObject(c.bridge,c.power,0x80,f.ctl,sizeof(f.ctl)));
  IOWorkLoop loop;loop.size=sizeof(loop);IOFilterInterruptEventSource irq;
  irq.provider=f.ctl;irq.loop=&loop;irq.enabled=true;put(f.ctl,0xe20,&irq);
  state.orgAlderLakeNSetupWorkLoop=reinterpret_cast<uintptr_t>(nativeSetup);
  c.pipe[0].memoryFramebuffer=reinterpret_cast<IOService*>(f.fb);
  put(f.fb,0x44de,uint8_t(1));put(f.fb,0x1e7,uint8_t(1));put(f.fb,0x3fd8,0U);
  put(f.fb,0x4a00,1U);put(f.fb,0x4a08,f.path+0);
  put(f.port,0x550,mode);if(foreign)put(f.port,0x578,static_cast<void*>(f.fb));
  memoryResult={};memoryResult.status=Display13FBMemory::Status::Complete;
  put(f.ctl,0x1a10,1U);put(f.ctl,0x1a30,0x1000000U);put(f.ctl,0x1a34,0x1000U);
  memoryResult.pool=Display13FBMemory::pool(f.ctl);memoryResult.surface=0x10000;
  put(f.plane,0x120,memoryResult.surface);
  memoryResult.control=0x94000008;memoryResult.stride=120;memoryResult.size=0x0437077f;
  put(f.plane,0x100,memoryResult.control);put(f.plane,0x118,memoryResult.stride);put(f.plane,0x11c,memoryResult.size);
  XE::wrapAlderLakeNSetupWorkLoop(reinterpret_cast<IOService*>(f.ctl),&loop);
  const bool valid=mode<2&&!foreign;
  assert(c.bridge.bootAdopted==valid && c.bridge.state.faulted==!valid);
  if(valid){
   assert(c.mainLink[ddi].owner==reinterpret_cast<uintptr_t>(f.port)&&!c.mainLink[ddi].bootPending);
   assert(c.bridge.bootPath==reinterpret_cast<uintptr_t>(f.path)&&objectRefs[f.port]==1);
   if(fault==7){
    // Same GPU address, native setupPlane staged a tiled primary. The mode
    // consumer retires the firmware claim before the pending plane is armed.
    if(donor){put(f.path,0xf8,1920U);put(f.path,0xfc,1080U);put(f.fb,0x4330,memoryResult.surface);
     put(f.fb,0x4334,7680U);f.fb[0x4a18]=1;
     reinterpret_cast<void(*)(void*,void*)>(donor+0x1f50)(f.plane,f.path);
    } else put(f.plane,0x100,memoryResult.control|0x1000U);
    assert(Display13FBMemory::field<unsigned>(f.plane,0x120)==memoryResult.surface);
    assert(Display13FBMemory::field<unsigned>(f.plane,0x100)&0x1c00);
    firstMode(f,map);assert(c.firmwarePlane.status==Display13FBMemory::Status::Unavailable);
   } else {resources(f,map);firstMode(f,map);}
   XE::wrapAlderLakeNSetupWorkLoop(reinterpret_cast<IOService*>(f.ctl),&loop);
   assert(c.firmwarePlane.status==Display13FBMemory::Status::Unavailable);
   // Native first mode change stops the inherited buffer, then can reacquire
   // the same AUX owner for normal training/enable. Mode0 is not rewritten.
   if(fault!=0 && fault!=8)restartPanel(f,map,ddi);
   else assert(!XE::wrapDisplay13DisablePort(f.port));
   assert(!c.mainLink[ddi].owner);
   assert(!objectRefs.count(f.port));assert(!XE::wrapDisplay13EnablePort(f.port));
   assert(!XE::wrapDisplay13DisablePort(f.port));assert(!objectRefs.count(f.port));
   assert(Display13FBMemory::field<unsigned>(f.port,0x550)==mode);
  } else assert(!c.mainLink[ddi].owner&&!objectRefs.count(f.port));
  assert(!locked);
 }
 if(donor)puts("PASS native eDP modeset error unwind through port and Combo clocks, peer preserved, retry succeeds");
 puts("PASS link-loss inputs, non-DSC and unknown sinks, native DSC ownership, fastset and stopped DSC retry; same-address tiling and AUX leases");
}
'''
with tempfile.TemporaryDirectory(prefix='display13-handoff-') as d:
 p=Path(d);(p/'test.cpp').write_text(base+main)
 subprocess.run(['clang++','-std=c++17','-O1','-g',*(['-arch','x86_64'] if len(sys.argv)>1 else ['-fsanitize=address,undefined','-fno-sanitize-recover=all']),'-I'+str(r/'WhateverXe'),'-I'+str(r/'tests'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 args=[]
 if len(sys.argv)>1:
  donor=Path(sys.argv[1]);data=donor.read_bytes()
  assert hashlib.sha256(data).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
  def target(at):
   assert data[at]==0xe8
   return at+5+int.from_bytes(data[at+1:at+5],'little',signed=True)
  assert target(0x4fbd1)==0x1f50 and target(0x4fca1)==target(0x50ef2)==0x4f044
  assert data[0x4fca6:0x4fcaa]==bytes.fromhex('84c0743d')
  assert target(0x4fd69)==target(0x4fddb)==0x1108
  assert data[0x4fd50:0x4fd55]==data[0x4fda1:0x4fda6]==bytes.fromhex('baf0000000')
  args.append(str(donor))
 if len(sys.argv)>2:
  accel=Path(sys.argv[2]);data=accel.read_bytes()
  assert hashlib.sha256(data).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
  assert target(0x7f2dd)==0x7ee9a and data[0x7f34c:0x7f354]==bytes.fromhex('31f6ff90a8090000')
  args.append(str(accel))
 subprocess.run([str(p/'test'),*args],check=True)
