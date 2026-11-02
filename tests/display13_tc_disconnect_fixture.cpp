#include "kern_display13_tc_dp.hpp"
#include "kern_display13_tc_mode.hpp"
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_output_wiring.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <new>
using namespace Display13TC;
struct OSObject {alignas(8) uint8_t bytes[0x700]{};int refs=1;void retain(){++refs;}void release(){assert(refs>0);--refs;}const OSObject*getMetaClass(){return this;}unsigned getClassSize()const{return sizeof(bytes);}};
struct IOWorkLoop {} workLoop;
bool inGate=true;bool workLoopInGate(const IOWorkLoop*){return inGate;}
struct IORegistryEntry {static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}void setProperty(const char*,const void*,unsigned){}};
struct MMIO {
 std::map<unsigned,unsigned> regs;std::vector<unsigned> writes,reads;bool noConnectorReads=false;
 bool readable(unsigned r){return regs.count(r);}
 unsigned read(unsigned r){assert(readable(r));if(noConnectorReads)assert(!(r>=0x161500&&r<=0x16150c)&&r!=0xc4000&&r!=0x44470&&r!=0x1638a0&&r!=0x163880&&r!=0x16e8a0&&r!=0x16e880);reads.push_back(r);return regs[r];}
 void write(unsigned r,unsigned v){assert(readable(r));writes.push_back(r);
  if(r==0x45404||r==0x45444||r==0x45454){unsigned mask=r==0x45404?0xa80aU:0x2aaaU;v=(v&~(mask>>1))|((v&mask)>>1);}
  if(r>=0x46038&&r<=0x46050){v=(v&~(Clock::Locked|Clock::Powered))|((v&Clock::Enable)?Clock::Locked:0)|((v&Clock::Power)?Clock::Powered:0);}
  regs[r]=v;
 }
};
struct Context {
 Display13Power::NativeBridge bridge;Mode tcMode[4];OSObject *tcObject[4]{};MMIO *map=nullptr;
 alignas(8) uint8_t controllerBytes[0xe08]{},powerBytes[0x80]{};
 unsigned tcBusy[4]{};IOWorkLoop *workLoop=&::workLoop;
 void *controller=controllerBytes,*power=powerBytes,*lock=(void*)1;bool tcSignalReady[4]{},clockFaulted=false;
} display13FBPower;
struct Display13FBPowerIO {
 MMIO *map;
 bool readable(unsigned r){return map->readable(r);}unsigned read(unsigned r){return map->read(r);}void write(unsigned r,unsigned v){map->write(r,v);}void delayUs(unsigned){}
 bool tcAuxSupported(){return true;}bool prepareTCAux(unsigned t){return (read(0x64300+256*t)&64)!=0;}bool tcAuxHealthy(unsigned){return true;}
};
struct Display13FBTCAuxIO : Display13FBPowerIO {
 unsigned tc;Display13FBTCAuxIO(MMIO*m,unsigned t):Display13FBPowerIO{m},tc(t){}
 bool admitted(unsigned t){return t==tc&&tc<4&&map==display13FBPower.map;}
 unsigned lock(){return 0;}void unlock(unsigned){}void bankFault(){assert(false);}
};
void IODelay(unsigned){}
// PRODUCTION_IO
bool locked=false;void IOLockLock(void*){assert(!locked);locked=true;}void IOLockUnlock(void*){assert(locked);locked=false;}
void adlnFault(bool&fault,unsigned){fault=true;}
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct XE {
 Display13OutputWiring adlnOutputWiring;uintptr_t orgDisplay13DPPhyGating=0;
 uintptr_t adlnWorkLoopInGate=reinterpret_cast<uintptr_t>(workLoopInGate);
 static unsigned display13TCTarget(OSObject*p){return Display13FBMemory::field<unsigned>(p,0x120);}
 static void wrapDisplay13DPPhyGating(void*,bool);
 static unsigned wrapDisplay13TCMode(OSObject*,unsigned);
 static void drainDisplay13TCMode(unsigned);
} instance,*callbackXE=&instance;
// PRODUCTION_GATE
struct Transport : Display13FBTCIO {
 using Display13FBTCIO::Display13FBTCIO;
 using Display13FBTCIO::powerHeld;using Display13FBTCIO::quiescent;
 bool admissionReady(){return true;}bool inheritedVerified(uintptr_t,uintptr_t,unsigned){return false;}
 unsigned nativeMode(){return Display13FBMemory::field<unsigned>(object,0x550);}
 unsigned nativeSetMode(unsigned mode){memcpy(object->bytes+0x550,&mode,4);if(!mode)memset(object->bytes+0x558,0,0x1c);return 0;}
 Display13Power::Result acquireDomains(Display13Power::Lease&l,unsigned d){Display13FBPowerIO io{map};return Display13Power::acquire(io,display13FBPower.bridge.state,l,d);}
 Display13Power::Result releaseDomains(Display13Power::Lease&l){Display13FBPowerIO io{map};return Display13Power::release(io,display13FBPower.bridge.state,l);}
 bool powerHeld(uintptr_t c,uintptr_t o,unsigned d){return c==reinterpret_cast<uintptr_t>(display13FBPower.controller)&&o==reinterpret_cast<uintptr_t>(object)&&d==tc+3&&accessHeld();}
 bool quiescent(uintptr_t c,uintptr_t o,unsigned d){return modeQuiescent(c,o,d);}
};
unsigned XE::wrapDisplay13TCMode(OSObject*p,unsigned wanted){
 assert(!wanted);auto &c=display13FBPower;unsigned d=Display13FBMemory::field<unsigned>(p,0x120);
 Transport io{p,d-3};auto result=c.tcMode[d-3].disconnect(io,reinterpret_cast<uintptr_t>(c.controller),reinterpret_cast<uintptr_t>(p),d);
 if(result.status==Status::Complete){c.tcObject[d-3]=nullptr;p->release();return 0;}return 1;
}
// PRODUCTION_DRAIN
template<class T>void put(void*p,unsigned at,T value){memcpy(static_cast<uint8_t*>(p)+at,&value,sizeof(value));}
int main(){
 for(unsigned d=3;d<=6;++d)for(unsigned gone:{0U,4U,0x2dU,UINT32_MAX})for(bool cleanup:{false,true}){
  display13FBPower.~Context();new(&display13FBPower)Context;auto &c=display13FBPower;MMIO map;c.map=&map;
  Port p;assert(Port::resolve(d,p));OSObject port;c.tcObject[p.tc]=&port;
  unsigned cfg[6]={d};put(port.bytes,0x548,cfg);put(port.bytes,0x120,d);
  instance.adlnOutputWiring.dp=instance.adlnOutputWiring.typeC=1U<<d;
  put(c.controller,0xc8,c.power);put(c.controller,0xc40,static_cast<void*>(&map));
  put(c.controller,0xe00,c.workLoop);
  map.regs={{0x45404,0},{0x45444,0},{0x45454,0},{0x42000,0x8e380000},{p.ready,0x1d},{p.buffer,0x80},{p.pll,0},{p.clock,0},{Clock::Gate,0xa5000000|Clock::gateBit(p.tc)},
   {p.laneAssignment(),12U<<(8*(p.tc&1))},{p.pinAssignment(),4U<<(4*(p.tc&1))},{0xc4000,0},{0x44470,1U<<(16+p.tc)},
   {0x60400,0},{0x70008,0},{0x46140,0},{p.hip,0x01010101},{p.phyRegister(0xa0),0},{0x64310+256*p.tc,0}};
  Display13FBPowerIO power{&map};assert(Display13Power::initialize(power,c.bridge.state,Display13Power::nativeDomains(0x7b)));c.bridge.begun=true;c.bridge.selectedPorts=0x78;
  Transport io{&port,p.tc};auto &mode=c.tcMode[p.tc];const auto who=reinterpret_cast<uintptr_t>(c.controller),obj=reinterpret_cast<uintptr_t>(&port);
  assert(mode.connect(io,who,obj,d,3).status==Status::Complete&&io.owned(d));
  assert(c.bridge.getDDI(power,d).status==Display13Power::Status::Complete);
  map.regs[p.pll]=0xcc000000;map.regs[p.clock]=0x80001234;map.regs[Clock::Gate]&=~Clock::gateBit(p.tc);c.tcSignalReady[p.tc]=true;
  put(port.bytes,0x570,1U); // Cached link config survives until native reset.
  assert(mode.disconnect(io,who,obj,d).status==Status::InUse&&mode.disconnectPending);
  map.regs[0x44470]=0;map.regs[p.ready]=gone;map.regs[p.laneAssignment()]=map.regs[p.pinAssignment()]=0;
  assert(!io.owned(d)&&io.retainedOwned(d));
  // The power-down PHY gate keeps the captured assignment after HPD falls.
  // Pin D mask C selects only the second physical pair.
  map.writes.clear();XE::wrapDisplay13DPPhyGating(&port,true);
  assert(!c.clockFaulted&&map.regs[p.phyRegister(0xa0)]==0x3e&&map.writes.size()==1);
  if(cleanup){mode.phase=Mode::Phase::Cleanup;io.nativeSetMode(0);assert(io.retainedOwned(d));}
  const auto buffer=map.regs[p.buffer];
  map.regs[p.buffer]&=~64U;assert(!io.retainedOwned(d));
  map.regs[p.buffer]=UINT32_MAX;assert(!io.retainedOwned(d));map.regs[p.buffer]=buffer;
  OSObject stranger;c.tcObject[p.tc]=&stranger;assert(!io.retainedOwned(d));c.tcObject[p.tc]=&port;
  auto leaseOwner=mode.auxLease.owner;mode.auxLease.owner=nullptr;assert(!io.retainedOwned(d));mode.auxLease.owner=leaseOwner;
  const auto owner=mode.ownership.owner;mode.ownership.owner=owner+1;
  assert(!io.retainedOwned(d));mode.ownership.owner=owner;
  const auto peer=map.regs[Clock::Gate]&~Clock::gateBit(p.tc);map.writes.clear();map.noConnectorReads=true;
  map.regs[p.buffer]|=1U<<31;assert(!Clock::stopLink(io,p)&&map.writes.empty());map.regs[p.buffer]&=~(1U<<31);
  const auto wells=map.regs[0x45404];map.regs[0x45404]|=1U<<10;
  map.regs[0x60400]=0x80000000U|((d+1)<<27);map.regs[0x70008]=0xc0000000;
  assert(!Clock::stopLink(io,p)&&map.writes.empty());
  map.regs[0x60400]=map.regs[0x70008]=0;map.regs[0x45404]=wells;
  assert(Clock::stopLink(io,p)&&!c.bridge.nativeDDIRefs(d)&&!(map.regs[p.pll]&0xcc000000U));
  assert((map.regs[Clock::Gate]&~Clock::gateBit(p.tc))==peer);
  c.tcSignalReady[p.tc]=false;
  if(!cleanup) {
   inGate=false;XE::drainDisplay13TCMode(d);assert(mode.disconnectPending&&io.nativeMode()==3);
   inGate=true;c.tcBusy[p.tc]=1;XE::drainDisplay13TCMode(d);assert(mode.disconnectPending);
   c.tcBusy[p.tc]=0;map.regs[0x64310+256*p.tc]=0x80000000;
   XE::drainDisplay13TCMode(d);assert(mode.disconnectPending);
   map.regs[0x64310+256*p.tc]=0;
   assert(Display13FBMemory::field<unsigned>(&port,0x570)==1);
   XE::drainDisplay13TCMode(d);assert(!mode.disconnectPending&&!io.nativeMode()&&!c.tcObject[p.tc]&&!port.refs);
   port.refs=1;c.tcObject[p.tc]=&port;
  } else assert(mode.disconnect(io,who,obj,d).status==Status::Complete&&!io.nativeMode());
  assert(!(map.regs[p.buffer]&64)&&mode.phase==Mode::Phase::Idle&&!mode.lanes.mask);
  for(auto refs:c.bridge.state.refs)assert(!refs);
  // Reconnect must capture the new assignment, not the previous orientation.
  map.noConnectorReads=false;map.regs[p.ready]=0x1d;map.regs[0x44470]=1U<<(16+p.tc);
  map.regs[p.laneAssignment()]=3U<<(8*(p.tc&1));map.regs[p.pinAssignment()]=4U<<(4*(p.tc&1));
  assert(mode.connect(io,who,obj,d,3).status==Status::Complete&&mode.lanes.mask==3&&io.owned(d));
 }
 puts("PASS real TC owner IO: lost HPD/mode/assignment, retained identity and power, clock stop then mode release, new orientation on reconnect");
}
