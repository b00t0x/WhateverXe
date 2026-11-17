#include "kern_display13_panel.hpp"
#include "kern_display13_power_bridge.hpp"
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_output_wiring.hpp"
#include <cassert>
#include <vector>
#include <new>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
#include <cstdio>
#include <map>
#include "kern_display13_firmware.hpp"
struct IOMemoryMap {
 std::vector<uint32_t> memory=std::vector<uint32_t>(0x400000);
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(memory.data());}
 uint64_t getLength(){return 0x1000000;}
 uint32_t &at(uint32_t reg){return memory.at(reg/4);}
};
struct Lock {bool held=false;};
void IOLockLock(Lock *p){assert(p && !p->held);p->held=true;}
void IOLockUnlock(Lock *p){assert(p && p->held);p->held=false;}
void IODelay(unsigned){}
bool display13TCAuxSupported(){return false;}
bool display13TCPrepareAUX(IOMemoryMap*,unsigned){assert(false);return false;}
bool display13TCHealthyAUX(IOMemoryMap*,unsigned){assert(false);return false;}
// PRODUCTION_IO
struct Context {
 IOMemoryMap *map=nullptr;void *controller=nullptr,*power=nullptr,*access=nullptr;
 Lock *lock=nullptr,*panelLock=nullptr;Display13Power::NativeBridge bridge;Display13Panel::State panel;
 bool clockStopped=false,clockFaulted=false,panelFaulted=false;
}display13FBPower;
bool mapping=true;bool adlnWriterMappingBound(){return mapping;}
unsigned portRefs=0;void *currentPort=nullptr;bool auxOwner=true;unsigned auxCalls=0,failAux=0;
std::map<uint32_t,uint8_t> sink;
struct OSObject {void retain(){++portRefs;}void release(){assert(portRefs);--portRefs;}};
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
using mach_vm_address_t=uintptr_t;
void *IOSimpleLockAlloc(){assert(false);return nullptr;}
Lock *IOLockAlloc(){assert(false);return nullptr;}
struct KernelPatcher {
 enum class Error {NoError,Failed};Error error=Error::NoError;
 unsigned blocks=0,routes=0,failBlock=0;uintptr_t routed=0;
 struct RouteRequest {
  uintptr_t handler,*original;
  template<class F>RouteRequest(const char*,F f,uintptr_t &o):handler(reinterpret_cast<uintptr_t>(f)),original(&o){}
 };
 uintptr_t solveSymbol(size_t,const char*,uintptr_t address,size_t){return address+0x48652;}
 void clearError(){error=Error::NoError;}Error getError(){return error;}
 void routeBlock(uintptr_t address,const uint8_t *bytes,size_t size){
  if(++blocks==failBlock){error=Error::Failed;return;}memcpy(reinterpret_cast<void*>(address),bytes,size);
 }
 bool routeMultipleShort(size_t,RouteRequest *r,unsigned count,uintptr_t address,size_t){
  assert(count==1);++routes;routed=r->handler;*r->original=address+0x48652;return true;
 }
};
struct XE {
 static XE *callbackXE;
 void *tglAUXLock=reinterpret_cast<void*>(1);
 bool tglEDPTranscoderReady=true,tglPanelDepthReady=true,tglPanelTimingReady=true,display13PanelPWMReady=true,tglSinkFramingReady=true;
 // Common panel/clock installers are prerequisites covered by their own
 // replays. The new Display13 installer and its generated call bridge run here.
 unsigned nativePanelCalls=0;
 void processNativePanel(KernelPatcher&,size_t,uintptr_t,size_t){++nativePanelCalls;}
 void processDisplay13Panel(KernelPatcher&,size_t,uintptr_t,size_t);
 bool display13FramebufferRequested=true,display13PanelPowerReady=true,adlsDisplaySelected=false;
 struct {unsigned panel=0;}adlsWiring;XELP::PanelPWM adlsPanelPWM;
 uintptr_t orgADLSLightUpEDP=0;
 static bool adlsCapturePanelPWM(void*){assert(false);return false;}
 static uint32_t wrapADLSLightUpEDP(void*,void*,void*,const void*);
 static bool display13PanelHealthy();
 Display13OutputWiring adlnOutputWiring;XELP::VBT::Ports firmwarePorts;
 XELP::Profile xeProfile;void *adlnTopologyController=nullptr,*adlnPlatform=nullptr;
 uintptr_t orgDisplay13PanelPower=0;
 uintptr_t tglPortByDDI=0;
 static bool display13AUXReady(void *p,bool){return auxOwner && p==currentPort;}
 static uint32_t wrapDisplay13ReadAUX(void*,uint32_t,void*,uint32_t);
 static uint32_t wrapDisplay13WriteAUX(void*,uint32_t,void*,uint32_t);
 static bool display13PanelAUX(void*,uint32_t,unsigned);
 static bool display13PanelTarget(void*);static bool display13PanelPowerReference(bool);
 static bool display13PanelCapture(void*);static void display13PanelTiming(void*,bool);
 static uint32_t display13PanelBrightness(void*,uint32_t);static void display13PanelPWMEnable(void*);
 static uint32_t wrapDisplay13PanelPower(void*,uint32_t);
 static void display13PanelPWMPeriod(void*);
};XE *XE::callbackXE;
bool auxAccess(void *p) {
 auto &c=display13FBPower;
 assert(p==currentPort && portRefs==1 && c.panelLock->held && !c.lock->held);
 assert(c.bridge.state.refs[static_cast<unsigned>(Display13Power::Domain::AUXA)]>0);
 ++auxCalls;return !failAux || auxCalls!=failAux;
}
uint32_t XE::wrapDisplay13ReadAUX(void *p,uint32_t a,void *d,uint32_t n) {
 if(!auxAccess(p))return kIOReturnNotReady;
 for(unsigned i=0;i<n;++i)static_cast<uint8_t*>(d)[i]=sink.at(a+i);return 0;
}
uint32_t XE::wrapDisplay13WriteAUX(void *p,uint32_t a,void *d,uint32_t n) {
 if(!auxAccess(p))return kIOReturnNotReady;
 for(unsigned i=0;i<n;++i)sink[a+i]=static_cast<uint8_t*>(d)[i];return 0;
}
// METHODS
unsigned calls=0;bool timeout=false;uint32_t nativeResult=0;
uint32_t nativePower(void *controller,uint32_t state) {
 auto &c=display13FBPower;assert(controller==c.controller && !c.lock->held && c.panelLock->held);++calls;
 assert(c.bridge.nativeRefs(1)==2 && Display13FBMemory::field<unsigned>(c.power,0x20)==2);
 if(state==2)assert(c.map->at(0xC2020)&(1U<<29));
 // Native config invokes the property callback, then proceeds with sequencing.
 XE::display13PanelTiming(controller,true);
 if(!timeout)c.map->at(0xC7200)=state==2?0x80000000:0;
 c.map->at(0xC2020)|=0x10000;return nativeResult;
}
template<class T>void put(void *p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
struct Fixture {
 IOMemoryMap map;Lock owner,pps;XE xe;alignas(8) uint8_t ctl[0x1c20]{},power[0x80]{},access[0x80]{},platform[0x1c0]{};
 Fixture() {
  display13FBPower.~Context();new(&display13FBPower)Context;auto &c=display13FBPower;c.map=&map;c.controller=ctl;c.power=power;c.access=access;c.lock=&owner;c.panelLock=&pps;
  xe.callbackXE=&xe;xe.adlnTopologyController=ctl;xe.adlnPlatform=platform;xe.adlnOutputWiring.valid=true;xe.adlnOutputWiring.panel=1;
  xe.xeProfile=XELP::identify(0xA7A88086,4);xe.orgDisplay13PanelPower=reinterpret_cast<uintptr_t>(nativePower);
  auto &v=xe.firmwarePorts;v.status=XELP::VBT::Status::Valid;v.present=1;v.port[0]={0x78C6,10,0,0x40,0,0};
  v.panel.valid=v.panel.pwmPresent=v.panel.backlightValid=true;v.panel.ddi=0;v.panel.backlightType=2;v.panel.pwmHz=200;
  put(ctl,0xC8,static_cast<void*>(power));put(ctl,0xC40,static_cast<void*>(access));put(ctl,0xD40,static_cast<void*>(platform));
  map.at(Display13Power::Driver)=0xFC0F;map.at(Display13Power::DDI)=15;map.at(Display13Power::Fuses)=0x8e380000;
  map.at(0xC7208)=0x083401f4;map.at(0xC720C)=0x138801f4;map.at(0xC7204)=0x67;
  map.at(0xC8250)=0xa0000000;map.at(0xC8254)=120000;map.at(0xC8258)=60000;map.at(0xC2020)=0x1234;
  Display13FBPanelIO io{&map};assert(c.bridge.begin(io,0,1).status==Display13Power::Status::Complete);
  assert(Display13Power::publishObject(c.bridge,power,sizeof(power),ctl,sizeof(ctl)));
  calls=0;timeout=false;nativeResult=0;mapping=true;
 }
 void balanced(){auto &c=display13FBPower;assert(c.bridge.nativeRefs(1)==1 && Display13FBMemory::field<unsigned>(power,0x20)==1 && !owner.held && !pps.held);}
};
bool realPanelPower=false;
unsigned powerResult=0,trainingResult=0;std::vector<unsigned> lightStages;
uint32_t lightPower(void *controller,uint32_t state){assert(state==2);lightStages.push_back(1);return realPanelPower?XE::wrapDisplay13PanelPower(controller,state):powerResult;}
void lightClocks(void*,void*,void*){lightStages.push_back(2);
 if(realPanelPower){assert(display13FBPower.clockStopped && display13FBPower.map->at(0xC7200)==0x80000000);display13FBPower.clockStopped=false;}
}
uint32_t lightTrain(void*,void *request){assert(!request);lightStages.push_back(3);return trainingResult;}
void lightPlane(void*,bool enabled){assert(!enabled);lightStages.push_back(4);}
void lightScaler(void*,void*){lightStages.push_back(5);}
void lightPipe(void*,void*,void*,const void*){lightStages.push_back(6);}
uint32_t lightBacklight(void *ctl,uint32_t brightness){lightStages.push_back(7);return realPanelPower?XE::display13PanelBrightness(ctl,brightness):0;}
void lightPWM(void *access){lightStages.push_back(8);if(realPanelPower)XE::display13PanelPWMEnable(access);}
void lightWrite(void *access,uint64_t reg,uint32_t value){assert(access==display13FBPower.access);display13FBPower.map->at(reg)=value;if(reg==0xC8250)lightStages.push_back(8);}
void lightDelay(unsigned){}
void jump(uint8_t *site,uintptr_t target){site[0]=0x48;site[1]=0xb8;memcpy(site+2,&target,8);site[10]=0xff;site[11]=0xe0;}
void redirect(uint8_t *image,unsigned call,unsigned stub,uintptr_t target){assert(image[call]==0xe8);jump(image+stub,target);int32_t delta=stub-call-5;memcpy(image+call+1,&delta,4);}
int main(int argc,char **argv) {
 // VBT interface 5 -> production PPS capture -> native power -> backlight
 // callbacks -> AUX bytes, with PWM registers deliberately unusable. Native
 // AUX serialization/electrical replies are leaves; leases are production.
 for(bool wide:{false,true})for(bool auxEnable:{false,true})for(unsigned failure=0;failure<12;++failure) {
  Fixture f;auto &c=display13FBPower;alignas(8) uint8_t port[0x600]{};uint32_t config[6]{};
  put(port,0x548,config);currentPort=port;portRefs=0;auxOwner=true;auxCalls=failAux=0;
  f.xe.tglPortByDDI=reinterpret_cast<uintptr_t>(+[](unsigned ddi)->OSObject*{assert(!ddi);return static_cast<OSObject*>(currentPort);});
  f.xe.firmwarePorts.panel.backlightType=5;f.xe.firmwarePorts.panel.pwmPresent=false;
  assert(XELP::VBT::display13Fixed(f.xe.firmwarePorts,3,f.xe.xeProfile.family).panel==1);
  f.map.at(0xC8250)=f.map.at(0xC8254)=f.map.at(0xC8258)=~0U;f.map.at(0x45444)=3;
  sink={{0x700,3},{0x701,uint8_t(auxEnable?5:3)},{0x702,uint8_t(wide?6:2)},{0x724,uint8_t(wide?12:8)},
        {0x720,0x82},{0x721,0x28},{0x722,0},{0x723,0}};
  assert(!XE::wrapDisplay13PanelPower(f.ctl,2));f.balanced();
  assert(c.panel.aux && Display13FBMemory::field<unsigned>(f.ctl,0xE60)==65535);
  failAux=failure;const auto r=XE::display13PanelBrightness(f.ctl,32768);
  if(failure && failure<=auxCalls)assert(r==kIOReturnNotReady);
  failAux=0;assert(!XE::display13PanelBrightness(f.ctl,32768));
  const auto expected=wide?2047U:127U;
  assert(sink[0x722]==(wide?expected>>8:expected) && (!wide || sink[0x723]==uint8_t(expected)));
  assert(sink[0x721]==0x2a && sink[0x720]==0x82);
  XE::display13PanelPWMPeriod(f.access);XE::display13PanelPWMEnable(f.access);assert(!c.panelFaulted && sink[0x720]==(auxEnable?0x83:0x82));
  assert(!XE::wrapDisplay13PanelPower(f.ctl,0) && sink[0x720]==0x82);
  // Simulate panel power loss: no stale protocol readiness, preserve level.
  sink[0x721]=0;sink[0x722]=sink[0x723]=0;
  assert(!XE::wrapDisplay13PanelPower(f.ctl,2));XE::display13PanelPWMEnable(f.access);
  assert(sink[0x721]==2 && sink[0x720]==(auxEnable?0x83:0x82) && sink[0x722]==(wide?expected>>8:expected));
  for(unsigned value:{0U,1U,65535U}) {
   assert(!XE::display13PanelBrightness(f.ctl,value));const auto level=uint32_t(uint64_t(value)*(wide?4095:255)/65535);
   assert(sink[0x722]==(wide?level>>8:level) && (!wide || sink[0x723]==uint8_t(level)));
  }
  const auto noCalls=auxCalls;assert(XE::display13PanelBrightness(f.ctl,65536)==kIOReturnNotReady && auxCalls==noCalls);
  auxOwner=false;const auto before=auxCalls;assert(XE::display13PanelBrightness(f.ctl,100)==kIOReturnNotReady && auxCalls==before);
  auxOwner=true;sink[0x702]=0;assert(XE::display13PanelBrightness(f.ctl,100)==kIOReturnNotReady);
  assert(f.map.at(0xC8250)==~0U && f.map.at(0xC8254)==~0U && f.map.at(0xC8258)==~0U);
  assert(!portRefs && !c.bridge.state.refs[static_cast<unsigned>(Display13Power::Domain::AUXA)]);f.balanced();
 }

 // Independent AUX-enable/PWM-level hybrid: no AUX brightness writes.
 {Fixture f;auto &c=display13FBPower;alignas(8) uint8_t port[0x600]{};uint32_t config[6]{};
  put(port,0x548,config);currentPort=port;portRefs=0;auxOwner=true;auxCalls=failAux=0;
  f.xe.tglPortByDDI=reinterpret_cast<uintptr_t>(+[](unsigned)->OSObject*{return static_cast<OSObject*>(currentPort);});
  f.xe.firmwarePorts.panel.backlightType=5;f.map.at(0x45444)=3;
  sink={{0x700,3},{0x701,5},{0x702,1},{0x720,0x82},{0x721,2},{0x722,0x55},{0x723,0xaa}};
  assert(!XE::wrapDisplay13PanelPower(f.ctl,2));assert(!XE::display13PanelBrightness(f.ctl,32768));
  XE::display13PanelPWMPeriod(f.access);XE::display13PanelPWMEnable(f.access);
  assert(!c.panelFaulted && sink[0x720]==0x83 && sink[0x721]==0 && sink[0x722]==0x55 && sink[0x723]==0xaa);
  assert(f.map.at(0xC8258)==60000 && f.map.at(0xC8254)==120000 && f.map.at(0xC8250)==0xa0000000);
  assert(!XE::wrapDisplay13PanelPower(f.ctl,0));assert(sink[0x720]==0x82 && !f.map.at(0xC8250));f.balanced();assert(!portRefs);
 }
 for(bool gate:{false,true})for(bool fail:{false,true}) {
  Fixture f;if(gate)f.map.at(0xC2020)|=1U<<29;timeout=fail;
  assert(XE::wrapDisplay13PanelPower(f.ctl,2)==(fail?kIOReturnNotReady:0));
  f.balanced();assert(calls==1 && f.map.at(0xC2020)==(0x11234|(gate?1U<<29:0)));
  assert(display13FBPower.panelFaulted==fail);
  if(!fail){assert(!XE::wrapDisplay13PanelPower(f.ctl,0));f.balanced();assert(!f.map.at(0xC7200));}
 }
 for(unsigned bad=0;bad<10;++bad) {
  Fixture f;auto &c=display13FBPower;
  switch(bad){case 0:mapping=false;break;case 1:f.xe.adlnPlatform=nullptr;break;case 2:c.bridge.state.suspended=true;break;
   case 3:c.clockFaulted=true;break;case 4:f.xe.firmwarePorts.panel.controller=1;break;
   case 5:f.xe.firmwarePorts.panel.backlightType=3;break;case 6:f.map.at(0xC8254)=~0U;break;
   case 7:put(f.ctl,0xD0,static_cast<void*>(f.power));break;case 8:f.map.at(0xC2020)=~0U;break;
   case 9:f.xe.adlnTopologyController=nullptr;break;}
  assert(XE::wrapDisplay13PanelPower(f.ctl,2)==kIOReturnNotReady && !calls);f.balanced();
 }
 // PPS/PWM use the PCH reference clock. LightUpEDP powers the panel before
 // its CDCLK callback, so an intentionally stopped CDCLK is a valid input.
 for(bool stopped:{false,true}) {
  auxCalls=0;const auto previousSink=sink;
  Fixture cold;display13FBPower.clockStopped=stopped;
  assert(!XE::wrapDisplay13PanelPower(cold.ctl,2));cold.balanced();
  assert(calls==1 && cold.map.at(0xC7200)==0x80000000);
  assert(!XE::display13PanelBrightness(cold.ctl,32768));cold.balanced();
  assert(cold.map.at(0xC8258)==uint64_t(120000)*32768/65535);
  assert(!auxCalls && sink==previousSink); // existing PWM never contacts an AUX sink
 }
 Fixture f;assert(XE::display13PanelCapture(f.ctl));
 assert(Display13FBMemory::field<unsigned>(f.ctl,0xE60)==120000);f.balanced();
 f.map.at(0xC8254)=0;XE::display13PanelPWMPeriod(f.access);assert(f.map.at(0xC8254)==120000);
 for(unsigned brightness:{0U,1U,32768U,65535U}) {
  assert(!XE::display13PanelBrightness(f.ctl,brightness));
  assert(f.map.at(0xC8258)==uint64_t(120000)*brightness/65535);f.balanced();
 }
 assert(XE::display13PanelBrightness(f.ctl,65536)==kIOReturnNotReady);f.balanced();
 f.map.at(0xC8250)=0;XE::display13PanelPWMEnable(f.access);assert(f.map.at(0xC8250)==0xa0000000);f.balanced();
 // The actual adapter's write allowlist must not grant a second PPS owner.
 Display13FBPanelIO io{&f.map};io.write(0xC7204,0);assert(f.map.at(0xC7204)==0x67);
 io.write(0x60400,123);assert(!f.map.at(0x60400));
 if(argc>1) {
  std::ifstream input(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
  assert(!bytes.empty() && bytes.size()<0x400000);
  auto image=static_cast<uint8_t *>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));assert(image!=MAP_FAILED);
  memcpy(image,bytes.data(),bytes.size());
  // Non-eDP boards may still have unused panel facts in firmware. With
  // DP-only, HDMI-only or DSI+external wiring, this eDP installer must leave
  // the donor and common panel hooks completely untouched.
  const auto savedWiring=f.xe.adlnOutputWiring;
  for(unsigned transport=0;transport<3;++transport) {
   f.xe.adlnOutputWiring={};f.xe.adlnOutputWiring.valid=true;
   if(transport==0)f.xe.adlnOutputWiring.dp=0x18;
   if(transport==1){f.xe.adlnOutputWiring.hdmi=2;f.xe.adlnOutputWiring.ddc[1]=2;}
   if(transport==2){f.xe.adlnOutputWiring.dsi=1;f.xe.adlnOutputWiring.dp=8;}
   assert(f.xe.adlnOutputWiring.described());
   KernelPatcher untouched;const auto calls=f.xe.nativePanelCalls;
   f.xe.display13PanelPowerReady=false;
   f.xe.processDisplay13Panel(untouched,0,reinterpret_cast<uintptr_t>(image),bytes.size());
   assert(!untouched.blocks && !untouched.routes && f.xe.nativePanelCalls==calls);
   assert(!f.xe.display13PanelPowerReady && !memcmp(image,bytes.data(),bytes.size()));
  }
  f.xe.adlnOutputWiring=savedWiring;
  // Execute the production installer on the untouched exact donor. The
  // replay consumes its actual bytes; it must not manufacture a second bridge.
  for(unsigned fail=1;fail<=4;++fail) {
   std::vector<uint8_t> rejected(bytes.begin(),bytes.end());KernelPatcher p;
   f.xe.display13PanelPowerReady=false;
   if(fail==4)rejected[0x507BD]^=1;else p.failBlock=fail;
   f.xe.processDisplay13Panel(p,0,reinterpret_cast<uintptr_t>(rejected.data()),rejected.size());
   assert(!f.xe.display13PanelPowerReady && !p.routes);
  }
  // Preserve the PWM owner while testing installer prerequisites.
  XE::callbackXE=&f.xe;
  KernelPatcher installer;f.xe.display13PanelPowerReady=false;
  f.xe.processDisplay13Panel(installer,0,reinterpret_cast<uintptr_t>(image),bytes.size());
  assert(f.xe.display13PanelPowerReady && installer.routes==1 && installer.routed==reinterpret_cast<uintptr_t>(XE::wrapDisplay13PanelPower));
  std::vector<uint8_t> installed(image,image+bytes.size());
  jump(image+0x48652,reinterpret_cast<uintptr_t>(lightPower));jump(image+0x6ccb4,reinterpret_cast<uintptr_t>(lightClocks));
  jump(image+0x1ba02,reinterpret_cast<uintptr_t>(lightTrain));jump(image+0x1108,reinterpret_cast<uintptr_t>(lightPlane));
  jump(image+0x65fd4,reinterpret_cast<uintptr_t>(lightScaler));jump(image+0x48bb6,reinterpret_cast<uintptr_t>(lightPipe));
  jump(image+0x4a56a,reinterpret_cast<uintptr_t>(lightBacklight));redirect(image,0x507b1,0x300000,reinterpret_cast<uintptr_t>(lightDelay));
  uintptr_t vtable[64]{};vtable[0x130/8]=reinterpret_cast<uintptr_t>(lightWrite);put(f.access,0,vtable);
  alignas(8) uint8_t path[0x3730]{};put(path,0x3648,static_cast<void*>(path));
  auto light=reinterpret_cast<uint32_t(*)(void*,void*,void*,const void*)>(image+0x5063e);
  for(unsigned patched=0;patched<2;++patched) {
   for(const auto &patch:Display13Panel::lightPatches)
    memcpy(image+patch.offset,patched?installed.data()+patch.offset:reinterpret_cast<const uint8_t*>(bytes.data())+patch.offset,patch.size);
   for(unsigned power:{0U,0xe00002d8U})for(unsigned train:{0U,0xe00002d8U}) {
    lightStages.clear();powerResult=power;trainingResult=train;
    const auto result=light(f.ctl,nullptr,path,nullptr);
    if(!patched){assert(lightStages.size()==8 && result==train);}
    else {assert(result==(power?power:train));assert(lightStages.size()==(power?1U:train?3U:8U));}
   }
  }
  // Route the separately qualified enable call to the stage-recording leaf
  // for BOTH old PWM and new AUX (adls_panel_pwm_native checks its installer).
  auto enableSite=image+0x507e9;memset(enableSite,0x90,16);enableSite[0]=0x48;enableSite[1]=0xb8;
  auto enable=reinterpret_cast<uintptr_t>(lightPWM);memcpy(enableSite+2,&enable,8);enableSite[10]=0xff;enableSite[11]=0xd0;
  // Replay the donor caller through the production power wrapper, including
  // its timing callback and PG owner. CDCLK hardware and training are leaves.
  Fixture cold;put(cold.access,0,vtable);realPanelPower=true;trainingResult=0;
  assert(XE::display13PanelCapture(cold.ctl));cold.map.at(0xC8254)=0; // resume lost the PWM period
  display13FBPower.clockStopped=true;lightStages.clear();
  cold.xe.orgADLSLightUpEDP=reinterpret_cast<uintptr_t>(light);
  assert(!XE::wrapADLSLightUpEDP(cold.ctl,nullptr,path,nullptr));cold.balanced();
  assert(cold.map.at(0xC8254)==120000 && cold.map.at(0xC8250)==0xa0000000);
  assert(calls==1 && !display13FBPower.clockStopped && lightStages.size()==8);
  // Full donor tail now crosses both production PWM adapters and AUX level.
  Fixture aux;alignas(8) uint8_t port[0x600]{};uint32_t config[6]{};put(port,0x548,config);
  currentPort=port;portRefs=0;auxOwner=true;auxCalls=failAux=0;
  aux.xe.tglPortByDDI=reinterpret_cast<uintptr_t>(+[](unsigned)->OSObject*{return static_cast<OSObject*>(currentPort);});
  aux.xe.firmwarePorts.panel.backlightType=5;aux.xe.firmwarePorts.panel.pwmPresent=false;
  aux.map.at(0xC8250)=aux.map.at(0xC8254)=aux.map.at(0xC8258)=~0U;aux.map.at(0x45444)=3;
  sink={{0x700,3},{0x701,5},{0x702,6},{0x724,12},{0x720,0x82},{0x721,0},{0x722,0},{0x723,0}};
  put(aux.access,0,vtable);put(aux.ctl,0xE58,32768U);display13FBPower.clockStopped=true;
  aux.xe.orgADLSLightUpEDP=reinterpret_cast<uintptr_t>(light);
  lightStages.clear();assert(!XE::wrapADLSLightUpEDP(aux.ctl,nullptr,path,nullptr));aux.balanced();
  assert(!portRefs && sink[0x720]==0x83 && sink[0x722]==7 && sink[0x723]==255);
  assert(aux.map.at(0xC8250)==~0U && aux.map.at(0xC8254)==~0U && aux.map.at(0xC8258)==~0U);
  realPanelPower=false;
  munmap(image,0x400000);
  puts("PASS exact native LightUpEDP power/train failure exits; original donor counterexamples");
 }
 puts("PASS production Display13 PPS/PWM wrappers: unlocked native calls, balanced/published PG, on/off/failure cleanup, foreign owner rejection");
}
