#include "kern_alderlake_s.hpp"
#include <cassert>
#include "kern_tgl_native_fia.hpp"
#include "kern_xelp_link_requirements.hpp"
#include "kern_xelp_platform.hpp"
#include <thread>
void *current_thread(){thread_local char token;return &token;}
#include <mutex>
using IORecursiveLock=std::recursive_mutex;
void IORecursiveLockLock(IORecursiveLock*p){p->lock();}
void IORecursiveLockUnlock(IORecursiveLock*p){p->unlock();}
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <sys/mman.h>
#include "kern_tgl_port_map.hpp"
#include "kern_tgl_fia.hpp"
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
uintptr_t testCaller=0;
#define __builtin_return_address(n) reinterpret_cast<void *>(testCaller)
#define FunctionCast(name, address) reinterpret_cast<decltype(&name)>(address)
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry s;return &s;}
 template<class... A> void setProperty(A...) {}
};
void tglConnectionRecord(void*,unsigned,bool,uint32_t,uint32_t,void*){}
void tglAUXRecord(void*,unsigned,bool,uint32_t,uint32_t,uint32_t){}
bool coldReady=true;
bool tglDPAltColdReady(void*){return coldReady;}
struct XE {
 XELP::Profile xeProfile=XELP::identify(0x9a498086,1);
 bool tglActive=true,tglDPAltFiaReady=true;
 bool adlsDisplaySelected=false;AlderLakeS::Wiring adlsWiring;alignas(8) uint8_t tglPlatformCopy[0x1c0]{};
 uint32_t tglDPAltCandidates=0;void *tglObservationController=nullptr;
 static void tglRecordStartOrder(unsigned,void*,unsigned=9,uint64_t=0,uint64_t=0,uint64_t=0) {}
 static void tglRecordNativeModeBinding(void *,uint32_t,uint32_t) {}
 TGLNativeFiaContext tglNativeFiaContext; bool tglNativeFiaReady=false; uint32_t tglModularProbed=0;
 uintptr_t tglNativeFiaBase=0,orgTGLPortReadReg=0,orgTGLPortWriteReg=0;
 uint32_t tglNativeFiaReads=0,tglNativeFiaWrites=0;
 bool tglLegacyTCEligible(uint32_t ddi)const{return ddi>=3&&ddi<=6;}
 static bool tglDPAltFia(void *port,TGLFiaPort &fia) {
  auto config=*reinterpret_cast<uint32_t **>(static_cast<uint8_t *>(port)+0x548);
  TGLTypeCPort tc;return coldReady && TGLTypeCPort::resolve(config[0],tc) && tc.resolveFia(0x10,fia);
 }
 static bool tglLegacyFia(void*,TGLFiaPort&);
 static bool tglNativeFiaAccess(void*,uint32_t,uintptr_t,bool,TGLNativeFia&);
 static uint32_t wrapTGLPortReadReg(void*,uint32_t);
 static void wrapTGLPortWriteReg(void*,uint32_t,uint32_t);
 static uint32_t wrapTGLSetPortMode(void*,uint32_t);
 uintptr_t orgTGLSetPortMode {};
 bool automatic=false,registered=false;
 uint32_t tglTCAutoRegistered=0,tglTCAutoCandidates=0x10,tglAUXParentMask=0x18;
 // LEGACY_HPD_POLICY
 bool tglTCModeAllowed(uint32_t ddi,uint32_t mode)const{return !automatic || ddi!=4 || mode==0 || (mode==4&&(registered || (tglTCAutoRegistered&0x10)));}
 IORecursiveLock *tglModeTransitionLock {};uint32_t tglModeSerializedCalls {};
 bool tglModeResultReady {true}; uint32_t tglModeResultCorrections {};
};
XE instance,*callbackXE=&instance;
bool tglQuietTraining(){return true;}
// PRODUCTION_WRAPPER
#undef __builtin_return_address
// LEGACY_FIA_WRAPPER
// FIA_WRAPPERS
alignas(8) uint8_t port[0x600],controller[0x1000],access[0x80];
uint32_t config[8];
unsigned powerGets,powerPuts,sleeps,reads,writes;
uint32_t ready,owned,baseError;
uint32_t ready2=0,owned2=0,topology=0x10;
uint32_t &mode(){return *reinterpret_cast<uint32_t*>(port+0x550);}
uint32_t readReg(void *p,uint32_t r){
 assert(p==port);++reads;if(r==0x1638a0){assert(powerGets>powerPuts);return topology;}assert(r==0x163890 || r==0x163894 || r==0x16e890 || r==0x16e894);
 if(r==0x16e890)return ready2;if(r==0x16e894)return owned2;
 return r==0x163890?ready:owned;
}
void writeReg(void *p,uint32_t r,uint32_t value){assert(p==port && (r==0x163894 || r==0x16e894));++writes;if(r==0x16e894)owned2=value;else owned=value;}
void get(void *p){assert(p==port);++powerGets;}
void put(void *p){assert(p==port);++powerPuts;assert(powerPuts<=powerGets);}
void sleepMock(unsigned n){assert(n==1 || n==100);sleeps+=n;}
void logMock(const char*,...){}
bool parseMock(const char*,void*,int){return true;} // -noAUXDelay, no change to FIA ready wait
using Fn=uint32_t(*)(void*,uint32_t);
Fn native,base;
uint32_t baseCall(void *p,uint32_t m){assert(p==port);const auto r=base(p,m);assert(r==0);return baseError;}
void jump(uint8_t *p,uintptr_t t){p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;}
void callTo(uint8_t *p,unsigned site,unsigned target){assert(p[site]==0xe8);int32_t rel=target-site-5;std::memcpy(p+site+1,&rel,4);}
void reset(unsigned ddi,uint32_t value=0){
 powerGets=powerPuts=sleeps=reads=writes=0;ready=value;owned=0x40000000;mode()=0;baseError=0;
 config[0]=ddi;config[2]=1;config[3]=1;config[5]=8;instance.tglModeResultCorrections=0;instance.tglModeResultReady=true;
}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});
 const size_t size=4*1024*1024;assert(b.size()<size && b.size()>0x138bb0);
 auto m=(uint8_t*)mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED);std::memcpy(m,b.data(),b.size());
 jump(m+0x1fe8e,(uintptr_t)readReg);jump(m+0x1fea8,(uintptr_t)writeReg);
 jump(m+0x1d23e,(uintptr_t)get);jump(m+0x1d27a,(uintptr_t)put);
 jump(m+0x300000,(uintptr_t)logMock);jump(m+0x300020,(uintptr_t)sleepMock);jump(m+0x300040,(uintptr_t)parseMock);
 for(unsigned site:{0x5fd9cu,0x5fe2au,0x5feb2u,0x5ff2bu,0x5ff79u,0x5ffa8u,0x5ffdeu,0x60073u,0x600ffu})callTo(m,site,0x300000);
 callTo(m,0x5ff83,0x300020);callTo(m,0x60109,0x300020);callTo(m,0x600df,0x300040);
 *reinterpret_cast<void**>(m+0x138ba8)=controller;
 *reinterpret_cast<void**>(port+0x548)=config;
 // Exact native base setter via the donor's base vtable, including its
 // timeout reset call. config flag8 skips the unrelated virtual reset.
 assert(m[0x5fe37]==0x48 && m[0x5fe38]==0x8d && m[0x5fe39]==0x05);
 int32_t disp;std::memcpy(&disp,m+0x5fe3a,4);const auto vt=0x5fe3e + disp;
 *reinterpret_cast<uintptr_t*>(m+vt+0x148)=(uintptr_t)baseCall;
 base=(Fn)(m+0x1b4c8);native=(Fn)(m+0x5fcb8);instance.orgTGLSetPortMode=(uintptr_t)native;
 reset(3,1);instance.tglDPAltCandidates=8;coldReady=false;
 assert(XE::wrapTGLSetPortMode(port,3)==kIOReturnNotReady && !reads && !writes && !powerGets);
 coldReady=true;instance.tglDPAltCandidates=0;
 // Qualified ADL-S HPD mode4 must execute native Combo mode1, without FIA.
 for(unsigned ddi:{0u,3u,4u}) {
 reset(ddi);instance.adlsDisplaySelected=true;instance.adlsWiring.valid=true;instance.adlsWiring.dp=1U<<ddi;instance.adlsWiring.pin[ddi]=ddi?ddi+6:1;
 auto bound=reinterpret_cast<uint32_t *>(instance.tglPlatformCopy+0x20+24*ddi);
 const uint32_t fixed[]={ddi,ddi?ddi+6:1,1,0,0x400,0x201};memcpy(bound,fixed,sizeof(fixed));
 *reinterpret_cast<uint32_t **>(port+0x548)=bound;
 instance.tglNativeLegacyHPDCaller=testCaller=0x74a59;
 assert(XE::wrapTGLSetPortMode(port,4)==0 && mode()==1);
 assert(!reads && !writes && !powerGets && !powerPuts && !sleeps);
 }
 instance.adlsDisplaySelected=false;instance.adlsWiring={};testCaller=0;instance.tglNativeLegacyHPDCaller=0;
 *reinterpret_cast<uint32_t **>(port+0x548)=config;
 unsigned cases=0;
 for(bool skipPower:{false,true})for(unsigned d=3;d<=8;++d)for(unsigned requested:{3u,4u})for(bool isReady:{false,true}){
  controller[0xc58]=skipPower?2:0;const unsigned powerCalls=skipPower?0:1;
  const auto sampled=isReady?(1u<<(d-3)):0u;
  reset(d,sampled);const auto raw=native(port,requested);const auto rawMode=mode();
  // Donor switches on only TC1..4 when checking ready; TC5/6 cannot pass.
  const bool success=isReady && d<=6;
  assert(raw==0 && rawMode==(success?requested:0u));
  assert(powerGets==powerCalls && powerPuts==(success?0u:powerCalls));assert(sleeps==(success?0u:100u));
  reset(d,sampled);const auto fixed=XE::wrapTGLSetPortMode(port,requested);
  assert(fixed==(success?0u:kIOReturnNotReady) && mode()==rawMode);
  assert(instance.tglModeResultCorrections==(success?0u:1u));
  assert(powerGets==powerCalls && powerPuts==(success?0u:powerCalls) && sleeps==(success?0u:100u));
  if(success){assert(owned==(0x40000000u|(1u<<(d-3))));assert(XE::wrapTGLSetPortMode(port,0)==0 && !mode());assert(powerGets==powerPuts && owned==0x40000000);}
  ++cases;
 }
 controller[0xc58]=0;
 // The automatic path must reject unsupported mode entry before the native
 // writer acquires AUX or modifies ownership; cleanup mode0 is always allowed.
 instance.automatic=true;reset(4,2);
 for(unsigned m:{2u,3u,4u})assert(XE::wrapTGLSetPortMode(port,m)==kIOReturnNotReady);
 assert(!powerGets&&!powerPuts&&!writes&&!reads);
 instance.registered=true;assert(XE::wrapTGLSetPortMode(port,4)==0&&mode()==4);
 assert(XE::wrapTGLSetPortMode(port,3)==kIOReturnNotReady&&mode()==4);
 assert(XE::wrapTGLSetPortMode(port,0)==0&&powerGets==powerPuts);
 // First runtime HPD reaches the native setter without a startup probe.
 instance.registered=false;instance.tglNativeLegacyHPDCaller=0x100;
 reset(4,2);testCaller=0x101;
 assert(XE::wrapTGLSetPortMode(port,4)==kIOReturnNotReady&&!powerGets);
 testCaller=0x100;
 assert(XE::wrapTGLSetPortMode(port,4)==0&&mode()==4&&instance.tglTCAutoRegistered==0x10);
 assert(powerGets==1&&powerPuts==0);
 assert(XE::wrapTGLSetPortMode(port,0)==0&&powerGets==powerPuts);
 assert(instance.tglTCAutoRegistered==0x10);testCaller=0;
 instance.automatic=false;
 // Native HAL does not treat repeated mode4 as a no-op. This is why boot
 // handoff and subsequent setters must share the durable mode slot.
 reset(4,2);assert(native(port,4)==0 && native(port,4)==0 && mode()==4);
 assert(powerGets==2 && powerPuts==0);
 assert(native(port,0)==0 && powerGets==2 && powerPuts==1);
 puts("PASS native counterexample: mode4 twice acquires AUX twice; one mode0 releases once");
 // The repair does not replace an existing native error, touch an
 // unqualified route, or apply to non-TC and unrelated modes.
 reset(4);baseError=0xe00002bc;assert(XE::wrapTGLSetPortMode(port,4)==baseError && !instance.tglModeResultCorrections);
 reset(4);instance.tglModeResultReady=false;assert(XE::wrapTGLSetPortMode(port,4)==0 && mode()==0 && !instance.tglModeResultCorrections);
 reset(1);assert(XE::wrapTGLSetPortMode(port,1)==0 && mode()==1 && !instance.tglModeResultCorrections);
 reset(4);assert(XE::wrapTGLSetPortMode(port,2)==0 && mode()==2 && powerGets==1 && !instance.tglModeResultCorrections);
 // Modular FIA TC3 has its ready bit in FIA2; the native code reads FIA1
 // and returns success after timeout. The wrapper reports failure; it does
 // not claim to fix the bank/mask or power lifetime yet.
 TGLTypeCPort p;TGLFiaPort fia;assert(TGLTypeCPort::resolve(5,p) && p.resolveFia(0x10,fia));
 assert(fia.ready==0x16e890 && fia.readyMask==1);
 reset(5,0);assert(XE::wrapTGLSetPortMode(port,4)==kIOReturnNotReady && reads==100 && !writes);
 // All-ones handling remains a separate missing hardware validity check.
 reset(4,~0u);assert(XE::wrapTGLSetPortMode(port,4)==0 && mode()==4);
 // Compare the new shared primitive to the actual native TC2 outcome,
 // then exercise the modular FIA2 case the native body cannot address.
 struct FiaIO {
  uint32_t bank {},ready {},ownership[3] {0x40000000,0x40000000,0x40000000};bool locked=false;
  bool hasColdReference(uint32_t){return true;}bool readable(uint32_t){return true;}
  uint32_t read(uint32_t reg){
   if(reg==0x1638a0)return 0x10;
   const uint32_t bases[]={0x163000,0x16e000,0x16f000};
   for(unsigned i=0;i<3;++i){if(reg==bases[i]+0x8a0)return 0;if(reg==bases[i]+0x890)return i==bank?ready:0;if(reg==bases[i]+0x894){assert(locked);return ownership[i];}}
   assert(false);return 0;
  }
  void write(uint32_t reg,uint32_t value){const uint32_t bases[]={0x163000,0x16e000,0x16f000};assert(locked && reg==bases[bank]+0x894);ownership[bank]=value;}
  void fiaLock(unsigned i){assert(i==bank && !locked);locked=true;}
  void fiaUnlock(unsigned i){assert(i==bank && locked);locked=false;}
  void sleepMs(unsigned){assert(false);}
 } mapped;
 reset(4,2);assert(native(port,4)==0);const auto nativeOwned=owned;
 mapped.ready=2;assert(TGLFia::waitLegacyReady(mapped,4).ok());
 assert(TGLFia::setOwned(mapped,4,true).ok() && mapped.ownership[0]==nativeOwned);
 assert(native(port,0)==0 && TGLFia::setOwned(mapped,4,false).ok() && mapped.ownership[0]==owned);
 mapped.bank=1;mapped.ready=1;
 assert(TGLFia::waitLegacyReady(mapped,5).ok() && TGLFia::setOwned(mapped,5,true).ok());
 assert(mapped.ownership[0]==0x40000000 && mapped.ownership[1]==0x40000001 && mapped.ownership[2]==0x40000000);
 *reinterpret_cast<uint8_t **>(port+0x578)=access;
 *reinterpret_cast<uint8_t **>(access+0x50)=controller;
 *reinterpret_cast<uint32_t *>(access+0x60)=0x170000;
 // Replay the coordinate adapter through the actual native call sites.
 IORecursiveLock lock;instance.tglModeTransitionLock=&lock;instance.tglNativeFiaReady=true;
 instance.tglNativeFiaBase=reinterpret_cast<uintptr_t>(m);instance.tglModularProbed=0x78;
 instance.orgTGLPortReadReg=reinterpret_cast<uintptr_t>(readReg);
 instance.orgTGLPortWriteReg=reinterpret_cast<uintptr_t>(writeReg);
 jump(m+0x1fe8e,reinterpret_cast<uintptr_t>(XE::wrapTGLPortReadReg));
 jump(m+0x1fea8,reinterpret_cast<uintptr_t>(XE::wrapTGLPortWriteReg));
 for(unsigned requested:{3U,4U})for(unsigned d=3;d<=6;++d){
  instance.tglDPAltCandidates=requested==3?0x78:0;
  const auto bit=1u<<((d-3)%2);reset(d,d<5?bit:0);ready2=d>=5?bit:0;
  owned2=0x8000000e;const auto before=owned2;
  assert(XE::wrapTGLSetPortMode(port,requested)==0&&mode()==requested&&powerGets==1&&powerPuts==0);
  assert(d<5?owned==(0x40000000u|bit):owned==0x40000000u);
  if(d>=5)assert(owned2==(before|bit));
  assert(XE::wrapTGLSetPortMode(port,0)==0&&mode()==0&&powerGets==powerPuts);
  assert(owned==0x40000000u);
  if(d>=5)assert(owned2==((before|bit)&~bit));
  assert(!instance.tglNativeFiaContext.owner);
 }
 // A direct register call, wrong caller/thread/port and nested unrelated
 // scope must never inherit another transaction's FIA mapping.
 TGLNativeFia mapping;
 { TGLNativeFiaScope outer(instance.tglNativeFiaContext,reinterpret_cast<uintptr_t>(current_thread()),port,5,true);
   assert(XE::tglNativeFiaAccess(port,0x163890,reinterpret_cast<uintptr_t>(m)+0x5feea,false,mapping));
   assert(!XE::tglNativeFiaAccess(controller,0x163890,reinterpret_cast<uintptr_t>(m)+0x5feea,false,mapping));
   assert(!XE::tglNativeFiaAccess(port,0x163890,reinterpret_cast<uintptr_t>(m)+0x5feeb,false,mapping));
   assert(!instance.tglNativeFiaContext.matches(1,port));
   ready=0x1234;assert(XE::wrapTGLPortReadReg(port,0x163890)==ready);
   {TGLNativeFiaScope nested(instance.tglNativeFiaContext,reinterpret_cast<uintptr_t>(current_thread()),nullptr,5,true);
    assert(!instance.tglNativeFiaContext.matches(reinterpret_cast<uintptr_t>(current_thread()),port));}
   assert(instance.tglNativeFiaContext.matches(reinterpret_cast<uintptr_t>(current_thread()),port));
 }
 assert(!instance.tglNativeFiaContext.owner && instance.tglNativeFiaReads && instance.tglNativeFiaWrites);
 // Hotplug needs no previous powered probe or DP-alt bootstrap. Native
 // acquisition must precede topology reads; the read mock enforces AUX debt.
 instance.tglDPAltCandidates=0;instance.tglModularProbed=0;coldReady=false;reset(5,0);ready2=1;
 assert(XE::wrapTGLSetPortMode(port,4)==0 && mode()==4 && owned2&1);
 assert(XE::wrapTGLSetPortMode(port,0)==0 && powerGets==powerPuts);coldReady=true;
 for(unsigned d=3;d<=6;++d) {
  topology=0;reset(d,1U<<(d-3));
  assert(XE::wrapTGLSetPortMode(port,4)==0 && (owned&(1U<<(d-3))));
  assert(XE::wrapTGLSetPortMode(port,0)==0 && owned==0x40000000 && powerGets==powerPuts);
 }
 topology=UINT32_MAX;reset(5,0);ready2=1;
 assert(XE::wrapTGLSetPortMode(port,4)==kIOReturnNotReady && !writes && powerGets==powerPuts);
 topology=16;reset(5,0);ready2=UINT32_MAX;
 assert(XE::wrapTGLSetPortMode(port,4)==kIOReturnNotReady && !writes && powerGets==powerPuts);
 // Even when native returns zero after an unavailable ownership access,
 // the wrapper reports failure and never overwrites the peer owner word.
 topology=16;reset(5,0);ready2=1;owned2=UINT32_MAX;
 assert(XE::wrapTGLSetPortMode(port,4)==kIOReturnNotReady && !writes && owned2==UINT32_MAX);
 // Missing mutex keeps this unqualified path outside the adapter.
 instance.tglModularProbed=0x78;instance.tglModeTransitionLock=nullptr;reset(5,0);
 assert(XE::wrapTGLSetPortMode(port,4)==kIOReturnNotReady && reads==100 && !writes);
 puts("PASS qualified native FIA adapter: TC1/2 identity and TC3/4 modular bank/bit, sibling preservation, paired AUX, scoped caller guards");
 munmap(m,size);
 printf("PASS: %u real native mode transitions plus error/guard/non-TC controls; timeout returns 0 with mode0 before repair, NotReady after; native power/read/write/wait sequence retained\n",cases);
 std::puts("Mapped primitive matches actual native TC2 ownership result and handles FIA2 without changing FIA1; confirmed remaining native FIA1/TC1..4-ready limitation and all-ones acceptance; CPU dependency mocks, no hardware qualification or new TC-cold requests.");
}
