#include "kern_alderlake_s.hpp"
#include "kern_framebuffer_donor.hpp"
template<class... T>void tglTransitionRecord(T...){ }
// Native boundaries are mocks; LEASE_FUNCTIONS is extracted unchanged from production.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>
#include <map>
#include <functional>
#include <sys/mman.h>
#include "kern_tgl_training_scope.hpp"
#include "kern_tgl_tc_boot.hpp"
using IORecursiveLock = std::recursive_mutex;
void IORecursiveLockLock(IORecursiveLock *l) { l->lock(); }
void IORecursiveLockUnlock(IORecursiveLock *l) { l->unlock(); }
void IOSleep(unsigned) { std::this_thread::yield(); }
thread_local char threadIdentity;
void *current_thread() { return &threadIdentity; }
std::map<void *, int> retained;
std::mutex refLock;
struct OSObject {
 void retain() { std::lock_guard<std::mutex> lock(refLock); ++retained[this]; }
 void release() { std::lock_guard<std::mutex> lock(refLock); assert(retained[this] > 0); --retained[this]; }
};
struct IOService {};
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot() { static IORegistryEntry root; return &root; }
 template<class... T> void setProperty(T...) {}
};
void clock_get_uptime(uint64_t *p) { *p = 0; }
void absolutetime_to_nanoseconds(uint64_t t, uint64_t *p) { *p = t; }
#define SYSLOG(...) ((void)0)
#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
constexpr uint32_t kIOReturnBusy = 0xe00002d5;
template<class... T> void tglTrace(T...) {}
template<class... T> void tglRecordStartOrder(T...) {}
template<class... T> void tglObservePath(T...) {}
template<class... T> void tglObserveActivation(T...) {}
template<class... T> void tglObserveResource(T...) {}
void tglAUXPublish() {}
void tglDPCDPublish() {}
struct XE {
 static bool display13RetirementHealthy(){return true;}
 static bool retireDisplay13Controller(void*){return true;}
 bool adlsDisplaySelected=false;AlderLakeS::Wiring adlsWiring;
 // TC_WIRING_FIELDS
 // LEASE_FIELDS
 IORecursiveLock *tglPathPoolLock;
 bool tglTrainingLeaseReady = true;
 bool framebufferStartInProgress=false;uint32_t framebufferProtocolCalls=0;void *framebufferStartedController=nullptr;
 void *tglObservationController;
 uintptr_t tglPortByDDI, orgTGLPathLookup, orgTGLAllocatePathAny, orgTGLAllocatePathFixed, orgTGLFreePath;
 uintptr_t tglEnablePG, tglDisablePG, tglDisablePort, tglDisableClocks, orgTGLControllerStop;
 uintptr_t orgTGLTrainingBegin, orgTGLTraining, orgTGLTrainingEnd, orgTGLAllocatePathResources;
 static void *tglResourceFB(void *, uint32_t);
 static uint32_t wrapTGLTrainingBegin(void *, void *);
 static uint32_t wrapTGLTraining(void *, void *, void *);
 static uint32_t wrapTGLTrainingEnd(void *, void *);
 static bool wrapTGLAllocatePathResources(void *, uint32_t, uint32_t);
};
XE obj, *callbackXE = &obj;
void invalidateTGLBootPanels() {} // exercised by tgl_boot_depth
struct {uint32_t enabled {},stopSeen {};} tglBootstrapAUX;
// LEASE_FUNCTIONS

