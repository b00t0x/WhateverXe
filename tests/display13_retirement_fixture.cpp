#include "kern_display13_fb_memory.hpp"
#include "kern_display13_retirement.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
using IORecursiveLock=int;using IOLock=int;
static int controllerMutex,fbMutex,ownerMutex;static unsigned ctlDepth,fbDepth,ownerDepth;
void IORecursiveLockLock(int *m){if(m==&controllerMutex){assert(ctlDepth||!fbDepth);++ctlDepth;}else{assert(ctlDepth);++fbDepth;}}
void IORecursiveLockUnlock(int *m){auto &depth=m==&controllerMutex?ctlDepth:fbDepth;assert(depth);--depth;}
void IOLockLock(int*){assert(!ownerDepth);++ownerDepth;}void IOLockUnlock(int*){assert(ownerDepth);--ownerDepth;}
struct OSObject {void **table;unsigned size,refs;OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}
 void retain(){++refs;}void release(){assert(refs);--refs;}};
using IOService=OSObject;
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 template<class... T>void setProperty(T...) {}};
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
#define OSSafeReleaseNULL(p) do{if(p){p->release();p=nullptr;}}while(0)
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
namespace Display13FBTopology {bool multiOutputWiring(unsigned w){return w!=0;}}
namespace TGLFBBinding {enum class Status {Unavailable,Complete};struct Readout {Status status{};unsigned pipe{},ddi{};};}
namespace Display13WM {enum class Status {Unavailable,Complete};}
namespace Display13DBUF {
 enum class Status {Complete,Conflict};struct State {bool faulted{};unsigned selected{},consumers{},unretired{};};
 static bool refuseDetach;static unsigned detachCalls;
 template<class IO>Status detach(IO&,State&,unsigned){++detachCalls;return refuseDetach?Status::Conflict:Status::Complete;}
}
struct Pipe {IOService *memoryFramebuffer{};uintptr_t publishedScanout{};bool destroyingPath{};
 int powerPath{},memory{},scanout{};uint64_t watermarkClockHz{};unsigned wmPending{};};
struct Power {int *lock{};IOService *controller{};void *map{};bool memoryFaulted{};
 struct Bridge {unsigned forgotten{};void forgetBootPath(uintptr_t){++forgotten;}}bridge;
 Pipe pipe[3];Display13DBUF::State dbuf;}display13FBPower;
struct {struct Slot{IOService *framebuffer{};void *buffer{};}slot[3];}display13FBSysMem;
struct {unsigned admittedPipes{};}display13FBIRQ;
struct Display13FBDBUFIO {void *map;};
void adlnFault(bool &fault,unsigned){fault=true;}
void adlnPublishMemory(const char*,unsigned,void*,size_t){}
struct XE {
 bool display13FramebufferRequested=true;struct Wiring{unsigned value=0x78,dsi=0;operator unsigned()const{return value;}Wiring &operator=(unsigned v){value=v;return *this;}}adlnOutputWiring;void *adlnPlatform{};
 uintptr_t orgAlderLakeNFreePath{},orgDisplay13FreeResources{},orgDisplay13AllocateResources{};
 bool disposeDSIPath(IOService*,OSObject*);
 static bool adlnFreeStoppedPipe(IOService*);
 static void wrapAlderLakeNFreePath(IOService*,OSObject*);
 static void drainDisplay13TCMode(unsigned ddi) {
  assert(ddi==3 && ctlDepth && !ownerDepth);
  assert(!display13FBPower.pipe[0].memoryFramebuffer && !display13FBPower.pipe[0].publishedScanout);
 }
 static uint32_t releaseDisplay13Path(IOService*,OSObject*),retireDisplay13Paths(void*),wrapDisplay13FreeResources(void*);
 static bool display13RetirementHealthy(),wrapDisplay13AllocateResources(void*),retireDisplay13Controller(void*);
}state,*callbackXE=&state;
template<class T>void put(void*p,unsigned off,T value){memcpy(static_cast<uint8_t*>(p)+off,&value,sizeof(value));}
TGLFBBinding::Readout adlnInspectPathBinding(IOService*f,OSObject*p,void*,unsigned){
 const auto pipe=Display13FBMemory::boundPipe(display13FBPower.pipe,f);
 if(!p||Display13FBMemory::field<void*>(p,0x32b0)!=f)return {};
 return {TGLFBBinding::Status::Complete,pipe,3};
}
bool dsiLifecycleTarget(IOService*,OSObject*){return true;}
// FUNCTIONS
alignas(16) static uint8_t ctl[0x1c20],fb[0x4b90],peer[0x4b90],path[0x3730];
static unsigned failure,memoryCalls,pathCalls,postCalls,transactionStops,allocCalls;
static uintptr_t backing=0x1234000;
static unsigned (*agdc)(void*,void*);
static void *frame=fb;
bool XE::adlnFreeStoppedPipe(IOService*f){
 // Substitute the native memory/IRQ leaf, not releaseDisplay13Path. Production
 // code must check its return AND actual slot/backing state before disposal.
 ++memoryCalls;if(failure==1)return false;
 if(failure==2)return true; // false success: outstanding backing must reject it
 put(f,0x4288,uint8_t(0xff));put(f,0x4348,uintptr_t(0));
 display13FBPower.pipe[0].publishedScanout=0;return true;
}
void nativePath(IOService*f,OSObject*p){
 assert(ctlDepth&&fbDepth&&!ownerDepth);++pathCalls;
 assert(!Display13FBMemory::field<uintptr_t>(f,0x4348));
 if(failure==4)return; // no progress must terminate the enclosing native loop
 put(f,0x49e8,0U);put(f,0x49f0,static_cast<void*>(nullptr));
 put(p,0x3644,0U);put(p,0x32b0,static_cast<void*>(nullptr));put(p,0x3648,static_cast<void*>(nullptr));
}
unsigned dsiDisposals=0;bool dsiRefuse=false;
bool XE::disposeDSIPath(IOService *f,OSObject *p){
 assert(ctlDepth && fbDepth && !ownerDepth);++dsiDisposals;
 if(dsiRefuse)return false;
 put(f,0x49e8,0U);put(f,0x49f0,uintptr_t(0));put(p,0x3644,0U);put(p,0x32b0,uintptr_t(0));put(p,0x3648,uintptr_t(0));
 return true;
}
void transactions(void*){assert(ctlDepth&&!fbDepth);++transactionStops;}
void disable(void*f,bool){assert(ctlDepth&&!fbDepth);put(path,0x3644,failure==5?2U:1U);}
void post(void*){assert(ctlDepth&&!fbDepth);++postCalls;}
void zero(void*p,size_t n){memset(p,0,n);}
void property(void*,const char*,const void*,unsigned){++postCalls;}
void syntheticFree(void*f){transactions(f);disable(f,false);if(XE::retireDisplay13Paths(f))return;
 post(f);post(f);post(f);post(f);put(f,0x1e7,uint8_t(0));}
