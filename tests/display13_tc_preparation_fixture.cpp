#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <new>
#include "kern_display13_boot.hpp"
#include "kern_display13_tc_mode.hpp"
#include "kern_display13_fb_memory.hpp"
#include "kern_xelp_link_requirements.hpp"
#include "kern_xelp_sst_route.hpp"
struct OSObject {unsigned size=0x700;unsigned refs=0;const OSObject*getMetaClass(){return this;}unsigned getClassSize()const{return size;}void retain(){++refs;}void release(){assert(refs);--refs;}};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}template<class...T>void setProperty(T...){} bool getProperty(const char*){return false;}};
constexpr unsigned kIOReturnSuccess=0,kIOReturnNotReady=0xe00002d8;
#define OSSafeReleaseNULL(p) do{(p)->release();(p)=nullptr;}while(0)
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
bool locked=false;void IOLockLock(void*){assert(!locked);locked=true;}void IOLockUnlock(void*){assert(locked);locked=false;}
struct MMIO {
 std::map<uint32_t,uint32_t> regs;std::vector<unsigned> writes;bool failOwner=false;
 bool readable(unsigned r){return r<0x1000000;}unsigned read(unsigned r){return regs[r];}
 void delayUs(unsigned){}
 void write(unsigned r,unsigned v){
  writes.push_back(r);
  if(r==0x45404 || r==0x45444 || r==0x45454){
   const auto mask=r==0x45404?0xa80aU:0x2aaaU;unsigned ack=(v&mask)>>1;
   if(r!=0x45404)for(unsigned t=0;t<4;++t){unsigned bit=1u<<(6+2*t);
    if(!(regs[0x64300+256*t]&64) || (r==0x45454 && !(regs[0x45444]&bit)))ack&=~bit;}
   regs[r]=(v&~(mask>>1))|ack;
  } else if(!(failOwner && r>=0x64300 && r<=0x64600))regs[r]=v;
 }
};
struct Context {
 AlderLakeN::DisplayReadout displayScratch;
 Display13Power::NativeBridge bridge;Display13TC::Mode tcMode[4];
 bool tcSupported=true,tcBankFaulted=false,clockFaulted=false;unsigned tcBusy[4]{};OSObject*tcObject[4]{};
 void *lock=(void*)1,*controller=(void*)2,*access=(void*)3;MMIO*map=nullptr;
 bool tcSignalReady[4]{};unsigned tcDPLink[4]{};
} display13FBPower;
struct Display13FBPowerIO {MMIO*map;bool tcAuxSupported(){return true;}bool prepareTCAux(unsigned t){return (map->read(0x45404)&5)==5 && (map->read(0x64300+256*t)&64);}bool tcAuxHealthy(unsigned){return true;}bool readable(unsigned r){return map->readable(r);}unsigned read(unsigned r){return map->read(r);}void write(unsigned r,unsigned v){map->write(r,v);}void delayUs(unsigned n){map->delayUs(n);}};
using Display13FBBootIO=Display13FBPowerIO;
struct Display13FBTCIO:Display13FBPowerIO {
 OSObject*object;unsigned tc;
 Display13FBTCIO(OSObject*p,unsigned t):Display13FBPowerIO{display13FBPower.map},object(p),tc(t){}
 bool admitted(unsigned t){return t==tc && t<4;}
 bool accessHeld(){Display13FBPowerIO io{map};return Display13Power::held(io,display13FBPower.bridge.state,Display13TC::Mode::accessDomains());}
 bool tcAuxSupported(){return true;}bool prepareTCAux(unsigned t){return t==tc && accessHeld() && (read(0x64300+256*t)&64);}
 bool tcAuxHealthy(unsigned){return true;}
 bool quiescent(unsigned d){return !(read(0x64000+256*d)&0x80000000);}
 // MODE_QUIESCENCE
};
struct XE {
 bool display13FramebufferRequested=true,display13FBTCHooksReady=true;XELP::Profile xeProfile;
 uint64_t adlnPlatform[0x1c0/sizeof(uint64_t)]{},tglPlatformCopy[0x1c0/sizeof(uint64_t)]{};Display13OutputWiring adlnOutputWiring;
 uintptr_t display13TCBaseMode=0,orgDisplay13TCMode=0;
 bool tglHDMIEligible(const uint32_t*){return false;}XELP::Display12SSTRoutes tglSSTRoutes(){return {};}
 static XELP::LinkRequirements framebufferLinkRequirements(void*,uint32_t);
 static unsigned display13TCTarget(OSObject*,bool=false);
 static uint32_t wrapDisplay13TCMode(OSObject*,uint32_t);
 static uint32_t setDSIPortMode(OSObject*,uint32_t){assert(false && "TC must not enter DSI");return kIOReturnNotReady;}
} instance,*callbackXE=&instance;
// FUNCTIONS
alignas(8) uint8_t portBytes[0x700];
template<class T>void put(unsigned o,T v){memcpy(portBytes+o,&v,sizeof(v));}
unsigned nativeCalls=0;
unsigned base(OSObject*p,unsigned mode){
 assert(!locked);++nativeCalls;
 // Model native reentry borrowing the owner's scratch after Mode drops its
 // lock. No reference to the old snapshot may be consumed after this call.
 IOLockLock(display13FBPower.lock);
 memset(&display13FBPower.displayScratch,0xa5,sizeof(display13FBPower.displayScratch));
 IOLockUnlock(display13FBPower.lock);
 memcpy(reinterpret_cast<uint8_t*>(p)+0x550,&mode,4);return 0;
}
int main(){
 unsigned cases=0;
 for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned protocol=0;protocol<3;++protocol)for(unsigned fault=0;fault<6;++fault){
  const bool dp=protocol!=0,alt=protocol==2;if(alt && fault==1)continue;
  display13FBPower.~Context();new(&display13FBPower)Context;auto &c=display13FBPower;
  instance=XE{};instance.xeProfile=XELP::identify(0x46a68086,3);instance.display13TCBaseMode=(uintptr_t)base;
  MMIO map;c.map=&map;map.regs[0x42000]=0x8e380000;
  for(unsigned d=3;d<=6;++d){map.regs[0x161500+4*(d-3)]=4;map.regs[0x64000+256*d]=0;}
  const unsigned peer=ddi==6?3:6;map.regs[0x64000+256*peer]=0x80000046;
  Display13FBPowerIO power{&map};assert(Display13Power::initialize(power,c.bridge.state,Display13Power::nativeDomains(0x7b)));
  c.bridge.begun=true;c.bridge.selectedPorts=0x78;c.bridge.bootPort=ddi;
  auto &w=instance.adlnOutputWiring;w.valid=true;if(dp)w.dp=1u<<ddi;else{w.hdmi=1u<<ddi;w.ddc[ddi]=ddi+6;}
  if(alt){w.typeC=w.dp;map.regs[0x161500+4*(ddi-3)]=0x1d;
   map.regs[0xc4000]=0;map.regs[0x44470]=1U<<(16+ddi-3);
   Display13TC::Port phy;Display13TC::Port::resolve(ddi,phy);
   map.regs[phy.laneAssignment()]=3U<<(8*(phy.tc&1));map.regs[phy.pinAssignment()]=4U<<(4*(phy.tc&1));}
  auto cfg=reinterpret_cast<unsigned*>(reinterpret_cast<uint8_t*>(instance.adlnPlatform)+0x20+24*ddi);
  unsigned values[]={ddi,ddi+6,dp?1u:2u,1u,dp?0x400u:0x800u,dp?0x201u:0x202u};memcpy(cfg,values,24);
  memset(portBytes,0,sizeof(portBytes));auto p=new(portBytes)OSObject;
  put(0x548,cfg);put(0x578,c.access);put(0x120,ddi);put(0x590,0x64000+256*ddi);nativeCalls=0;
  // cold; valid GOP; live without retained GOP; omitted route; owner readback failure; invalid record.
  if(fault==1 || fault==2){
   map.regs[0x45404]=0xfc0f;map.regs[0x45454]=3u<<(2*ddi);
   map.regs[0x64000+256*ddi]=0x80000046;map.regs[0x46038+8*(ddi-3)]=0xcc000000;
   map.regs[0x46100+4*ddi]=0x80000000;map.regs[0x46140]=(ddi+3)<<28;
   map.regs[0x60400]=(dp?0x82030006:0x80030100)|((ddi+1)<<27);map.regs[0x70008]=0xc0000000;
   map.regs[0x60540]=0x80040300;map.regs[0x60030]=0x7e100000;map.regs[0x60034]=map.regs[0x60044]=0x800000;map.regs[0x60040]=0x100000;
   if(fault==1){auto &l=c.bridge.ddiLease[ddi];l.owner=&c.bridge.state;l.domains=Display13Power::dependencies(Display13Power::ddiDomain(ddi));c.bridge.ddiRefs[ddi]=1;}
  }
  if(fault==3)instance.display13FBTCHooksReady=false;
  if(fault==4)map.failOwner=true;
  if(fault==5)cfg[4]=1;
  const auto before=map.regs;auto result=XE::wrapDisplay13TCMode(p,alt?3:4);
  if((result==0)!=(fault<=1))fprintf(stderr,"case ddi=%u dp=%u fault=%u phase=%u buffer=%x power=%x aux=%x\n",ddi,dp,fault,unsigned(c.tcMode[ddi-3].phase),map.regs[0x64000+256*ddi],map.regs[0x45404],map.regs[0x45444]);
  assert((result==0)==(fault<=1));assert(nativeCalls==(fault<=1?1u:0u));
  if(result==0){assert(c.tcMode[ddi-3].ownership.active && c.tcMode[ddi-3].auxLease.owner);if(fault==1)assert(map.regs[0x64000+256*ddi]==before.at(0x64000+256*ddi));}
  for(unsigned other=3;other<=6;++other)if(other!=ddi){assert(map.regs[0x64000+256*other]==before.at(0x64000+256*other));assert(!c.tcMode[other-3].controller);}
  assert(!locked);++cases;
 }
 puts("PASS real TC mode entry: HDMI/fixedDP cold and validated GOP, missing route/identity/ownership refusal, mandatory power/ownership/AUX ordering and peer preservation; simulated MMIO");
}
