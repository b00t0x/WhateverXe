#include "kern_alderlake_s.hpp"
#include <cassert>
#include "kern_tgl_native_fia.hpp"
#include "kern_xelp_link_requirements.hpp"
#include "kern_xelp_platform.hpp"
#include <thread>
void *current_thread(){thread_local char token;return &token;}
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <sys/mman.h>
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
using IORecursiveLock=std::recursive_mutex;
thread_local unsigned worker=2;
std::atomic<bool> firstRead{false},resumeFirst{false},secondAttempt{false},secondDone{false};
void await(std::atomic<bool>&flag){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!flag.load()){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}}
void IORecursiveLockLock(IORecursiveLock*p){if(worker==1)secondAttempt=true;p->lock();}
void IORecursiveLockUnlock(IORecursiveLock*p){p->unlock();}
#define FunctionCast(name,address) reinterpret_cast<decltype(&name)>(address)
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry s;return &s;}template<class...A>void setProperty(A...){};};
void tglConnectionRecord(void*,unsigned,bool,uint32_t,uint32_t,void*){}
void tglAUXRecord(void*,unsigned,bool,uint32_t,uint32_t,uint32_t){}
bool coldReady=true;
bool tglDPAltColdReady(void*){return coldReady;}
struct XE {
 XELP::Profile xeProfile=XELP::identify(0x9a498086,1);
 bool adlsDisplaySelected=false;AlderLakeS::Wiring adlsWiring;alignas(8) uint8_t tglPlatformCopy[0x1c0]{};
 uint32_t tglDPAltCandidates=0;void *tglObservationController=nullptr;
 uint32_t tglTCAutoRegistered=0,tglTCAutoCandidates=0x10,tglAUXParentMask=0x18;
 // LEGACY_HPD_POLICY
 bool tglTCModeAllowed(uint32_t,uint32_t)const{return true;}
 static void tglRecordStartOrder(unsigned,void*,unsigned=9,uint64_t=0,uint64_t=0,uint64_t=0){}
 static void tglRecordNativeModeBinding(void *,uint32_t,uint32_t) {}
 TGLNativeFiaContext tglNativeFiaContext; bool tglNativeFiaReady=false; uint32_t tglModularProbed=0;
 uintptr_t tglNativeFiaBase=0,orgTGLPortReadReg=0,orgTGLPortWriteReg=0;
 uint32_t tglNativeFiaReads=0,tglNativeFiaWrites=0;
 bool tglLegacyTCEligible(uint32_t ddi)const{return ddi>=3&&ddi<=6;}
 static bool tglDPAltFia(void *port,TGLFiaPort &fia) {
  auto config=*reinterpret_cast<uint32_t **>(static_cast<uint8_t *>(port)+0x548);
  TGLTypeCPort tc;return coldReady && TGLTypeCPort::resolve(config[0],tc) && tc.resolveFia(0x10,fia);
 }
 static bool tglLegacyFia(void *port,TGLFiaPort &fia) {return tglDPAltFia(port,fia);}
 static bool tglNativeFiaAccess(void*,uint32_t,uintptr_t,bool,TGLNativeFia&);
 static uint32_t wrapTGLPortReadReg(void*,uint32_t);
 static void wrapTGLPortWriteReg(void*,uint32_t,uint32_t);
 static uint32_t wrapTGLSetPortMode(void*,uint32_t);
 uintptr_t orgTGLSetPortMode{};IORecursiveLock *tglModeTransitionLock{};
 bool tglModeResultReady=true;uint32_t tglModeResultCorrections{},tglModeSerializedCalls{};
};
XE instance,*callbackXE=&instance;
// PRODUCTION_WRAPPER
alignas(8)uint8_t ports[2][0x600],controller[0x1000];uint32_t configs[2][8];
std::atomic<uint32_t> owned{0};std::atomic<unsigned> auxGets[2],auxPuts[2],baseCalls[2];bool interleave;
unsigned index(void*p){assert(p==ports[0]||p==ports[1]);return p==ports[0]?0:1;}
uint32_t &mode(unsigned i){return *reinterpret_cast<uint32_t*>(ports[i]+0x550);}
uint32_t readReg(void*p,uint32_t r){
 auto i=index(p);assert(r==0x163890||r==0x163894);if(r==0x163890)return 3;
 const auto snapshot=owned.load();
 if(interleave&&i==0){firstRead=true;await(resumeFirst);}
 return snapshot;
}
void writeReg(void*p,uint32_t r,uint32_t v){index(p);assert(r==0x163894);owned=v;}
void get(void*p){++auxGets[index(p)];}
void put(void*p){auto i=index(p);++auxPuts[i];assert(auxPuts[i]<=auxGets[i]);}
void sleepMock(unsigned n){assert(n==1||n==100);}
void logMock(const char*,...){}
bool parseMock(const char*,void*,int){return true;}
using Fn=uint32_t(*)(void*,uint32_t);Fn native,base;
uint32_t baseCall(void*p,uint32_t m){++baseCalls[index(p)];return base(p,m);}
void jump(uint8_t*p,uintptr_t t){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;}
void callTo(uint8_t*p,unsigned site,unsigned target){assert(p[site]==0xe8);int32_t rel=target-site-5;memcpy(p+site+1,&rel,4);}
void reset(){
 firstRead=false;resumeFirst=false;secondAttempt=false;secondDone=false;owned=0x40000000;interleave=true;
 instance.tglModeSerializedCalls=0;
 for(unsigned i=0;i<2;++i){mode(i)=0;auxGets[i]=auxPuts[i]=baseCalls[i]=0;configs[i][0]=i+3;configs[i][5]=8;}
}
int main(int argc,char**argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});
 const size_t size=4*1024*1024;assert(b.size()<size);
 auto m=(uint8_t*)mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED);memcpy(m,b.data(),b.size());
 jump(m+0x1fe8e,(uintptr_t)readReg);jump(m+0x1fea8,(uintptr_t)writeReg);
 jump(m+0x1d23e,(uintptr_t)get);jump(m+0x1d27a,(uintptr_t)put);
 jump(m+0x300000,(uintptr_t)logMock);jump(m+0x300020,(uintptr_t)sleepMock);jump(m+0x300040,(uintptr_t)parseMock);
 for(unsigned site:{0x5fd9cu,0x5fe2au,0x5feb2u,0x5ff2bu,0x5ff79u,0x5ffa8u,0x5ffdeu,0x60073u,0x600ffu})callTo(m,site,0x300000);
 callTo(m,0x5ff83,0x300020);callTo(m,0x60109,0x300020);callTo(m,0x600df,0x300040);
 *reinterpret_cast<void**>(m+0x138ba8)=controller;
 for(unsigned i=0;i<2;++i)*reinterpret_cast<void**>(ports[i]+0x548)=configs[i];
 int32_t disp;memcpy(&disp,m+0x5fe3a,4);const auto vt=0x5fe3e + disp;
 *reinterpret_cast<uintptr_t*>(m+vt+0x148)=(uintptr_t)baseCall;
 base=(Fn)(m+0x1b4c8);native=(Fn)(m+0x5fcb8);instance.orgTGLSetPortMode=(uintptr_t)native;
 IORecursiveLock lock;
 for(bool serial:{false,true}){
  reset();instance.tglModeTransitionLock=serial?&lock:nullptr;
  std::thread a([]{worker=0;assert(XE::wrapTGLSetPortMode(ports[0],4)==0);});
  await(firstRead);
  std::thread b([]{worker=1;assert(XE::wrapTGLSetPortMode(ports[1],4)==0);secondDone=true;});
  if(serial){await(secondAttempt);assert(!secondDone&&baseCalls[1]==0&&auxGets[1]==0);}
  else {await(secondDone);assert(owned==0x40000002);}
  resumeFirst=true;a.join();b.join();interleave=false;
  assert(mode(0)==4&&mode(1)==4&&auxGets[0]==1&&auxGets[1]==1);
  assert(owned==(serial?0x40000003u:0x40000001u));
  assert(instance.tglModeSerializedCalls==(serial?2u:0u));
  // Drain only after both original calls complete; the serialization does
  // not change original AUX ownership or native mode0 behavior.
  assert(XE::wrapTGLSetPortMode(ports[0],0)==0&&XE::wrapTGLSetPortMode(ports[1],0)==0);
  assert(owned==0x40000000&&mode(0)==0&&mode(1)==0&&auxGets[0]==auxPuts[0]&&auxGets[1]==auxPuts[1]);
  printf("%s: real native concurrent TC1/TC2 calls %s shared ownership; base/AUX entry exclusion and paired releases verified\n",serial?"serialized":"unserialized",serial?"preserve":"lose");
 }
 // Unqualified/null lock/non-TC remain on their prior native path.
 instance.tglModeTransitionLock=&lock;instance.tglModeResultReady=false;unsigned count=instance.tglModeSerializedCalls;
 assert(XE::wrapTGLSetPortMode(ports[0],4)==0&&instance.tglModeSerializedCalls==count);
 assert(XE::wrapTGLSetPortMode(ports[0],0)==0);
 instance.tglModeResultReady=true;configs[0][0]=1;
 assert(XE::wrapTGLSetPortMode(ports[0],1)==0&&instance.tglModeSerializedCalls==count);
 munmap(m,size);
 puts("PASS actual mode wrapper + qualified donor concurrency. Scheduler deliberately interleaved; current hardware/caller concurrency is NOT established. Not a drain/idle or full Mode lifetime proof.");
}