unsigned syntheticAGDC(void*,void*){return XE::wrapDisplay13FreeResources(frame);}
bool allocate(void*){++allocCalls;return true;}
void hook(uint8_t*p,uintptr_t target){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&target,8);p[10]=0xff;p[11]=0xe0;}
void call(uint8_t*m,unsigned at,unsigned target){assert(m[at]==0xe8);const int32_t d=target-(at+5);memcpy(m+at+1,&d,4);}
void setup(uintptr_t native){
 memset(ctl,0,sizeof(ctl));memset(fb,0,sizeof(fb));memset(peer,0,sizeof(peer));memset(path,0,sizeof(path));
 static void *table[82]{};table[0x288/8]=(void*)property;
 for(auto p:{ctl,fb,peer,path}){auto o=reinterpret_cast<OSObject*>(p);o->table=table;o->size=p==ctl?sizeof(ctl):p==path?sizeof(path):sizeof(fb);o->refs=1;}
 put(ctl,0x9b8,&controllerMutex);put(ctl,0xd5c,1U);put(ctl,0xd60,frame);
 put(fb,0x1d0,static_cast<void*>(ctl));put(fb,0x4220,&fbMutex);put(fb,0x3fd8,int32_t(-1));
 put(fb,0x1e7,uint8_t(1));put(fb,0x49e8,failure==6?3U:1U);put(fb,0x49f0,static_cast<void*>(path));
 put(fb,0x4288,uint8_t(0xff));put(fb,0x4348,backing);
 put(path,0x32b0,frame);put(path,0x3648,static_cast<void*>(peer));put(path,0x3644,2U);
 display13FBPower={};auto &c=display13FBPower;c.lock=&ownerMutex;c.controller=reinterpret_cast<IOService*>(ctl);
 c.pipe[0].memoryFramebuffer=reinterpret_cast<IOService*>(fb);c.pipe[0].memoryFramebuffer->retain();c.pipe[0].publishedScanout=backing;
 c.pipe[1].memoryFramebuffer=reinterpret_cast<IOService*>(peer);c.pipe[1].publishedScanout=0x5678000;c.pipe[1].wmPending=7;
 display13FBSysMem={};display13FBIRQ.admittedPipes=3;
 Display13DBUF::refuseDetach=failure==3;Display13DBUF::detachCalls=0;
 state={};state.orgAlderLakeNFreePath=(uintptr_t)nativePath;state.orgDisplay13FreeResources=native;
 state.orgDisplay13AllocateResources=(uintptr_t)allocate;
 memoryCalls=pathCalls=postCalls=transactionStops=allocCalls=0;
}
int main(int argc,char**argv){
 uint8_t *image=nullptr;size_t bytes=0;uintptr_t native=(uintptr_t)syntheticFree;agdc=syntheticAGDC;
 if(argc==2){
  std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(file)),{});
  const auto cave=unsigned((b.size()+4095)&~4095ULL);bytes=cave+4096;
  image=(uint8_t*)mmap(nullptr,bytes,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);assert(image!=MAP_FAILED);memcpy(image,b.data(),b.size());
  using namespace Display13Retirement;
  assert(!memcmp(image+Loop,Before,sizeof(Before)));assert(!memcmp(image+ReleaseCall,ReleaseBefore,sizeof(ReleaseBefore)));
  uint8_t patched[sizeof(Before)];patch(patched,(uintptr_t)XE::retireDisplay13Paths);memcpy(image+Loop,patched,sizeof(patched));
  memcpy(image+ReleaseCall,ReleaseAfter,sizeof(ReleaseAfter));
  // NATIVE_HOOKS
  hook(image+cave,(uintptr_t)XE::wrapDisplay13FreeResources);call(image,ReleaseCall,cave);
  hook(image+cave+32,(uintptr_t)zero);call(image,0x67f5e,cave+32);
  native=(uintptr_t)(image+0x67e8e);agdc=(decltype(agdc))(image+0x6ba08);
 }
 for(failure=0;failure<7;++failure){
  setup(native);const auto peerBefore=display13FBPower.pipe[1];unsigned handle=1;
  auto result=agdc(ctl,&handle);assert(result==(failure?kIOReturnNotReady:0U));
  assert(!ctlDepth&&!fbDepth&&!ownerDepth&&transactionStops==1);
  assert(display13FBPower.pipe[1].memoryFramebuffer==peerBefore.memoryFramebuffer&&display13FBPower.pipe[1].publishedScanout==peerBefore.publishedScanout&&display13FBPower.pipe[1].wmPending==7);
  if(failure){
   assert(!postCalls&&Display13FBMemory::field<unsigned>(fb,0x49e8)==(failure==6?3U:1U));
   assert(Display13FBMemory::field<void*>(path,0x32b0)==fb&&Display13FBMemory::field<void*>(path,0x3648)==peer);
   assert(!XE::wrapDisplay13AllocateResources(fb)&&!allocCalls);
   assert(!XE::retireDisplay13Controller(ctl));
   if(failure<=2)assert(Display13FBMemory::field<uintptr_t>(fb,0x4348)==backing&&!pathCalls);
   if(failure==3)assert(!Display13FBMemory::field<uintptr_t>(fb,0x4348)&&!pathCalls); // partial backing release
  }else{
   assert(postCalls==4&&pathCalls==1&&!Display13FBMemory::field<unsigned>(fb,0x49e8));
   assert(!display13FBPower.pipe[0].memoryFramebuffer&&!(display13FBIRQ.admittedPipes&1));
   assert(XE::wrapDisplay13AllocateResources(fb)&&allocCalls==1);
  }
 }
 failure=0;setup(native);
 // Unretired peer ownership prevents pool destruction even after this path
 // completes. Clear only that synthetic peer for the empty-controller case.
 assert(!XE::retireDisplay13Controller(ctl));assert(pathCalls==1);
 display13FBPower.pipe[1]={};assert(XE::retireDisplay13Controller(ctl));assert(pathCalls==1);
 // DSI selected at the real release boundary must never fall through to the
 // normal audio/DDI cleanup body, including when its dedicated disposal fails.
 setup(native);state.adlnOutputWiring.dsi=1;dsiRefuse=true;dsiDisposals=0;
 assert(XE::wrapDisplay13FreeResources(fb)==kIOReturnNotReady && dsiDisposals==1 && !pathCalls && !postCalls);
 assert(Display13FBMemory::field<void*>(path,0x32b0)==fb && display13FBPower.pipe[0].memoryFramebuffer==reinterpret_cast<IOService*>(fb));
 setup(native);state.adlnOutputWiring.dsi=1;dsiRefuse=false;dsiDisposals=0;
 assert(!XE::wrapDisplay13FreeResources(fb) && dsiDisposals==1 && !pathCalls && postCalls==4);
 assert(!display13FBPower.pipe[0].memoryFramebuffer);
 setup(native);state.display13FramebufferRequested=false;display13FBPower.memoryFaulted=true;
 assert(XE::display13RetirementHealthy()&&XE::retireDisplay13Controller(nullptr)); // other backends unchanged
 if(image)munmap(image,bytes);
 puts("PASS D13 production path/loop/return: memory refusal, false memory success, DDB refusal after partial release, no progress, active path, excess count, AGDC error, allocation/pool blocking, peer preservation and success");
}
