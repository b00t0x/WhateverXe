"""Production bank installer/IRQ/timer/lifecycle -> exact native dispatch.

Linux v6.12 gen11_gt_engine_identity/reset_one_iir defines selector/latch/W1C
and bank-unlock order. Fixed Apple donor tables define native event numbers.
The MMIO model retains late identities, independently latches a bank snapshot,
and honors per-engine masks. Workloop timers, thread calls, gates and forcewake are modeled;
this does not establish electrical IRQ behavior or GPU waiter recovery.
"""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
donor = Path(sys.argv[1]).resolve()
assert hashlib.sha256(donor.read_bytes()).hexdigest() in {
    'bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0',
    'b4893d74f6a52ba01bcf7c18b019445c61f39b754a7cfab62d9d97ff5b5338a0',
}
source = (root/'WhateverXe/kern_xe.cpp').read_text()
a = source.index('// XELP video IRQ ownership:')
b = source.index('\nbool XE::processTGLAccelerator', a)
glue = source[a:b]
a = glue.index('struct XELPVideoIRQIO {')
b = glue.index('\nusing XELPIRQRead', a)
glue = glue[:a] + glue[b:]
a = source.index('                // Qualify the complete parent bank owner')
b = source.index('                if (hostOK && adlnDetected)', a)
installer = source[a:b]
a = source.index('void XE::wrapTGLHostIRQClassEnable(')
b = source.index('\nvoid XE::wrapTGLHostIRQEnable', a)
lifecycle = source[a:b]
code = r'''
#include "kern_xelp_video_irq.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <deque>
#include <map>
#include <fstream>
#include <future>
#include <functional>
#include <mutex>
#include <iterator>
#include <sys/mman.h>
#include <stdexcept>
using mach_vm_address_t=uintptr_t;
static std::map<void *,int> refs;
struct OSObject {void retain(){++refs[this];} void release(){--refs[this];}};
struct IOTimerEventSource;
static std::mutex timerGate;
static std::function<void()> duringWake,beforeJoin,duringNativeDisable;
static bool failLoop=false,failTimer=false,failLock=false,failCall=false;
using IOReturn=int;
#define OSSafeReleaseNULL(obj) do { if(obj) {obj->release();obj=nullptr;} } while(false)
struct IOWorkLoop:OSObject {
 static IOWorkLoop *workLoop();
 IOTimerEventSource *source=nullptr;
 int addEventSource(IOTimerEventSource *s){if(failLoop)return 1;source=s;return 0;}
 int runAction(int (*action)(OSObject *,void *,void *,void *,void *),OSObject *target) {
  if(beforeJoin)beforeJoin();
  std::lock_guard<std::mutex> gate(timerGate);
  return action(target,nullptr,nullptr,nullptr,nullptr);
 }
} loop;
IOWorkLoop *IOWorkLoop::workLoop(){return &loop;}
struct IOService:OSObject {IOWorkLoop *getWorkLoop(){assert(false && "native IOAccel gate inversion");return &loop;}};
struct OSSerialize {};
struct OSData:OSObject {
 static OSData *withBytes(const void *,size_t){static OSData d;return &d;}
 bool serialize(OSSerialize *){return true;}
};
struct OSSerializer:OSObject {
 template<class T>static OSSerializer *forTarget(void *,T){static OSSerializer d;return &d;}
};
struct IORegistryEntry:IOService {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry d;return &d;}
 template<class T>void setProperty(const char *,T){}
};
constexpr int kIOReturnSuccess=0,kIOTimerEventSourceOptionsPriorityWorkLoop=255;
struct IOTimerEventSource:OSObject {
 using Action=void (*)(OSObject *,IOTimerEventSource *);
 Action action;unsigned ms=0,arms=0;bool enabled=true,scheduled=false;
 static IOTimerEventSource *timerEventSource(unsigned option,OSObject *,Action a) {
  assert(option==255);if(failTimer)return nullptr;static IOTimerEventSource s;s.action=a;return &s;
 }
 int setTimeoutMS(unsigned delay){assert(loop.source==this);ms=delay;++arms;scheduled=enabled;return 0;}
 void enable(){enabled=true;}
 void cancelTimeout(){scheduled=false;}
 void fire(){std::lock_guard<std::mutex> gate(timerGate);assert(scheduled && loop.source==this);scheduled=false;action(nullptr,this);}
};
using thread_call_param_t=void *;
constexpr unsigned THREAD_CALL_PRIORITY_KERNEL=1, THREAD_CALL_OPTIONS_ONCE=1, kMillisecondScale=1000000;
struct ThreadCall {
 void (*callback)(void *,void *);bool scheduled=false;unsigned ms=0,arms=0;
 void fire(){std::lock_guard<std::mutex> gate(timerGate);assert(scheduled);scheduled=false;callback(nullptr,nullptr);}
};
using thread_call_t=ThreadCall *;
static thread_call_t thread_call_allocate_with_options(void (*fn)(void *,void *),void *,unsigned priority,unsigned options) {
 assert(priority==THREAD_CALL_PRIORITY_KERNEL && options==THREAD_CALL_OPTIONS_ONCE);
 static ThreadCall c{fn};return failCall?nullptr:&c;
}
static void clock_interval_to_deadline(unsigned ms,unsigned scale,uint64_t *d){assert(scale==kMillisecondScale);*d=ms;}
static void thread_call_enter_delayed(thread_call_t c,uint64_t d){c->scheduled=true;c->ms=d;++c->arms;}
static void thread_call_cancel_wait(thread_call_t c){
 if(beforeJoin)beforeJoin();
 std::lock_guard<std::mutex> gate(timerGate);
 c->scheduled=false;
}
struct IOSimpleLock {bool held=false;};
static IOSimpleLock *IOSimpleLockAlloc(){static IOSimpleLock l;return failLock?nullptr:&l;}
static void IOSimpleLockFree(IOSimpleLock *){}
static int IOSimpleLockLockDisableInterrupt(IOSimpleLock *l){assert(l && !l->held);l->held=true;return 1;}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *l,int){assert(l->held);l->held=false;}
#define PANIC(...) throw std::runtime_error("panic")
#define SYSLOG(...) ((void)0)
struct Hardware {
 uint32_t bank=0,bank0=0,latch=0,mask[3]={0x5a5a0000,0xa5a50000,0x12340000};
 uint32_t intr[3]={0x111,0x111,0x111};
 unsigned delay[3]={},polls[3]={},reads=0,acks=0,clears=0,wake=0;
 unsigned selected=0,ns=0,tick=1000;bool locked=false,stopped=false;
 std::deque<uint32_t> stale;
 std::vector<unsigned> choices,order;
 uint32_t pending()const {return bank&~((mask[0]&0xffff)?1U:0)&~((mask[1]&0xffff)?4U:0)&~((mask[2]&0xffff)?0x80000000U:0);}
};
static Hardware *active;
struct XELPVideoIRQIO {
 volatile uint8_t *bar;
 Hardware &h(){return *reinterpret_cast<Hardware *>(const_cast<uint8_t *>(bar));}
 uint64_t now(){auto &hw=h();hw.ns+=hw.tick;return hw.ns;}
 uint32_t read(uint32_t reg) {
  auto &hw=h();assert(!hw.stopped);
  if(reg==0x190010)return (hw.bank0?1:0)|(hw.pending()?2:0);
  if(reg==0x190018)return hw.bank0;
  if(reg==0x19001c){assert(!hw.locked);hw.locked=true;return hw.pending();}
  if(reg==0x190064) {
   ++hw.reads;
   if(hw.latch)return hw.latch;
   auto n=hw.selected;
   if(++hw.polls[n]<=hw.delay[n])return 0;
   if(!hw.stale.empty()){hw.latch=hw.stale.front();hw.stale.pop_front();}
   else hw.latch=0x80000000U|((n==2?2:1)<<16)|((n==1?2:0)<<20)|hw.intr[n];
   return hw.latch;
  }
  for(unsigned n=0;n<3;++n)if(reg==XELPVideoIRQ::masks[n])return hw.mask[n];
  assert(false);return 0;
 }
 void write(uint32_t reg,uint32_t value) {
  auto &hw=h();assert(!hw.stopped);
  if(reg==0x190074) {
   unsigned n=value==1?0:value==4?1:value==0x80000000U?2:99;
   assert(n!=99);hw.selected=n;hw.choices.push_back(n);return;
  }
  if(reg==0x190064){assert(value==0x80000000U && hw.latch);hw.latch=0;++hw.acks;return;}
  if(reg==0x19001c){assert(hw.locked);hw.locked=false;hw.bank&=~value;++hw.clears;return;}
  if(reg==0x190018){hw.order.push_back(4);hw.bank0&=~value;return;}
  for(unsigned n=0;n<3;++n)if(reg==XELPVideoIRQ::masks[n]){hw.mask[n]=value;return;}
  assert(false);
 }
};
GLUE
static void (*routed)(void *,uint64_t *);
struct KernelPatcher {
 struct RouteRequest {const char *symbol;decltype(routed) destination;
  RouteRequest(const char *s,decltype(routed) d):symbol(s),destination(d){} };
 template<class T>T solveSymbol(size_t,const char *s,uintptr_t a,size_t) {
  const char *names[]={
   "__ZN17IGInterruptBridge22readAndClearInterruptsER8IGBitSetILm46EE",
   "__ZN17IGInterruptBridge25readAndClearRCSInterruptsER8IGBitSetILm46EEj",
   "__ZN17IGInterruptBridge25readAndClearCCSInterruptsER8IGBitSetILm46EEj",
   "__ZN17IGInterruptBridge25readAndClearBCSInterruptsER8IGBitSetILm46EEj",
   "__ZN17IGInterruptBridge25readAndClearGuCInterruptsER8IGBitSetILm46EEj",
   "__ZN17IGInterruptBridge17serviceInterruptsERK8IGBitSetILm46EE",
   "__ZN16IntelAccelerator22SafeForceWakeInterruptEbj"};
  unsigned offsets[]={0x4aff0,0x4b0c0,0x4b116,0x4b16c,0x4b1c2,0x4b3a0,0x2da26};
  for(unsigned i=0;i<7;++i)if(!strcmp(s,names[i]))return T(a+offsets[i]);assert(false);return 0;
 }
 void clearError(){}
 bool routeMultiple(size_t,RouteRequest *r,unsigned n,uintptr_t,size_t){assert(n==1);routed=r->destination;return true;}
};
static bool install(uintptr_t address,size_t size) {
 KernelPatcher patcher;size_t index=1;bool hostOK=true;
 INSTALLER
 return hostOK;
}
static void nativeEnable(void *) {for(auto &m:active->mask)m=0;active->latch=0;}
static void nativeDisable(void *) {
 if(duringNativeDisable)duringNativeDisable();
 active->stopped=true;
}
struct XE {
 uintptr_t orgTGLHostIRQClassEnable=(uintptr_t)nativeEnable,orgTGLHostIRQClassDisable=(uintptr_t)nativeDisable;
 void *tglObservationController=nullptr;
 static void wrapTGLHostIRQClassEnable(void *);
 static void wrapTGLHostIRQClassDisable(void *);
} xe,*callbackXE=&xe;
static struct {void *owner=nullptr;}tglHost;
static void *tglFBIRQBegin(unsigned,void *,uintptr_t,uintptr_t){return nullptr;}
static void tglFBIRQEnd(void *,unsigned){}
#define FunctionCast(fn,addr) reinterpret_cast<decltype(&fn)>(addr)
LIFECYCLE
static std::vector<unsigned> hits;
static void hit(void *,void *arg){hits.push_back(reinterpret_cast<uintptr_t>(arg));}
static void bank0(unsigned n,void *,uint64_t *events,uint32_t p){assert(p==0x2008011);active->order.push_back(n);*events|=1ULL<<(n==3?36:6+n);}
static void rcs(void *b,uint64_t *e,uint32_t p){bank0(0,b,e,p);}
static void ccs(void *b,uint64_t *e,uint32_t p){bank0(1,b,e,p);}
static void bcs(void *b,uint64_t *e,uint32_t p){bank0(2,b,e,p);}
static void guc(void *b,uint64_t *e,uint32_t p){bank0(3,b,e,p);}
static void wake(void *,bool on,unsigned domains){assert(domains==4);if(on){++active->wake;if(duringWake)duringWake();}else {assert(active->wake);--active->wake;}}
static void jump(uint8_t *p,uintptr_t target){const uint8_t h[]={0xff,0x25,0,0,0,0};memcpy(p,h,6);memcpy(p+6,&target,8);}
static constexpr uint64_t Bank0Events=(1ULL<<6)|(1ULL<<7)|(1ULL<<8)|(1ULL<<36);
static uint64_t expect(unsigned mask){return (mask&1?((1ULL<<3)|(1ULL<<9)|(1ULL<<15)):0)|(mask&4?((1ULL<<4)|(1ULL<<10)|(1ULL<<16)):0)|(mask&0x80000000U?((1ULL<<5)|(1ULL<<11)|(1ULL<<17)):0);}
static void checkHits(uint64_t expected){uint64_t got=0;for(auto n:hits){assert(!(got&(1ULL<<n)));got|=1ULL<<n;}assert(got==expected);hits.clear();}
int main(int argc,char **argv) {
 assert(argc==2 || argc==3);
 std::ifstream f(argv[1],std::ios::binary);std::vector<char> original((std::istreambuf_iterator<char>(f)),{});
 auto image=static_cast<uint8_t *>(mmap(nullptr,original.size(),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));assert(image!=MAP_FAILED);memcpy(image,original.data(),original.size());
 auto base=reinterpret_cast<uintptr_t>(image);
 assert(!install(base,0x4b480) && !routed);
 image[0x4b0af]^=1;assert(!install(base,original.size()) && !routed);image[0x4b0af]^=1;
 alignas(8) uint8_t unclaimed[0x20]={};Hardware native;active=&native;
 failLock=true;assert(install(base,original.size()) && !routed);failLock=false;
 failTimer=failCall=true;assert(install(base,original.size()) && !routed);
 XE::wrapTGLHostIRQClassEnable(unclaimed);assert(native.mask[0]==0 && !xelpIRQBridge);
 failTimer=failCall=false;
 failLoop=argc==3;
 assert(install(base,original.size()) && routed && (xelpIRQCall!=nullptr)==failLoop);
 auto scheduled=[] {return xelpIRQCall?xelpIRQCall->scheduled:xelpIRQTimer->scheduled;};
 auto armsCount=[] {return xelpIRQCall?xelpIRQCall->arms:xelpIRQTimer->arms;};
 auto delay=[] {return xelpIRQCall?xelpIRQCall->ms:xelpIRQTimer->ms;};
 auto fire=[] {if(xelpIRQCall)xelpIRQCall->fire();else xelpIRQTimer->fire();};
 jump(image+0x4aff0,(uintptr_t)routed);
 jump(image+0x4b0c0,(uintptr_t)rcs);jump(image+0x4b116,(uintptr_t)ccs);jump(image+0x4b16c,(uintptr_t)bcs);jump(image+0x4b1c2,(uintptr_t)guc);
 jump(image+0x2da26,(uintptr_t)wake);
 static uint64_t cookie=42;auto ptr=&cookie;memcpy(image+0xc8200,&ptr,8);
 assert(!memcmp(image+0xbeb98,"\3\0\0\0\4\0\0\0\17\0\0\0\20\0\0\0\11\0\0\0\12\0\0\0",24));
 alignas(8)uint8_t owner[0x1310]={},bridge[0x900]={};
 *reinterpret_cast<void **>(bridge+0x10)=owner;bridge[0x8a8]=1;
 for(unsigned n=0;n<46;++n){*reinterpret_cast<void **>(bridge+0xc8+n*32)=(void *)hit;*reinterpret_cast<uintptr_t *>(bridge+0xd8+n*32)=n;}
 auto process=reinterpret_cast<void (*)(void *)>(image+0x4b414);
 auto start=[&](Hardware &hw,unsigned media=5){
  *reinterpret_cast<uint64_t *>(owner+0x1300)=media==1?0x2f:media==4?0x37:0x3f;
  active=&hw;*reinterpret_cast<void **>(owner+0x1240)=&hw;xelpIRQState={};XE::wrapTGLHostIRQClassEnable(bridge);assert(xelpIRQEnabled);};
 auto stop=[&](){XE::wrapTGLHostIRQClassDisable(bridge);assert(!xelpIRQBridge && !scheduled() && refs[reinterpret_cast<OSObject *>(bridge)]==0);};
 for(unsigned mask:{0U,1U,4U,5U,0x80000000U,0x80000005U}) {
  Hardware hw;start(hw);hw.bank=mask;hw.bank0=0x2008011;
  process(bridge);checkHits(expect(mask)|Bank0Events);assert(!hw.locked && !hw.latch && hw.acks==hw.choices.size());
  assert(hw.order==std::vector<unsigned>({0,1,2,3,4}));
  hw.bank=mask;process(bridge);checkHits(expect(mask));assert(!xelpIRQState.pending);stop();
 }
 // Delayed DATA_VALID retains the preexisting positive input.
 {Hardware hw;start(hw);hw.bank=4;hw.delay[1]=12;process(bridge);checkHits(expect(4));assert(hw.reads==13 && !xelpIRQState.pending);stop();}
 // Either engine times out: preserve the acquired peer, unlock the bank,
 // quarantine just the unresolved source and retain its notification for retry.
 for(unsigned failed:{0U,1U,2U}) {
  Hardware hw;start(hw);hw.bank=0x80000005U;hw.bank0=0x2008011;hw.delay[failed]=~0U;
  hw.mask[0]=0x5a5a0000;hw.mask[1]=0xa5a50000;hw.mask[2]=0x12340000;
  process(bridge);checkHits(expect(0x80000005U^XELPVideoIRQ::bits[failed])|Bank0Events);
  assert(xelpIRQState.pending==XELPVideoIRQ::bits[failed] && xelpIRQState.timeouts==1 && hw.reads<=103 && !hw.locked);
  assert(hw.mask[failed]==((failed==2?0x12340000U:failed==1?0xa5a50000U:0x5a5a0000U)|0xffff));
  assert(!hw.pending() && scheduled());
  // Unrelated render traffic cannot postpone an already armed retry.
  const auto arms=armsCount();
  for(unsigned i=0;i<100;++i){hw.bank0=0x2008011;process(bridge);checkHits(Bank0Events);}
  assert(armsCount()==arms);
  // A stuck producer reasserts but cannot storm the parent line.
  hw.bank|=XELPVideoIRQ::bits[failed];assert(!hw.pending());
  for(unsigned i=0;i<10;++i){auto before=hw.reads;fire();assert(hw.reads-before<=100 && hw.wake==0 && !hw.pending() && delay()<=100);checkHits(0);}
  assert(delay()==100 && xelpIRQState.pending);
  hw.delay[failed]=0;fire();checkHits(expect(XELPVideoIRQ::bits[failed]));
  assert(!xelpIRQState.pending && !scheduled() && xelpIRQState.resolved==1 && !(hw.mask[failed]&0xffff));
  stop();
 }
 // Native getGPUInfo enumeration (also replayed by adlp_video_route_native)
 // publishes types3/4/5 as 0x2f/0x37/0x3f for physical media masks1/4/5.
 // A foreign identity from an absent engine must never notify that consumer.
 for(unsigned media:{1U,4U}) {
  Hardware hw;start(hw,media);hw.bank=media;
  hw.stale.push_back(media==1?0x80210111U:0x80010111U);
  process(bridge);checkHits(expect(media));assert(xelpIRQState.unknown==1 && !xelpIRQState.pending);stop();
 }
 // All pending engines can fail together, then recover independently.
 {Hardware hw;start(hw);hw.bank=0x80000005U;for(auto &d:hw.delay)d=~0U;
  process(bridge);checkHits(0);assert(xelpIRQState.pending==0x80000005U && hw.reads<=303 && !hw.pending());
  hw.delay[1]=0;fire();checkHits(expect(4));assert(xelpIRQState.pending==0x80000001U);
  hw.delay[0]=hw.delay[2]=0;fire();checkHits(expect(0x80000001U));assert(!xelpIRQState.pending);stop();}
 // A nonadvancing clock still has a read bound. An empty matching identity
 // releases quarantine without inventing any native notification.
 {Hardware hw;start(hw);hw.tick=0;hw.bank=4;hw.delay[1]=~0U;process(bridge);assert(hw.reads==4096);checkHits(0);
  hw.delay[1]=0;hw.intr[1]=0;fire();checkHits(0);assert(!xelpIRQState.pending);stop();}
 // Stale peer and VECS identities are routed by actual identity, once, before
 // reselecting the original source. Unknown class/instance never dispatches.
 for(uint32_t stale:{0x80210111U,0x80020111U,0x80030111U,0x80310111U}) {
  Hardware hw;start(hw);hw.bank=0x80000005U;hw.stale.push_back(stale);process(bridge);
  checkHits(expect(0x80000005U));assert(xelpIRQState.foreign==1 && !xelpIRQState.pending && !hw.latch);stop();
 }
 // Four foreign identities bound the drain and become a visible obligation.
 {Hardware hw;start(hw);hw.bank=1;for(unsigned i=0;i<4;++i)hw.stale.push_back(0x80030001U);
  process(bridge);checkHits(0);assert(xelpIRQState.pending==1 && xelpIRQState.unknown==4 && hw.reads==4);
  fire();checkHits(expect(1));assert(!xelpIRQState.pending);stop();}
 // Lifecycle invalidation cancels retry before MMIO becomes unavailable.
 {Hardware hw;start(hw);hw.bank=4;hw.delay[1]=~0U;process(bridge);checkHits(0);stop();
  assert(xelpIRQState.retired==1 && !xelpIRQState.pending);xelpIRQRetry(nullptr,xelpIRQTimer);assert(!hw.wake);
  hw.stopped=false;XE::wrapTGLHostIRQClassEnable(bridge);hw.bank=4;process(bridge);checkHits(0);
  assert(xelpIRQState.pending==4 && scheduled());
  hw.delay[1]=0;fire();checkHits(expect(4));assert(!xelpIRQState.pending);stop();}
 // Forcewake can be in progress on the private loop when class-disable
 // closes admission. Synchronous cancellation joins that callback. Donor processInterrupts
 // still sees bridge+0x8a8 set until the outer disable completes.
 {Hardware hw;start(hw);hw.bank=4;hw.delay[1]=~0U;process(bridge);checkHits(0);
  const auto reads=hw.reads,arms=armsCount();
  std::promise<void> inWake,allowReturn;
  auto release=allowReturn.get_future();
  duringWake=[&]{inWake.set_value();release.wait();};
  beforeJoin=[&]{allowReturn.set_value();};
  duringNativeDisable=[&]{
   assert(!xelpIRQBridge && !hw.wake);
   hw.bank0=0x2008011;hw.bank=1;
   process(bridge);checkHits(Bank0Events);
   assert(hw.bank==1 && !xelpIRQState.pending);
  };
  auto worker=std::async(std::launch::async,[&]{fire();});
  inWake.get_future().wait();
  stop();worker.get();
  assert(hw.reads==reads && armsCount()==arms && !xelpIRQArmed);
  assert(xelpIRQState.retired==1 && xelpIRQState.resolved==0);
  duringWake={};beforeJoin={};duringNativeDisable={};
 }
 printf("Checked donor dispatch and modeled retry lifecycle (%s)\n",failLoop?"thread call":"workloop");
}
'''.replace('GLUE', glue).replace('INSTALLER', installer).replace('LIFECYCLE', lifecycle)
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    cpp=directory/'test.cpp'
    cpp.write_text(code)
    subprocess.run(['clang++','-std=c++17','-I'+str(root/'WhateverXe'),str(cpp),'-o',str(directory/'test')],check=True)
    for args in ([], ['fail-attachment']):
        subprocess.run([str(directory/'test'),str(donor),*args],check=True)
    # Compiled semantic faults must reach and fail behavior assertions. A
    # compiler failure is not accepted as detecting the intended regression.
    header=(root/'WhateverXe/kern_xelp_video_irq.hpp').read_text()
    mutants={
        'lost_pending': ('s.pending|=bits[n];','s.pending|=0;'),
        'wrong_consumer': ('dispatch(actual,identity,events);','dispatch(n,identity,events);'),
        'absent_engine_consumer': ('if(actual>=0 && !(s.present&bits[actual]))actual=-1;', ''),
        'missing_ack': ('io.write(Identity,Valid);++identities;','++identities;'),
        'unmasked_storm': ('io.read(masks[n])|0xffffU','io.read(masks[n])'),
    }
    for name,(old,new) in mutants.items():
        assert header.count(old)==1
        (directory/'kern_xelp_video_irq.hpp').write_text(header.replace(old,new))
        subprocess.run(['clang++','-std=c++17','-I'+str(directory),str(cpp),'-o',str(directory/'mutant')],check=True)
        result=subprocess.run([str(directory/'mutant'),str(donor)],capture_output=True,text=True)
        assert result.returncode and 'Assertion failed:' in result.stderr,(name,result)
        print('DETECTED',name)
    (directory/'kern_xelp_video_irq.hpp').write_text(header)
    assert code.count('if(events)xelpIRQDispatch(bridge,&events);')==1
    cpp.write_text(code.replace('if(events)xelpIRQDispatch(bridge,&events);','/* lost retry delivery */'))
    subprocess.run(['clang++','-std=c++17','-I'+str(directory),str(cpp),'-o',str(directory/'mutant')],check=True)
    result=subprocess.run([str(directory/'mutant'),str(donor)],capture_output=True,text=True)
    assert result.returncode and 'Assertion failed:' in result.stderr,result
    print('DETECTED lost_retry_dispatch')
    # The attachment failure must preserve ownership of the delayed interrupt.
    old='thread_call_enter_delayed(xelpIRQCall,deadline);'
    assert code.count(old)==1
    cpp.write_text(code.replace(old,'/* retry not scheduled */'))
    subprocess.run(['clang++','-std=c++17','-I'+str(directory),str(cpp),'-o',str(directory/'mutant')],check=True)
    result=subprocess.run([str(directory/'mutant'),str(donor),'fail-attachment'],capture_output=True,text=True,timeout=15)
    assert result.returncode and 'scheduled()' in result.stderr,result
    print('DETECTED lost_fallback_retry')
    # Both backends must join active work without using the accelerator gate.
    # The check runs from stop after the callback has entered forcewake.
    for name,old,new,oracle in [
        ('native_gate_inversion','else xelpIRQLoop->runAction(xelpIRQCancelTimer,nullptr);','else (*reinterpret_cast<IOService **>(static_cast<uint8_t *>(bridge)+0x10))->getWorkLoop()->runAction(xelpIRQCancelTimer,nullptr);','native IOAccel gate inversion'),
        ('retry_after_stop','if(xelpIRQEnabled) {','{','hw.reads==reads'),
        ('late_bank_after_stop','(master&2) && xelpIRQEnabled && xelpIRQBridge==bridge','master&2','got==expected'),
    ]:
        assert code.count(old)==1
        cpp.write_text(code.replace(old,new))
        subprocess.run(['clang++','-std=c++17','-I'+str(directory),str(cpp),'-o',str(directory/'mutant')],check=True)
        result=subprocess.run([str(directory/'mutant'),str(donor)],capture_output=True,text=True,timeout=15)
        assert result.returncode and oracle in result.stderr,(name,result.stderr)
        print('DETECTED',name)