template<size_t N> struct Raw {
 uint64_t words[(N+7)/8] {};
 uint8_t *data() { return reinterpret_cast<uint8_t *>(words); }
 template<class T> T &at(size_t offset) { assert(offset + sizeof(T) <= sizeof(words)); return *reinterpret_cast<T *>(data()+offset); }
};
Raw<0xe60> ctl;
Raw<0x80> access, power;
Raw<0x580> ports[9];
Raw<0x4a60> fbs[3];
Raw<0x3700> paths[4];
Raw<0x16a000> mmio;
bool used[4] {}, existingPath, badPipe, failAdoption, stopCalled;
int pgRefs[8] {};
std::atomic<unsigned> nativeCalls {0};
std::function<void()> nativeBoundary;
uint32_t nativeEndResult,nativeTrainingResult;
void *protectedMMIO;
bool protectOnPowerDrop, bufferIdlePending;
std::atomic<unsigned> portDisables{0}, clockDisables{0};
IORecursiveLock poolLock;
void *getPort(uint32_t ddi) { return ddi < 9 ? ports[ddi].data() : nullptr; }
void *lookup(void *) { return existingPath ? paths[0].data() : nullptr; }
void *allocateFixed(uint32_t pipe) {
 if (pipe >= 3 || used[pipe]) return nullptr;
 used[pipe] = true; return paths[pipe].data();
}
void *allocateAny() {
 if (badPipe) { used[3] = true; return paths[3].data(); }
 for (unsigned i=0; i<3; ++i) if (!used[i]) return allocateFixed(i);
 return nullptr;
}
void freePath(void *p) {
 for (unsigned i=0; i<4; ++i) if (p==paths[i].data()) {
  assert(used[i]); used[i]=false; paths[i].at<void *>(0x3648)=nullptr; return;
 }
 assert(false);
}
unsigned expectedPG=0;
void enablePG(void *p, uint32_t pg) { std::lock_guard<std::mutex> lock(refLock); assert(p==power.data() && pg<8); if(expectedPG)assert(pg==expectedPG); ++pgRefs[pg]; }
void disablePG(void *p, uint32_t pg) { std::lock_guard<std::mutex> lock(refLock); assert(p==power.data() && pg<8 && pgRefs[pg]>0); --pgRefs[pg]; if (protectOnPowerDrop && pgRefs[pg]==0) assert(mprotect(protectedMMIO, sizeof(mmio), PROT_NONE)==0); }
uint32_t disablePort(void *p) {
 ++portDisables;
 for (unsigned ddi=0; ddi<9; ++ddi) if (p==ports[ddi].data()) {
  auto &buffer=mmio.at<uint32_t>(0x64000+0x100*ddi);
  buffer=(buffer & ~0x80000080u) | (bufferIdlePending?0:0x80);
 }
 // The donor port disable clears transport enable; it does not clear FUNC.
 for (unsigned pipe=0; pipe<3; ++pipe) if (paths[pipe].at<void *>(0x3648)==p)
  mmio.at<uint32_t>(0x60540+0x1000*pipe)&=~0x80000000u;
 return 0;
}
std::function<void(unsigned)> clockBoundary;
void disableClocks(void *, void *port, void *) {
 ++clockDisables;
 std::lock_guard<IORecursiveLock> lock(poolLock);
 for (unsigned pipe=0; pipe<3; ++pipe) if (paths[pipe].at<void *>(0x3648)==port) {
  // Model the qualified clock wrapper/native boundary, not an imagined FUNC
  // cleanup. Native disableClocks does not clear FUNC or TP configuration.
  mmio.at<uint32_t>(0x46140+4*pipe)=0;
  if(clockBoundary) clockBoundary(pipe);
 }
}
void nativeStop(void *, IOService *) { stopCalled=true; }
uint32_t beginEnd(void *, void *) { ++nativeCalls; if(nativeBoundary) nativeBoundary(); return nativeEndResult; }
uint32_t train(void *, void *, void *) { ++nativeCalls; if(nativeBoundary) nativeBoundary(); return nativeTrainingResult; }
bool allocateResources(void *, uint32_t, uint32_t) {
 ++nativeCalls;
 if(nativeBoundary) nativeBoundary();
 if (failAdoption) return false;
 return XE::wrapTGLAllocatePathAny() != nullptr;
}
void *XE::tglResourceFB(void *controller, uint32_t i) { assert(controller==ctl.data()); return i<3 ? fbs[i].data() : nullptr; }
void setup() {
 for (const auto &r : retained) assert(r.second==0);
 retained.clear();
 obj = XE{};
 obj.tglAUXParentMask=0x1f8;obj.tglTCWiring=TGLTCWiring::fromMasks(0x1f8,0,0); // mock-qualified TC backends
 obj.tglPathPoolLock = &poolLock; obj.tglObservationController=ctl.data();
 obj.tglPortByDDI=(uintptr_t)getPort; obj.orgTGLPathLookup=(uintptr_t)lookup;
 obj.orgTGLAllocatePathAny=(uintptr_t)allocateAny; obj.orgTGLAllocatePathFixed=(uintptr_t)allocateFixed;
 obj.orgTGLFreePath=(uintptr_t)freePath; obj.tglEnablePG=(uintptr_t)enablePG;
 obj.tglDisablePG=(uintptr_t)disablePG; obj.tglDisablePort=(uintptr_t)disablePort;
 obj.tglDisableClocks=(uintptr_t)disableClocks; obj.orgTGLControllerStop=(uintptr_t)nativeStop;
 obj.orgTGLTrainingBegin=(uintptr_t)beginEnd; obj.orgTGLTrainingEnd=(uintptr_t)beginEnd;
 obj.orgTGLTraining=(uintptr_t)train; obj.orgTGLAllocatePathResources=(uintptr_t)allocateResources;
 ctl={}; access={}; power={}; mmio={};
 existingPath=badPipe=failAdoption=stopCalled=false; nativeCalls=0; nativeEndResult=nativeTrainingResult=0;
 nativeBoundary={}; clockBoundary={}; protectedMMIO=nullptr; protectOnPowerDrop=bufferIdlePending=false;portDisables=clockDisables=0;
 std::fill(std::begin(used),std::end(used),false); std::fill(std::begin(pgRefs),std::end(pgRefs),0);
 ctl.at<void *>(0xd40)=obj.tglPlatformCopy; ctl.at<void *>(0xc40)=access.data(); ctl.at<void *>(0xc8)=power.data();
 access.at<void *>(0x50)=mmio.data(); access.at<uint32_t>(0x60)=sizeof(mmio);
 for (unsigned i=0; i<9; ++i) { ports[i]={}; ports[i].at<uint32_t>(0x550)=4; ports[i].at<void *>(0x578)=access.data(); obj.tglTrainingLeases[i].ddi=i;mmio.at<uint32_t>(0x64000+0x100*i)=0x80; }
 for (auto &fb : fbs) { fb={}; fb.at<void *>(0x1d0)=ctl.data(); fb.at<void *>(0x4a40)=access.data(); }
 for (unsigned i=0; i<4; ++i) { paths[i]={}; paths[i].at<uint32_t>(0x3640)=i; }
}
XE::TGLTrainingLease &reserve(unsigned ddi) {
 auto &lease=obj.tglTrainingLeases[ddi];
 assert(XE::tglPrepareTrainingLease(ctl.data(),lease)==0);
 assert(lease.path && lease.powerOwned && lease.busy);
 lease.busy=false; return lease;
}
void release(unsigned ddi) {
 auto &lease=obj.tglTrainingLeases[ddi]; lease.busy=true;
 assert(XE::tglReleaseTrainingLease(lease,true)); lease.busy=false;
}
void clean() {
 for (bool x:used) assert(!x);
 for (int x:pgRefs) assert(x==0);
 for (const auto &r:retained) assert(r.second==0);
}
void observerPowerDrop() {
 setup();
 protectedMMIO=mmap(nullptr,sizeof(mmio),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(protectedMMIO!=MAP_FAILED);
 access.at<void *>(0x50)=protectedMMIO;
 static_cast<uint32_t *>(protectedMMIO)[0x64400/4]=0x80;
 reserve(4); protectOnPowerDrop=true;
 // Real release() drops the last test power reference; disablePG makes the
 // aperture inaccessible. A diagnostic read after that must fail this test.
 release(4); clean();
 assert(munmap(protectedMMIO,sizeof(mmio))==0);
 puts("observer: release completed with inaccessible MMIO after final power put");
}
void samePortArbitration() {
 setup(); uint32_t request[20]={5}, output[20]={};
 // Pause the actual resource wrapper inside its native callback. A different
 // thread attempts all three training stages while no native path exists yet.
 nativeBoundary=[&] {
  assert(obj.tglAllocatingFB[4] && !obj.tglTrainingLeases[4].path);
  std::thread contender([&] {
   assert(XE::wrapTGLTrainingBegin(ctl.data(),request)==kIOReturnBusy);
   assert(XE::wrapTGLTraining(ctl.data(),request,output)==kIOReturnBusy);
   assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==kIOReturnBusy);
  }); contender.join();
 };
 assert(XE::wrapTGLAllocatePathResources(fbs[0].data(),4,0));
 assert(nativeCalls==1 && !obj.tglAllocatingFB[4]);
 nativeBoundary={}; freePath(paths[0].data()); clean();
 // Reverse order: native training owns busy even before a lease exists.
 for (unsigned stage=0;stage<3;++stage) {
  setup(); existingPath=true;
  auto ownerThread=current_thread();
  nativeBoundary=[&] {
   if(current_thread()!=ownerThread) return;
   assert(obj.tglTrainingLeases[4].busy && !obj.tglTrainingLeases[4].path);
   std::thread contender([&] {
    assert(!XE::wrapTGLAllocatePathResources(fbs[0].data(),4,0));
    assert(XE::wrapTGLAllocatePathResources(fbs[1].data(),3,0));
   }); contender.join();
  };
  if(stage==0) assert(XE::wrapTGLTrainingBegin(ctl.data(),request)==0);
  if(stage==1) assert(XE::wrapTGLTraining(ctl.data(),request,output)==0);
  if(stage==2) assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==0);
  assert(nativeCalls==2 && !obj.tglTrainingLeases[4].busy);
  nativeBoundary={}; freePath(paths[0].data()); clean();
 }
 puts("arbitration: same-port allocation vs Begin/Training/End excluded in both orders; other port progresses");
}
void shutdownStatus() {
 // Native disable can return zero while the buffer has not become idle.
 setup();auto &pending=reserve(4);pending.clocksActive=true;bufferIdlePending=true;
 assert(!XE::tglReleaseTrainingLease(pending,true));
 assert(pending.path && pending.powerOwned && pending.clocksActive && portDisables==1 && !clockDisables);
 bufferIdlePending=false;release(4);assert(clockDisables==1);clean();
 // Donor disableDisplay's software state=1/return=0 does not clear a stuck
 // PIPECONF.ACTIVE status. All 3 pipes must retain their own resources.
 for(unsigned pipe=0;pipe<3;++pipe)for(uint32_t state:{0x40000000u,0x80000000u,0xFFFFFFFFu}) {
  setup();for(unsigned i=0;i<pipe;++i)used[i]=true;
  auto &lease=reserve(4);assert(lease.pipe==pipe);lease.clocksActive=true;
  mmio.at<uint32_t>(0x70008+pipe*0x1000)=state;
  assert(!XE::tglReleaseTrainingLease(lease,true));
  assert(lease.path && lease.powerOwned && lease.clocksActive && !portDisables && !clockDisables);
  assert(!XE::tglReleaseTrainingLease(lease,false) && lease.path && lease.powerOwned);
  mmio.at<uint32_t>(0x70008+pipe*0x1000)=0;release(4);
  for(unsigned i=0;i<pipe;++i)used[i]=false;clean();
 }
 // Missing buffer-idle after native End must not restore registers/drop PG.
 setup();auto &end=reserve(4);mmio.at<uint32_t>(0x64400)=0;
 assert(!XE::tglReleaseTrainingLease(end,false) && end.path && end.powerOwned);
 mmio.at<uint32_t>(0x64400)=0x80;assert(XE::tglReleaseTrainingLease(end,false));clean();
 // Controller stop must not turn the same failed release into pool destruction.
 setup();auto &held=reserve(4);bufferIdlePending=true;
 XE::wrapTGLControllerStop(ctl.data(),nullptr);
 assert(!stopCalled&&held.path&&held.powerOwned&&!held.busy&&obj.tglStopRetained&&retained[ctl.data()]==1);
 bufferIdlePending=false;XE::wrapTGLControllerStop(ctl.data(),nullptr);
 assert(stopCalled&&obj.tglStopComplete&&!obj.tglStopRetained);clean();
 // The real unselected Begin wrapper also participates in the drain.
 setup();uint32_t noPort[20]={0};nativeBoundary=[&]{
  XE::wrapTGLControllerStop(ctl.data(),nullptr);assert(!stopCalled&&obj.tglStopRetained);
 };
 assert(!XE::wrapTGLTrainingBegin(ctl.data(),noPort));nativeBoundary={};
 XE::wrapTGLControllerStop(ctl.data(),nullptr);assert(stopCalled&&!obj.tglStopRetained);clean();
 // Unselected protocol calls are drained too, not just per-port busy leases.
 setup();{
  FramebufferDonor::Call active(obj.framebufferProtocolCalls);assert(active);
  XE::wrapTGLControllerStop(ctl.data(),nullptr);assert(!stopCalled&&obj.tglStopRetained);
  FramebufferDonor::Call rejected(obj.framebufferProtocolCalls);assert(!rejected);
 }
 XE::wrapTGLControllerStop(ctl.data(),nullptr);assert(stopCalled&&!obj.tglStopRetained);clean();
 puts("shutdown status: buffer idle missing / pipe enable or active / allones retain resources; no premature clock teardown; explicit retry succeeds on all 3 pipes");
}
void trainingRouteRestoration() {
 // Exercise warm, inactive routes on every pipe. The native mocks must leave
 // the training route present at clock teardown, so only production lease
 // restoration can put the original configuration back. This tests cleanup
 // ownership, not physical power sequencing or absence of hardware hangs.
 for(unsigned pipe=0;pipe<3;++pipe) {
  setup();for(unsigned i=0;i<pipe;++i)used[i]=true;
  const auto func=0x60400+0x1000*pipe,tp=0x60540+0x1000*pipe;
  mmio.at<uint32_t>(func)=0x02030006;
  mmio.at<uint32_t>(tp)=0x00040300;
  auto &lease=reserve(4);assert(lease.pipe==pipe);lease.clocksActive=true;
  mmio.at<uint32_t>(func)=0x2a030006;
  mmio.at<uint32_t>(tp)=0x80040300;
  mmio.at<uint32_t>(0x46140+4*pipe)=0x50000000;
  mmio.at<uint32_t>(0x64400)=0x80003606;
  unsigned boundaryCalls=0;
  clockBoundary=[&](unsigned actualPipe) {
   ++boundaryCalls;assert(actualPipe==pipe);
   assert(mmio.at<uint32_t>(func)==0x2a030006);
   assert(mmio.at<uint32_t>(tp)==0x00040300);
   assert(mmio.at<uint32_t>(0x64400)==0x00003686);
  };
  release(4);
  assert(boundaryCalls==1 && portDisables==1 && clockDisables==1);
  assert(mmio.at<uint32_t>(func)==0x02030006);
  assert(mmio.at<uint32_t>(tp)==0x00040300);
  assert(mmio.at<uint32_t>(0x46140+4*pipe)==0);
  for(unsigned i=0;i<pipe;++i)used[i]=false;clean();
 }
 puts("route restoration: native mocks preserve FUNC; production lease restores saved configuration on all 3 pipes (no hardware ordering claim)");
}
void wiringAdmission() {
 setup();obj.tglAUXParentMask=1u<<4;
 obj.tglTCWiring=TGLTCWiring::fromMasks((1u<<3)|(1u<<4)|(1u<<5),0,0);
 assert(!XE::tglTrainingLeaseForDDI(3) && XE::tglTrainingLeaseForDDI(4) && !XE::tglTrainingLeaseForDDI(5));
 obj.tglTCWiring=TGLTCWiring::fromMasks(0,1u<<4,0);assert(!XE::tglTrainingLeaseForDDI(4));
 // CPU-only qualification of three fake native backends, separate from the
 // production TC2 mask. Run actual Begin/Training/End, not direct lease calls.
 unsigned orders=0,order[3]={3,4,5};
 do {
  setup();obj.tglAUXParentMask=0x38;obj.tglTCWiring=TGLTCWiring::fromMasks(0x38,0,0);
  uint32_t request[20] {},output[20] {};
  for(unsigned ddi=3;ddi<=5;++ddi){
   request[0]=ddi+1;
   assert(XE::wrapTGLTrainingBegin(ctl.data(),request)==0);
   assert(XE::wrapTGLTraining(ctl.data(),request,output)==0);
   assert(obj.tglTrainingLeases[ddi].path && obj.tglTrainingLeases[ddi].pipe==ddi-3);
  }
  for(unsigned ddi:order){request[0]=ddi+1;assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==0);}
  assert(nativeCalls==9);clean();++orders;
 }while(std::next_permutation(order,order+3));
 printf("wiring: intent alone cannot admit backend; actual 3-port Begin/Training/End passed %u release orders with mock-qualified backends\n",orders);
}
void endCompletion() {
 // Execute the production End entry, not merely its cleanup callee. Native
 // End's side effects must happen only once while our reservation remains.
 for(unsigned pipe=0;pipe<3;++pipe)for(bool stuckPipe:{false,true}) {
  setup();for(unsigned i=0;i<pipe;++i)used[i]=true;
  auto &lease=reserve(4);assert(lease.pipe==pipe);lease.clocksActive=true;
  uint32_t request[3]={5,0,0}, output[20] {};
  unsigned nativePuts=0;
  nativeBoundary=[&]{++nativePuts;};
  if(stuckPipe)mmio.at<uint32_t>(0x70008+pipe*0x1000)=0x40000000;
  else mmio.at<uint32_t>(0x64400)=0;
  assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==kIOReturnBusy);
  assert(nativeCalls==1 && nativePuts==1 && lease.endCleanupPending && !lease.clocksActive);
  assert(lease.path && lease.powerOwned && !lease.busy && pgRefs[lease.pg]==1);
  // Same End retries only idle/restore; a conflicting successful-training End,
  // another training call or scanout adoption cannot reuse the stopped lease.
  assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==kIOReturnBusy);
  request[2]=1;assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==kIOReturnBusy);request[2]=0;
  uint32_t training[20]={5};
  assert(XE::wrapTGLTraining(ctl.data(),training,output)==kIOReturnBusy);
  assert(!XE::wrapTGLAllocatePathResources(fbs[pipe].data(),4,0));
  assert(nativeCalls==1 && nativePuts==1 && lease.endCleanupPending && !lease.busy);
  mmio.at<uint32_t>(0x70008+pipe*0x1000)=0;mmio.at<uint32_t>(0x64400)=0x80;
  assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==0);
  assert(nativeCalls==1 && nativePuts==1 && !lease.endCleanupPending && !lease.path && !lease.busy);
  assert(!clockDisables && !portDisables);
  for(unsigned i=0;i<pipe;++i)used[i]=false;clean();
 }
 // A new Begin can finish the retained cleanup before restarting. The
 // already-consumed native clock debt is not returned by this cleanup either.
 setup();auto &pending=reserve(4);pending.clocksActive=true;mmio.at<uint32_t>(0x64400)=0;
 uint32_t request[3]={5,0,0};
 assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==kIOReturnBusy);
 assert(XE::wrapTGLTrainingBegin(ctl.data(),request)==0);
 assert(nativeCalls==2 && portDisables==1 && !clockDisables && !pending.endCleanupPending);clean();
 // Native errors stay native errors, and successful-training End stays native.
 for(uint32_t result:{0u,0xe00002bcu})for(uint32_t commit:{0u,1u}) {
  setup();auto &lease=reserve(4);lease.clocksActive=true;request[2]=commit;nativeEndResult=result;
  assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==result && nativeCalls==1);
  assert(!lease.endCleanupPending && !lease.busy);
  if(result || commit) {assert(lease.path && lease.clocksActive);release(4);}
  else assert(!lease.path);
  clean();
 }
 puts("End completion: all 3 pipes retain failed cleanup and return Busy; retries consume native End once, training/adoption blocked, later End/Begin finishes; native error/commit semantics preserved");
}

