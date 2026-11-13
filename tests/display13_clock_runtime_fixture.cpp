#include <cstring>
#include <new>
#include <string>
#include "kern_display13_wm.hpp"
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_dbuf.hpp"
#include "kern_display13_power_bridge.hpp"
#include "kern_tgl_fb_binding.hpp"
#include "kern_display13_fb_topology.hpp"
// DEVICE
struct Lock {bool held=false;};
void IOLockLock(Lock*p){assert(!p->held);p->held=true;}
void IOLockUnlock(Lock*p){assert(p->held);p->held=false;}
using IORecursiveLock=Lock;
bool IORecursiveLockTryLock(Lock*p){if(p->held)return false;p->held=true;return true;}
void IORecursiveLockUnlock(Lock*p){IOLockUnlock(p);}
int IOSimpleLockLockDisableInterrupt(Lock*p){IOLockLock(p);return 0;}
void IOSimpleLockUnlockEnableInterrupt(Lock*p,int){IOLockUnlock(p);}
unsigned sleeps=0;void IOSleep(unsigned){++sleeps;}
struct OSObject {unsigned size;const OSObject*getMetaClass()const{return this;}unsigned getClassSize()const{return size;}
 void retain(){assert(false);}void release(){assert(false);}};
struct IOService:OSObject {};
struct IOWorkLoop {bool held=false,available=false;unsigned tries=0,opens=0;};
struct IORegistryEntry {
 std::map<std::string,std::vector<uint8_t>> properties;
 static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char*k,const void*p,size_t n){properties[k]={static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n};}
 const void*getProperty(const char*k){return properties.count(k)?properties[k].data():nullptr;}
};
struct Pipe {
 IOService *memoryFramebuffer=nullptr;TGLFBBinding::PublishedPath powerPath;
 bool wmProgramming=false,wmCommitting=false,destroyingPath=false;
 uint32_t wmPending=0;uint64_t watermarkClockHz=0;Display13WM::Latch wmLatch;
};
struct Context {
 Lock *lock=nullptr;IO *map=nullptr;IOService *controller=nullptr;void *power=nullptr;
 IOWorkLoop *workLoop=nullptr;Display13Power::NativeBridge bridge;
 Display13Transcoder::Bindings transcoders;Display13DBUF::State dbuf;Pipe pipe[3];
 bool clockFaulted=false,watermarkFaulted=false,clockStopped=true;uint32_t sequence=0;
 uint64_t resumeClockHz=0;Display13Clock::Readout clock;
}display13FBPower;
struct {Lock *lock=nullptr;IO *base=nullptr;IOService *controller=nullptr;
 unsigned handlers=0,blockedPipes=0;bool quiescing=false;Display13FBIRQ::State state;
}display13FBIRQ;
struct Display13FBPowerIO {IO *map;uint32_t read(uint32_t r){assert(display13FBPower.lock->held);return map->read(r);}};
struct Display13FBClockIO {
 IO *map;bool readable(uint32_t r){return map->readable(r);}uint32_t read(uint32_t r){return map->read(r);}
 void write(uint32_t r,uint32_t v){assert(display13FBPower.lock->held);map->write(r,v);}
 void delayUs(unsigned n){map->delayUs(n);}
};
struct Display13FBIRQIO {IO *base;uint32_t read(uint32_t r){return base->read(r);}void write(uint32_t r,uint32_t v){base->write(r,v);}};
bool irqOwner=true;
bool adlnIRQOwner(IOService*c){return irqOwner&&c==display13FBIRQ.controller;}
void adlnIRQCache(IOService*,Display13FBIRQIO&){}
void adlnFault(bool&v,unsigned){v=true;}
Display13Clock::Variant variant=Display13Clock::Variant::ADLP;
Display13Clock::Variant display13ClockVariant(){return variant;}
unsigned invalidations=0;void adlnInvalidateWM(){++invalidations;}
TGLFBBinding::Readout adlnInspectPathBinding(IOService*,OSObject*,void*,const Display13OutputWiring&){assert(false);return {};}
Display13WM::Latch adlnVerifyWMLatch(IOService*,uint64_t,uint64_t){assert(false);return {};}
struct XE {
 bool display13FBClockSetHookReady=true;uintptr_t adlnWorkLoopInGate=0,adlnWorkLoopTryClose=0,adlnWorkLoopOpen=0;
 void*adlnPlatform=nullptr;Display13OutputWiring adlnOutputWiring;
 static void adlnSetDualClock(IOService*,uint64_t);
}state,*callbackXE=&state;
bool inGate(const IOWorkLoop*p){return p->held;}
bool tryGate(IOWorkLoop*p){++p->tries;if(!p->available)return false;p->held=true;return true;}
void openGate(IOWorkLoop*p){assert(p->held);p->held=false;++p->opens;}
// GUARD
// METHODS
template<class T>void put(void*p,unsigned o,T v){memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
struct Fixture {
 alignas(8) uint8_t ctl[0x1c20]{},power[0x80]{};IO io;Lock owner,irq;IOWorkLoop loop;
 Fixture(){
  display13FBPower.~Context();new(&display13FBPower)Context;display13FBIRQ={};state={};auto&c=display13FBPower;
  c.controller=reinterpret_cast<IOService*>(ctl);c.power=power;c.map=&io;c.lock=&owner;c.workLoop=&loop;
  put(ctl,0,unsigned(sizeof(ctl)));put(ctl,0xe00,&loop);put(power,0x10,c.controller);
  // Native power object association contract, independently of CDCLK state.
  put(ctl,0xc8,static_cast<void*>(power));put(ctl,0xc40,static_cast<void*>(&io));
  c.bridge.begun=true;c.bridge.bootAdopted=true;c.bridge.state.refs[0]=1;
  io.regs[PLL]=0;io.regs[Display13Power::Driver]=0x403;
  io.regs[Display13FBIRQ::Master]=Display13FBIRQ::Enable;
  display13FBIRQ.lock=&irq;display13FBIRQ.base=&io;display13FBIRQ.controller=c.controller;display13FBIRQ.state.enabled=true;
  state.adlnWorkLoopInGate=reinterpret_cast<uintptr_t>(inGate);state.adlnWorkLoopTryClose=reinterpret_cast<uintptr_t>(tryGate);state.adlnWorkLoopOpen=reinterpret_cast<uintptr_t>(openGate);
  sleeps=invalidations=0;irqOwner=true;variant=Display13Clock::Variant::ADLP;
 }
 void check(){assert(!owner.held&&!irq.held&&!display13FBIRQ.quiescing&&!display13FBIRQ.blockedPipes);}
};
int main(){
 for(bool gateOwned:{false,true})for(unsigned ref:{0U,1U,2U}) {
  Fixture f;f.loop.held=gateOwned;f.io.regs[DSSM]=ref<<29;
  const uint64_t hz=ref?1305600000ULL:1296000000ULL;
  XE::adlnSetDualClock(display13FBPower.controller,hz);f.check();
  assert(!display13FBPower.clockFaulted&&!display13FBPower.clockStopped);
  assert(Display13Clock::read(f.io).nativeHz()==hz && f.io.writesTo(PLL)>0);
  assert(Display13FBMemory::field<uint64_t>(f.ctl,0xe98)==hz && invalidations==1);
  assert(!f.loop.tries&&!f.loop.opens&&f.loop.held==gateOwned&&!sleeps);
  assert(f.io.regs[Display13FBIRQ::Master]==Display13FBIRQ::Enable);
 }
 for(unsigned bad=0;bad<9;++bad){
  Fixture f;auto&c=display13FBPower;
  switch(bad){case 0:c.bridge.bootAdopted=false;break;case 1:irqOwner=false;break;
   case 2:c.pipe[1].wmProgramming=true;break;case 3:c.dbuf.updating=true;break;
   case 4:f.io.regs[0x70180]=1U<<31;break;case 5:f.io.regs[0x70080]=1;break;
   case 6:display13FBIRQ.handlers=1;break;case 7:f.io.regs[0x60400]=1U<<31;break;
   case 8:c.controller=nullptr;break;}
  XE::adlnSetDualClock(reinterpret_cast<IOService*>(f.ctl),1305600000);f.check();
  assert(!f.io.writesTo(PLL)&&!f.io.writesTo(Control));
  assert(f.io.regs[Display13FBIRQ::Master]==Display13FBIRQ::Enable);
 }
 {Fixture f;f.io.badControl=true;XE::adlnSetDualClock(display13FBPower.controller,1305600000);f.check();
  assert(display13FBPower.clockFaulted&&f.io.writesTo(PLL)&&!f.io.regs[Display13FBIRQ::Master]);}
 {Fixture f;f.io.regs[PLL]=0xc0000044;display13FBPower.clockStopped=false;
  XE::adlnSetDualClock(display13FBPower.controller,1305600000);f.check();
  assert(!display13FBPower.clockFaulted&&f.io.writes.empty()&&!f.loop.tries);}
 puts("PASS production cold CDCLK with contended gate: owner-held guard, actual clock writes, IRQ restoration and fault containment");
}
