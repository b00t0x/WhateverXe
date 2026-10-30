#include "kern_display13_fb_memory.hpp"
#include "kern_display13_dbuf.hpp"
#include "kern_display13_wm.hpp"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
using IOLock=int;
static unsigned locks;
void IOLockLock(int*){assert(!locks);++locks;}
void IOLockUnlock(int*){assert(locks);--locks;}
struct OSObject {unsigned size;OSObject *getMetaClass(){return this;}unsigned getClassSize(){return size;}};
using IOService=OSObject;
struct Map {std::map<uint32_t,uint32_t> r;};
struct Display13FBDBUFIO {Map *map;uint32_t read(uint32_t reg){assert(map->r.count(reg));return map->r[reg];}};
static unsigned selectedPipe;
namespace TGLFBBinding {
 enum class Status {Unavailable,Complete};
 struct Object {void *p;unsigned size;};
 struct Objects {Object controller,fb,path,plane,port;unsigned owner;bool logical;};
 struct Result {Status status;unsigned framebuffer,pipe,ddi,plane;};
 Result inspect(const Objects&,void*,unsigned){return {Status::Complete,selectedPipe,selectedPipe,selectedPipe,0};}
}
struct Pipe {IOService *memoryFramebuffer{};uint64_t watermarkClockHz{};Display13WM::Plan wm[2];};
struct Power {
 int *lock{};IOService *controller{};Map *map{};
 bool engineEnabled{},clockFaulted{},memoryFaulted{},watermarkFaulted{};
 struct {struct {bool faulted{};}state;}bridge;
 Display13DBUF::State dbuf;Display13WM::Latency latency;Pipe pipe[3];
}display13FBPower;
struct IORegistryEntry {
 std::map<std::string,std::vector<uint8_t>> records;
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry root;return &root;}
 void setProperty(const char *name,const void *p,unsigned size){records[name]=std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+size);}
 void *getProperty(const char *name){return records.count(name)?this:nullptr;}
};
unsigned display13FBExpectedOwner(void*){return 0;}
bool dsiLogicalSurface(void*){return true;}
void adlnFault(bool &f,unsigned){f=true;}
struct XE {
 bool display13FBWatermarkHooksReady=true;void *adlnPlatform{};unsigned adlnOutputWiring=0;
 uintptr_t adlnAdjustPixelClock{};
 static bool dsiSharedWriterStopped(void*,unsigned){return false;}
 static void adlnWMSetup(OSObject*,OSObject*,bool);
}state,*callbackXE=&state;
uint64_t adjust(void*,void*,void*,bool cursor,void*){assert(cursor);return 135255000;}
template<class T>void put(void *p,unsigned off,T v){memcpy((uint8_t*)p+off,&v,sizeof(v));}
// PRODUCTION_SETUP
alignas(16) static uint8_t ctl[0x1c20],fb[0x4b90],path[0x3730],plane[0x1c8];
static int mutex;
void setup(Map &map,unsigned pipe,uint32_t oldControl,uint32_t width){
 memset(ctl,0,sizeof(ctl));memset(fb,0,sizeof(fb));memset(path,0,sizeof(path));memset(plane,0,sizeof(plane));
 ((OSObject*)ctl)->size=sizeof(ctl);((OSObject*)fb)->size=sizeof(fb);
 ((OSObject*)path)->size=sizeof(path);((OSObject*)plane)->size=sizeof(plane);
 selectedPipe=pipe;display13FBPower={};auto &c=display13FBPower;
 c.lock=&mutex;c.controller=(IOService*)ctl;c.map=&map;c.engineEnabled=true;c.dbuf.begun=true;
 c.pipe[pipe].memoryFramebuffer=(IOService*)fb;
 c.dbuf.pipe[pipe].cursor=pipe?Display13DBUF::Range{2048,2088}:Display13DBUF::Range{0,40};
 c.latency=Display13WM::decodeLatency(0x63503300,0x00909090,30);
 assert(c.latency.status==Display13WM::Status::Complete);
 put(plane,0x68,(IOService*)fb);put(plane,0x100,0x94001000U);
 put(fb,0x4278,width);put(path,0x13A,uint16_t(1125));
 map.r={{0x45404,0xFC0F},{0x45004,8},{0x70080+0x1000*pipe,oldControl}};
 state.adlnAdjustPixelClock=(uintptr_t)adjust;IORegistryEntry::getRegistryRoot()->records.clear();
}
int main(){
 const unsigned widths[]={64,128,256},controls[]={0x10000027,0x10000022,0x10000023};
 for(unsigned p=0;p<3;++p)for(unsigned old=0;old<3;++old)for(unsigned next=0;next<3;++next){
  Map map;setup(map,p,controls[old],widths[next]);auto before=map.r;
  XE::adlnWMSetup((OSObject*)plane,(OSObject*)path,true);
  auto &c=display13FBPower;const auto &plan=c.pipe[p].wm[1];
  assert(!c.watermarkFaulted && plan.status==Display13WM::Status::Complete && plan.width==widths[next]);
  assert(map.r==before); // Preparation does not change CURCNTR to make the check pass.
  const auto expected=Display13WM::plan(widths[next],1126,135255000,40,c.latency,true,false,widths[next]);
  assert(plan.normal[0].encoded()==expected.normal[0].encoded());
  // After application the old size must still fail the strict match.
  assert(Display13WM::cursorMatches(controls[old],plan.width)==(old==next));
  assert(Display13WM::cursorMatches(controls[next],plan.width));
 }
 for(unsigned invalid:{0U,32U,512U}){
  Map map;setup(map,0,0x10000027,invalid);XE::adlnWMSetup((OSObject*)plane,(OSObject*)path,true);
  assert(display13FBPower.watermarkFaulted);
 }
 Map map;setup(map,0,0x10000027,256);display13FBPower.dbuf.pipe[0].cursor={0,0};
 XE::adlnWMSetup((OSObject*)plane,(OSObject*)path,true);assert(display13FBPower.watermarkFaulted);
 assert(!locks);puts("production cursor WM: 27 size transitions, post-apply mismatch and invalid input checks passed");
}