void routeLifecycleMatrix() {
 unsigned sequences=0;
 for(unsigned adapter:{0u,1u,3u,4u})for(unsigned ddi=0;ddi<9;++ddi)for(unsigned pipe=0;pipe<3;++pipe)
 for(unsigned initial=0;initial<3;++initial) {
  const unsigned mode=adapter?adapter:1;
  if(adapter==0 ? ddi>1 : adapter==1 ? (ddi!=0&&ddi!=3&&ddi!=4) : ddi<3)continue;
  setup();
  if(adapter==0){obj.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);obj.tglComboDPMask=1U<<ddi;}
  if(adapter==1){obj.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);obj.adlsDisplaySelected=true;obj.adlsWiring.valid=true;obj.adlsWiring.dp=1U<<ddi;}
  if(mode==3){obj.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);obj.tglDPAltCandidates=1U<<ddi;}
  ports[ddi].at<uint32_t>(0x550)=mode;
  for(unsigned i=0;i<pipe;++i)used[i]=true;
  const unsigned func=0x60400+pipe*0x1000,tp=0x60540+pipe*0x1000,buffer=0x64000+ddi*0x100;
  const uint32_t savedFunc=initial?0x02030006:0,savedTP=initial?0x00040300:0;
  mmio.at<uint32_t>(func)=savedFunc;mmio.at<uint32_t>(tp)=savedTP;
  uint32_t request[20]={ddi+1},output[20]={};
  if(initial==2) {
   // An inherited native scanout remains owned by native. A training call
   // must not borrow its pipe/power or restore over its register state.
   existingPath=true;mmio.at<uint32_t>(func)|=0x80000000;
   mmio.at<uint32_t>(tp)|=0x80000000;mmio.at<uint32_t>(buffer)=0x80000000;
   assert(XE::wrapTGLTrainingBegin(ctl.data(),request)==0);
   assert(XE::wrapTGLTraining(ctl.data(),request,output)==0);
   request[2]=1;assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==0);request[2]=0;
   assert(!obj.tglTrainingLeases[ddi].path && !obj.tglTrainingLeases[ddi].powerOwned);
   assert(mmio.at<uint32_t>(func)==(savedFunc|0x80000000));
   for(int refs:pgRefs)assert(!refs);
   // Model the native owner's completed disconnect; not an OS sleep/wake.
   existingPath=false;mmio.at<uint32_t>(func)=savedFunc;mmio.at<uint32_t>(tp)=savedTP;mmio.at<uint32_t>(buffer)=0x80;
  }
  auto &lease=obj.tglTrainingLeases[ddi];
  // Repeat without resetting controller, pool or mode. Successful training,
  // native failure and delayed abort completion all precede a reconnection.
  for(unsigned cycle=0;cycle<4;++cycle) {
   const auto generation=lease.generation;
   assert(XE::wrapTGLTrainingBegin(ctl.data(),request)==0);
   nativeTrainingResult=cycle==1?0xe00002bc:0;
   assert(XE::wrapTGLTraining(ctl.data(),request,output)==nativeTrainingResult);
   assert(lease.path && lease.pipe==pipe && lease.generation==generation+1);
   assert(lease.powerOwned && !lease.busy && pgRefs[lease.pg]==1);
   assert(mmio.at<uint32_t>(func)==savedFunc);
   if(cycle==2) {
    mmio.at<uint32_t>(buffer)=0;
    assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==kIOReturnBusy);
    const auto calls=nativeCalls.load();
    assert(lease.endCleanupPending && lease.powerOwned);
    assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==kIOReturnBusy && nativeCalls==calls);
    assert(XE::wrapTGLTraining(ctl.data(),request,output)==kIOReturnBusy && nativeCalls==calls);
    mmio.at<uint32_t>(buffer)=0x80;
    assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==0 && nativeCalls==calls);
   } else assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==0);
   assert(!lease.path && !lease.busy && !lease.powerOwned && !lease.endCleanupPending);
   assert(mmio.at<uint32_t>(func)==savedFunc && mmio.at<uint32_t>(tp)==savedTP);
   for(int refs:pgRefs)assert(!refs);
   for(unsigned i=0;i<pipe;++i)assert(used[i]);
  }
  // Stop closes admission even when another training request arrives later.
  XE::wrapTGLControllerStop(ctl.data(),nullptr);
  const auto calls=nativeCalls.load();
  assert(XE::wrapTGLTraining(ctl.data(),request,output)==kIOReturnBusy && nativeCalls==calls);
  for(unsigned i=0;i<pipe;++i)used[i]=false;
  clean();++sequences;
 }
 assert(sequences==153);
 puts("PASS 153 actual-wrapper route/pipe/initial-state sequences: 612 train/abort/reconnect cycles, inherited ownership and stop admission (not OS sleep/wake)");
}
int main(int argc, char **argv) {
 if(argc==2) {
  if(!strcmp(argv[1],"end")) { endCompletion(); return 0; }
  if(!strcmp(argv[1],"shutdown")) { shutdownStatus(); return 0; }
  if(!strcmp(argv[1],"observer")) { observerPowerDrop(); return 0; }
  if(!strcmp(argv[1],"arbitration")) { samePortArbitration(); return 0; }
  return 2;
 }
 observerPowerDrop(); samePortArbitration(); shutdownStatus(); endCompletion(); trainingRouteRestoration(); wiringAdmission(); routeLifecycleMatrix();
 unsigned orders=0;
 int in[3]={3,4,5};
 do {
  int out[3]={3,4,5};
  do {
   setup();
   for (int ddi:in) reserve(ddi);
   assert(obj.tglTrainingLeases[3].fb!=obj.tglTrainingLeases[4].fb);
   assert(obj.tglTrainingLeases[4].fb!=obj.tglTrainingLeases[5].fb);
   assert(obj.tglTrainingLeases[3].fb!=obj.tglTrainingLeases[5].fb);
   assert(XE::tglPrepareTrainingLease(ctl.data(),obj.tglTrainingLeases[6])==5);
   assert(!obj.tglTrainingLeases[6].busy);
   for (int ddi:out) {
    // Releasing one port must not read/write another port's DDI buffer.
    for (int other:out) if (other!=ddi && obj.tglTrainingLeases[other].path) mmio.at<uint32_t>(0x64000+0x100*other)=0x80000000;
    release(ddi);
    for (int other:out) if (other!=ddi && obj.tglTrainingLeases[other].path) assert(mmio.at<uint32_t>(0x64000+0x100*other)==0x80000000);
   }
   clean(); ++orders;
  } while(std::next_permutation(out,out+3));
 } while(std::next_permutation(in,in+3));
 setup();
 // An in-flight native FB allocation must exclude that FB from reservations.
 obj.tglAllocatingFB[1]=fbs[0].data();
 auto &claimed=reserve(3); assert(claimed.fb!=fbs[0].data()); release(3);
 obj.tglAllocatingFB[1]=nullptr; clean();
 setup(); reserve(3); auto &l=reserve(4);
 assert(XE::tglTrainingLeaseForDDI(4)==&l);
 assert(XE::tglTrainingLeaseForDDI(3) && !XE::tglTrainingLeaseForDDI(9) && !XE::tglTrainingLeaseForDDI(~0u));
 l.busy=true; assert(XE::tglPrepareTrainingLease(ctl.data(),l)==4 && l.busy); l.busy=false;
 // Failed hardware admission leaves another port's reservation intact.
 mmio.at<uint32_t>(0x64500)=0x80000000;
 assert(XE::tglPrepareTrainingLease(ctl.data(),obj.tglTrainingLeases[5])==5);
 assert(!obj.tglTrainingLeases[5].busy && !obj.tglTrainingLeases[5].path && l.path);
 mmio.at<uint32_t>(0x64500)=0;
 auto native=nativeCalls.load();
 assert(!XE::wrapTGLAllocatePathResources(obj.tglTrainingLeases[3].fb,4,0) && nativeCalls==native);
 failAdoption=true;
 assert(!XE::wrapTGLAllocatePathResources(l.fb,4,0) && l.path && !l.busy && !l.adoptThread);
 failAdoption=false;
 l.adoptThread=current_thread();
 assert(!XE::wrapTGLAllocatePathFixed((l.pipe+1)%3) && !l.adopted);
 void *adopted=l.path;
 std::thread other([&]{ assert(!XE::tglAdoptingLease()); }); other.join();
 l.adoptThread=nullptr;
 assert(XE::wrapTGLAllocatePathResources(l.fb,4,0));
 assert(!l.path && !l.powerOwned && !l.adoptThread && !l.busy);
 freePath(adopted); release(3); clean();
 // Concurrent calls use the actual shared pool lock, separate port slots.
 setup();
 std::thread p3([]{reserve(3);}),p4([]{reserve(4);}),p5([]{reserve(5);});
 p3.join(); p4.join(); p5.join();
 assert(obj.tglTrainingLeases[3].pipe!=obj.tglTrainingLeases[4].pipe);
 assert(obj.tglTrainingLeases[4].pipe!=obj.tglTrainingLeases[5].pipe);
 assert(obj.tglTrainingLeases[3].pipe!=obj.tglTrainingLeases[5].pipe);
 std::thread q3([]{release(3);}),q4([]{release(4);}),q5([]{release(5);});
 q3.join(); q4.join(); q5.join(); clean();
 // Native pool exhaustion and unsupported pipe rollback.
 setup(); std::fill(used,used+3,true);
 assert(XE::tglPrepareTrainingLease(ctl.data(),obj.tglTrainingLeases[4])==6);
 std::fill(used,used+3,false); badPipe=true;
 assert(XE::tglPrepareTrainingLease(ctl.data(),obj.tglTrainingLeases[4])==7); clean();
 // Do not free a live link when release did not disable it.
 setup(); auto &active=reserve(4); mmio.at<uint32_t>(0x64400)=0x80000000;
 assert(!XE::tglReleaseTrainingLease(active,false) && active.path && active.powerOwned);
 release(4); clean();
 // Stop drains all ports, and prevents fresh reservations.
 setup(); reserve(3); reserve(4); reserve(5);
 XE::wrapTGLControllerStop(ctl.data(),nullptr);
 assert(stopCalled && obj.tglLeasesStopping);
 assert(XE::tglPrepareTrainingLease(ctl.data(),obj.tglTrainingLeases[4])==4); clean();
 // Already-active native path: training still owns/relinquishes its busy flag.
 setup(); existingPath=true; uint32_t request[20]={5}, output[20]={};
 assert(XE::wrapTGLTraining(ctl.data(),request,output)==0 && !obj.tglTrainingLeases[4].busy);
 assert(XE::wrapTGLTrainingBegin(ctl.data(),request)==0 && !obj.tglTrainingLeases[4].busy);
 assert(XE::wrapTGLTrainingEnd(ctl.data(),request)==0 && !obj.tglTrainingLeases[4].busy); clean();
 // Native USB-C mode3 shares the resource pool without becoming legacy DP.
 setup();obj.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);obj.tglDPAltCandidates=8;
 assert(!obj.tglLegacyTCEligible(3)&&!obj.tglAUXParentEligible(3));
 assert(XE::tglTrainingLeaseForDDI(3)&&!XE::tglTrainingLeaseForDDI(4));
 assert(XE::tglPrepareTrainingLease(ctl.data(),obj.tglTrainingLeases[3])==1); // mode4 mismatch
 ports[3].at<uint32_t>(0x550)=3;
 // A real panel owns FB0/pipe0: reserve only the remaining external path.
 ctl.at<uint8_t>(0xe55)=1;used[0]=true;fbs[0].at<uint32_t>(0x49e8)=1;
 auto &alt=reserve(3);assert(alt.pipe==1&&alt.fb!=fbs[0].data());release(3);
 assert(used[0]&&fbs[0].at<uint32_t>(0x49e8)==1);used[0]=false;clean();
 ports[3].at<uint32_t>(0x550)=2;
 assert(XE::tglPrepareTrainingLease(ctl.data(),obj.tglTrainingLeases[3])==1); // no TBT
 // ADL-S Combo A/B/C use the same pool and mode1 contract. Allocate all
 // three simultaneously and vary both allocation and teardown order.
 unsigned comboOrders=0,ins[]={0,3,4};
 do {unsigned outs[]={0,3,4};do {
  setup();obj.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);obj.adlsDisplaySelected=true;
  obj.adlsWiring.valid=true;obj.adlsWiring.dp=0x19;obj.tglAUXParentReady=true;
  for(auto d:ins){
   assert(XE::tglTrainingLeaseForDDI(d)&&!obj.tglLegacyTCEligible(d)&&obj.tglAUXParentEligible(d));
   ports[d].at<uint32_t>(0x550)=1;reserve(d);
  }
  for(auto d:outs)release(d);
  clean();++comboOrders;
 }while(std::next_permutation(outs,outs+3));}while(std::next_permutation(ins,ins+3));
 assert(comboOrders==36);
 std::puts("PASS ADL-S Combo A/B/C: 36 three-port allocation/teardown orders using the shared lease implementation");
 // Each reservation must own the union of lane and pipe domains itself.
 // No inherited GOP/AUX refs or another monitor are available to mask a gap.
 unsigned powerCases=0;
 for(unsigned mode:{1u,3u,4u})for(unsigned d=0;d<9;++d)for(unsigned pipe=0;pipe<3;++pipe){
  if(mode==1 ? (d!=0 && d!=3 && d!=4) : d<3)continue;
  setup();
  if(mode==1){obj.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);obj.adlsDisplaySelected=true;obj.adlsWiring.valid=true;obj.adlsWiring.dp=1U<<d;}
  if(mode==3){obj.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);obj.tglDPAltCandidates=1U<<d;}
  ports[d].at<uint32_t>(0x550)=mode;
  for(unsigned i=0;i<pipe;++i)used[i]=true;
  expectedPG=pipe==2?4:(pipe==1 || d>=3?3:1);
  auto &lease=reserve(d);assert(lease.pipe==pipe&&lease.pg==expectedPG);
  release(d);expectedPG=0;
  for(unsigned i=0;i<pipe;++i)used[i]=false;
  clean();++powerCases;
 }
 assert(powerCases==45);
 std::puts("PASS 45 Combo/legacy/DP-alt port x pipe power unions without inherited refs");
 // Nested and concurrent training must not unmute another invocation.
 TGLTrainingScopes scopes;
 auto one=scopes.enter(1), nested=scopes.enter(1), two=scopes.enter(2);
 assert(scopes.active(1) && scopes.active(2) && !scopes.active(3));
 scopes.leave(one); assert(scopes.active(1)); scopes.leave(two); assert(!scopes.active(2)); scopes.leave(nested); assert(!scopes.active(1));
 uint32_t tokens[TGLTrainingScopes::Capacity+2];
 for(unsigned i=0;i<arrsize(tokens);++i) tokens[i]=scopes.enter(i+1);
 assert(scopes.active(999));
 for(unsigned i=0;i<arrsize(tokens);++i) scopes.leave(tokens[i]);
 assert(!scopes.active(999));
 std::atomic<unsigned> entered {0}; std::atomic<bool> finish {false};
 auto worker=[&](uintptr_t id) { auto token=scopes.enter(id); ++entered; while(!finish.load()) { assert(scopes.active(id)); std::this_thread::yield(); } scopes.leave(token); };
 std::thread a(worker,1),b(worker,2),c(worker,3);
 while(entered.load()!=3) std::this_thread::yield();
 assert(scopes.active(1)&&scopes.active(2)&&scopes.active(3)&&!scopes.active(4));
 finish=true; a.join(); b.join(); c.join(); assert(!scopes.active(1)&&!scopes.active(2)&&!scopes.active(3));
 printf("PASS: %u 3-port allocation/release orders; failure/adoption/stop isolation; nested/concurrent/overflow diagnostic scopes\n",orders);
}
