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
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct XE {
 static XE *callbackXE;
 bool display13FramebufferRequested=true,display13PanelPowerReady=true;
 Display13OutputWiring adlnOutputWiring;XELP::VBT::Ports firmwarePorts;
 XELP::Profile xeProfile;void *adlnTopologyController=nullptr,*adlnPlatform=nullptr;
 uintptr_t orgDisplay13PanelPower=0;
 static bool display13PanelTarget(void*);static bool display13PanelPowerReference(bool);
 static bool display13PanelCapture(void*);static void display13PanelTiming(void*,bool);
 static uint32_t display13PanelBrightness(void*,uint32_t);static void display13PanelPWMEnable(void*);
 static uint32_t wrapDisplay13PanelPower(void*,uint32_t);
};XE *XE::callbackXE;
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
uint32_t lightBacklight(void*,uint32_t){lightStages.push_back(7);return 0;}
void lightPWM(void*){lightStages.push_back(8);}
void lightWrite(void *access,uint64_t reg,uint32_t value){assert(access==display13FBPower.access);display13FBPower.map->at(reg)=value;if(reg==0xC8250)lightStages.push_back(8);}
void lightDelay(unsigned){}
void jump(uint8_t *site,uintptr_t target){site[0]=0x48;site[1]=0xb8;memcpy(site+2,&target,8);site[10]=0xff;site[11]=0xe0;}
void redirect(uint8_t *image,unsigned call,unsigned stub,uintptr_t target){assert(image[call]==0xe8);jump(image+stub,target);int32_t delta=stub-call-5;memcpy(image+call+1,&delta,4);}
int main(int argc,char **argv) {
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
  Fixture cold;display13FBPower.clockStopped=stopped;
  assert(!XE::wrapDisplay13PanelPower(cold.ctl,2));cold.balanced();
  assert(calls==1 && cold.map.at(0xC7200)==0x80000000);
  assert(!XE::display13PanelBrightness(cold.ctl,32768));cold.balanced();
  assert(cold.map.at(0xC8258)==uint64_t(120000)*32768/65535);
 }
 Fixture f;assert(XE::display13PanelCapture(f.ctl));
 assert(Display13FBMemory::field<unsigned>(f.ctl,0xE60)==120000);f.balanced();
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
  jump(image+0x48652,reinterpret_cast<uintptr_t>(lightPower));jump(image+0x6ccb4,reinterpret_cast<uintptr_t>(lightClocks));
  jump(image+0x1ba02,reinterpret_cast<uintptr_t>(lightTrain));jump(image+0x1108,reinterpret_cast<uintptr_t>(lightPlane));
  jump(image+0x65fd4,reinterpret_cast<uintptr_t>(lightScaler));jump(image+0x48bb6,reinterpret_cast<uintptr_t>(lightPipe));
  jump(image+0x4a56a,reinterpret_cast<uintptr_t>(lightBacklight));redirect(image,0x507b1,0x300000,reinterpret_cast<uintptr_t>(lightDelay));
  uintptr_t vtable[64]{};vtable[0x130/8]=reinterpret_cast<uintptr_t>(lightWrite);put(f.access,0,vtable);
  alignas(8) uint8_t path[0x3730]{};put(path,0x3648,static_cast<void*>(path));
  auto light=reinterpret_cast<uint32_t(*)(void*,void*,void*,const void*)>(image+0x5063e);
  for(unsigned patched=0;patched<2;++patched) {
   if(patched)for(const auto &patch:Display13Panel::lightPatches) {
    assert(!memcmp(image+patch.offset,patch.before,patch.size));memcpy(image+patch.offset,patch.after,patch.size);
   }
   for(unsigned power:{0U,0xe00002d8U})for(unsigned train:{0U,0xe00002d8U}) {
    lightStages.clear();powerResult=power;trainingResult=train;
    const auto result=light(f.ctl,nullptr,path,nullptr);
    if(!patched){assert(lightStages.size()==8 && result==train);}
    else {assert(result==(power?power:train));assert(lightStages.size()==(power?1U:train?3U:8U));}
   }
  }
  // Replay the donor caller through the production power wrapper, including
  // its timing callback and PG owner. CDCLK hardware and training are leaves.
  Fixture cold;put(cold.access,0,vtable);realPanelPower=true;trainingResult=0;
  display13FBPower.clockStopped=true;lightStages.clear();
  assert(!light(cold.ctl,nullptr,path,nullptr));cold.balanced();
  assert(calls==1 && !display13FBPower.clockStopped && lightStages.size()==8);
  realPanelPower=false;
  munmap(image,0x400000);
  puts("PASS exact native LightUpEDP power/train failure exits; original donor counterexamples");
 }
 puts("PASS production Display13 PPS/PWM wrappers: unlocked native calls, balanced/published PG, on/off/failure cleanup, foreign owner rejection");
}
