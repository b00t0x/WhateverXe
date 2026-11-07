#include "kern_display13_fb_memory.hpp"
#include "kern_display13_dbuf.hpp"
#include "kern_display13_wm.hpp"
#include <cassert>
#include <map>
#include <cstdio>
using IOLock=int;using IORecursiveLock=int;
static unsigned ownerDepth,fbDepth;
void IOLockLock(int*){assert(fbDepth && !ownerDepth);++ownerDepth;}
void IOLockUnlock(int*){assert(ownerDepth);--ownerDepth;}
void IORecursiveLockLock(int*){assert(!ownerDepth);++fbDepth;}
void IORecursiveLockUnlock(int*){assert(fbDepth && !ownerDepth);--fbDepth;}
struct OSObject {unsigned size;OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}};
using IOService=OSObject;
struct Map {
 unsigned reads=0;std::map<uint32_t,uint32_t> r;
 Map() {
  r[0x45404]=0xFC0F;r[0x4438C]=0x1C000700;
  for(unsigned p=0;p<3;++p){const auto b=p*0x1000;
   r[0x70008+b]=p?0:0xC0000000;r[0x70080+b]=r[0x60400+b]=0;
   r[0x7027C+b]=p?0:0x07FF0000;r[0x7017C+b]=0;
   for(unsigned n=0;n<5;++n)r[0x70180+b+0x100*n]=!p&&!n?0x84000000:0;
   for(unsigned n=0;n<3;++n)r[0x70B08+b+0x100*n]=0;
  }
  for(unsigned i=0;i<4;++i)r[Display13DBUF::regs[i]]=i<2?0xC040C000:0x40C000;
 }
};
struct Display13FBDBUFIO {
 Map *map;bool readable(uint32_t reg){return map->r.count(reg);}
 uint32_t read(uint32_t reg){assert(readable(reg));++map->reads;return map->r[reg];}
 void write(uint32_t reg,uint32_t value){assert(readable(reg));map->r[reg]=(value&~(1U<<30))|((value&(1U<<31))>>1);}
 void delayUs(unsigned){}
};
namespace TGLFBBinding {enum class Status {Unavailable,Complete};struct Readout {Status status{};unsigned pipe{},ddi{};};}
namespace Display13FBTopology {bool multiOutputWiring(unsigned w){return w!=0;}bool selected(void*,uint32_t*,unsigned){return true;}}
struct Pipe {
 IOService *memoryFramebuffer{};bool destroyingPath{},wmProgramming{},wmCommitting{};
 unsigned wmPending{};uint64_t watermarkClockHz{};Display13WM::Plan wmProgram[2];Display13WM::Latch wmLatch;
};
struct Power {
 int *lock{};IOService *controller{};Map *map{};void *power{};
 bool engineEnabled{},clockStopped{},memoryFaulted{},clockFaulted{},watermarkFaulted{};
 struct Bridge {bool bootAdopted{},begun{};struct {bool suspended{},faulted{};}state;}bridge;
 struct {unsigned domains{};}engine;
 Pipe pipe[3];Display13DBUF::State dbuf;
}display13FBPower;
static bool badBinding,failPrimary,failWM;static unsigned armCalls;static Display13WM::Latch nextLatch;
static std::map<unsigned,std::vector<uint64_t>> records;
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry root;return &root;}void setProperty(const char*,void*,unsigned){}};
void adlnFault(bool &f,unsigned,unsigned=0){f=true;}
void adlnPublishMemory(const char *name,unsigned p,void *data,unsigned bytes,bool=false){
 if(!strcmp(name,"N97FBDBUFLayout"))records[p]=std::vector<uint64_t>((uint64_t*)data,(uint64_t*)data+bytes/8);
}
TGLFBBinding::Readout adlnInspectPathBinding(IOService *fb,OSObject *path,void*,unsigned){
 const auto p=Display13FBMemory::boundPipe(display13FBPower.pipe,fb);
 return badBinding||!path?TGLFBBinding::Readout{}:TGLFBBinding::Readout{TGLFBBinding::Status::Complete,p,3+p};
}
static IOService *dsiFB=nullptr;
bool dsiModeTarget(IOService *fb){return fb && fb==dsiFB;}
struct XE {
 bool display13FBDBUFHooksReady=true,display13FBWatermarkHooksReady=true;struct {unsigned dsi=0;operator unsigned()const{return 0x78;}}adlnOutputWiring;void *adlnPlatform{};
 bool dsiStopAttempted=false;
 uintptr_t adlnAdjustPixelClock=0;
 static bool dsiSharedWriterStopped(IOService*,unsigned);
 static void wrapAlderLakeNPipeWMSetup(IOService*,IOService*,OSObject*,void*);
 static void adlnSetupDBUFPipe(IOService*,IOService*,unsigned,bool);
 static void wrapAlderLakeNEngineEnable(void*){assert(false);}
 static void wrapAlderLakeNPrimaryDBUF(OSObject*,unsigned,unsigned,bool);
 static void wrapDisplay13CursorDBUF(OSObject*);
 static void adlnWMEnable(OSObject*,bool,bool){if(failWM)display13FBPower.watermarkFaulted=true;}
 static void wrapDisplay13WMArmUpdate(IOService*);
}state,*callbackXE=&state;
template<class T>void put(void *p,unsigned off,T v){memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
unsigned planePipe(OSObject *plane){return Display13FBMemory::boundPipe(display13FBPower.pipe,Display13FBMemory::field<IOService*>(plane,0x68));}
void XE::wrapAlderLakeNPrimaryDBUF(OSObject *plane,unsigned start,unsigned end,bool){
 assert(fbDepth&&!ownerDepth);auto &c=display13FBPower;const auto p=planePipe(plane);Display13FBDBUFIO io{c.map};
 if(failPrimary){c.dbuf.faulted=true;return;}
 const auto next=Display13DBUF::toAbsolute(c.dbuf,p,{start,end+1});
 assert(Display13DBUF::preparePrimary(io,c.dbuf,next.start,next.end-1,p)==Display13DBUF::Status::Complete);
 c.map->r[0x7027C+0x1000*p]=Display13DBUF::pendingHardwareRaw(c.dbuf,p);
 assert(Display13DBUF::finishPrimary(io,c.dbuf,p)==Display13DBUF::Status::Complete);
}
void XE::wrapDisplay13CursorDBUF(OSObject *plane){
 assert(fbDepth&&!ownerDepth);auto &c=display13FBPower;const auto p=planePipe(plane);Display13FBDBUFIO io{c.map};
 assert(Display13DBUF::prepareCursor(io,c.dbuf,p)==Display13DBUF::Status::Complete);
 c.map->r[0x7017C+0x1000*p]=Display13DBUF::pendingHardwareRaw(c.dbuf,p);
 assert(Display13DBUF::finishCursor(io,c.dbuf,p)==Display13DBUF::Status::Complete);
 c.pipe[p].wmPending|=2;
}
void XE::wrapDisplay13WMArmUpdate(IOService *fb){
 assert(fbDepth&&!ownerDepth);++armCalls;auto &c=display13FBPower;const auto p=Display13FBMemory::boundPipe(c.pipe,fb);
 c.pipe[p].wmLatch=nextLatch;
 if(nextLatch.status==Display13WM::Status::Complete){
  Display13FBDBUFIO io{c.map};
  assert(Display13DBUF::retire(io,c.dbuf,io.read(0x7027C+0x1000*p),io.read(0x7017C+0x1000*p),10,11,p)==Display13DBUF::Status::Complete);
  c.pipe[p].wmPending=0;c.pipe[p].watermarkClockHz=614400000;
 }
}
IOService *pendingBackingFB=nullptr;
struct {bool modeReady=false;} dsiPendingBacking;
bool dsiPendingTarget(IOService *fb){return fb && fb==pendingBackingFB;}
// PRODUCTION_SETUP
alignas(16) static uint8_t ctl[0x1c20],fb[3][0x4b90],paths[3][0x3730],planes[3][0x1c8];
static int ownerMutex,frameMutex;
void setup(Map &map){
 memset(ctl,0,sizeof(ctl));memset(fb,0,sizeof(fb));memset(paths,0,sizeof(paths));memset(planes,0,sizeof(planes));
 state={};dsiFB=nullptr;display13FBPower={};auto &c=display13FBPower;c.lock=&ownerMutex;c.controller=(IOService*)ctl;c.map=&map;c.engineEnabled=true;
 for(unsigned p=0;p<3;++p){
  ((OSObject*)fb[p])->size=sizeof(fb[p]);((OSObject*)planes[p])->size=sizeof(planes[p]);
  c.pipe[p].memoryFramebuffer=(IOService*)fb[p];put(fb[p],0x4220,&frameMutex);put(fb[p],0x4A00,1U);put(fb[p],0x4A08,(OSObject*)paths[p]);
  put(paths[p],0x32C8,(OSObject*)planes[p]);put(planes[p],0x68,(IOService*)fb[p]);put(planes[p],0x70,(OSObject*)paths[p]);
 }
 Display13FBDBUFIO io{&map};assert(Display13DBUF::begin(io,c.dbuf,7)==Display13DBUF::Status::Complete);
 badBinding=failPrimary=failWM=false;armCalls=0;records.clear();nextLatch={};
}
int main(){
 using S=Display13WM::Status;using C=Display13WM::LatchCheck;
 // Exercise the production default/optimized callback after a cold lost
 // bank, including the deferred case where native PG powers it later.
 for(unsigned p:{1U,2U})for(bool optimized:{false,true}) {
  Map map;setup(map);auto &c=display13FBPower;Display13FBDBUFIO io{&map};
  assert(Display13DBUF::bindOffPipe(io,c.dbuf,p)==Display13DBUF::Status::Complete);
  const auto plan=Display13DBUF::plannedLayout(7,p);
  c.dbuf.pipe[p].primary=plan.primary;c.dbuf.pipe[p].cursor=plan.cursor;
  c.dbuf.restorePending=1U<<p;
  XE::adlnSetupDBUFPipe(c.controller,(IOService*)fb[p],p,optimized);
  assert(!c.dbuf.faulted && !c.dbuf.restorePending && !armCalls);
  assert(records[p][2]==unsigned(Display13DBUF::Status::Complete));
  assert(Display13DBUF::hardwareMatches(io,c.dbuf,p,plan.primary,plan.cursor));
  assert(!ownerDepth && !fbDepth);
 }
 // The caller really programs reservations, but a prepared primary or a
 // delayed live surface must retain them until a later successful arm.
 for(auto check:{C::AwaitingScanout,C::AwaitingFlip})for(bool optimized:{false,true})for(unsigned p=0;p<3;++p){
  Map map;setup(map);auto &c=display13FBPower;Display13FBDBUFIO io{&map};
  if(p)assert(Display13DBUF::bindOffPipe(io,c.dbuf,p)==Display13DBUF::Status::Complete);
  map.r[0x70008+p*0x1000]=0xC0000000;map.r[0x70180+p*0x1000]=0;
  nextLatch.check=check;XE::adlnSetupDBUFPipe(c.controller,(IOService*)fb[p],p,optimized);
  assert(!c.dbuf.faulted&&armCalls==1&&c.pipe[p].wmPending&&c.dbuf.pipe[p].unretired&&!c.pipe[p].watermarkClockHz);
  assert(records[p][2]==unsigned(Display13DBUF::Status::Complete));
  // Another pipe can allocate without retiring this pipe's reservation.
  const auto held=c.dbuf.pipe[p].unretired;unsigned peer=p?0:1;
  if(!p)XE::adlnSetupDBUFPipe(c.controller,(IOService*)fb[peer],peer,false);
  assert(!c.dbuf.faulted&&c.dbuf.pipe[p].unretired==held);
  nextLatch.status=S::Complete;IORecursiveLockLock(&frameMutex);XE::wrapDisplay13WMArmUpdate((IOService*)fb[p]);IORecursiveLockUnlock(&frameMutex);
  assert(!c.pipe[p].wmPending&&!c.dbuf.pipe[p].unretired&&c.pipe[p].watermarkClockHz);
 }
 // A late callback after DSI stop does not touch MMIO, revive the engine,
 // allocate a DDB range, or turn an ordinary stop into a shared fault.
 for(bool pending:{false,true})for(bool cold:{false,true}) {
  Map map;setup(map);auto &c=display13FBPower;state.adlnOutputWiring.dsi=1;state.dsiStopAttempted=!pending;
  pendingBackingFB=pending?(IOService*)fb[0]:nullptr;
  dsiFB=(IOService*)fb[0];
  if(cold){c.engineEnabled=false;c.clockStopped=true;c.bridge.bootAdopted=c.bridge.begun=true;}
  const auto registers=map.r;const auto reads=map.reads;const auto dbuf=c.dbuf;
  XE::adlnSetupDBUFPipe(c.controller,(IOService*)fb[0],0,false);
  IORecursiveLockLock(&frameMutex);XE::wrapAlderLakeNPipeWMSetup(c.controller,(IOService*)fb[0],(OSObject*)paths[0],nullptr);IORecursiveLockUnlock(&frameMutex);
  assert(map.r==registers && map.reads==reads && !armCalls && records.empty());
  assert(!memcmp(&dbuf,&c.dbuf,sizeof(dbuf)) && !c.memoryFaulted && !c.dbuf.faulted && !c.watermarkFaulted);
  assert(!ownerDepth && !fbDepth);
  if(!cold){
   Display13FBDBUFIO io{&map};assert(Display13DBUF::bindOffPipe(io,c.dbuf,1)==Display13DBUF::Status::Complete);
   map.r[0x71008]=0xC0000000;map.r[0x71180]=0;
   nextLatch.check=C::AwaitingScanout;
   XE::adlnSetupDBUFPipe(c.controller,(IOService*)fb[1],1,false);
   assert(armCalls==1 && records.count(1) && !c.dbuf.faulted);
   IORecursiveLockLock(&frameMutex);XE::wrapAlderLakeNPipeWMSetup(c.controller,(IOService*)fb[1],(OSObject*)paths[1],nullptr);IORecursiveLockUnlock(&frameMutex);
   assert(c.watermarkFaulted); // Peer malformed association still reports its real fault.
  }
  pendingBackingFB=nullptr;
 }
 // Generic unavailable results, mismatches, timeouts and allocation failures
 // must not be turned into deferred success by the new branch.
 for(unsigned fault=0;fault<6;++fault){
  Map map;setup(map);auto &c=display13FBPower;
  if(fault==1){nextLatch.status=S::Conflict;nextLatch.check=C::AwaitingScanout;}
  if(fault==2){nextLatch.status=S::CommitTimeout;nextLatch.check=C::Frame;}
  if(fault==3)failPrimary=true;
  if(fault==4)failWM=true;
  if(fault==5)badBinding=true;
  XE::adlnSetupDBUFPipe(c.controller,(IOService*)fb[0],0,false);assert(c.dbuf.faulted);
 }
 assert(!ownerDepth&&!fbDepth);puts("production DBUF setup: deferred retirement and failure guards passed");
}
