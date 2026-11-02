#include <map>
#include <vector>
#include <new>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
#include "kern_display13_power_bridge.hpp"
#include "kern_tgl_combo_signal.hpp"
#include "kern_vbt.hpp"
#include "kern_xelp_sst_program.hpp"
// NATIVE_OBJECTS
std::map<void *,unsigned> objectRefs;
extern bool locked;
struct OSObject {unsigned size;const OSObject *getMetaClass() const{return this;}unsigned getClassSize() const{return size;}
 void retain(){assert(locked);++objectRefs[this];}void release(){assert(!locked&&objectRefs[this]);if(!--objectRefs[this])objectRefs.erase(this);}};
struct IOService:OSObject {};
constexpr unsigned kIOReturnNotReady=0xE00002D8;
#define FunctionCast(fn,ptr) reinterpret_cast<decltype(&fn)>(ptr)
bool locked=false,bound=true,loseAfterNative=false;unsigned calls=0,nativeResult=0;
void IOLockLock(void*){assert(!locked);locked=true;}void IOLockUnlock(void*){assert(locked);locked=false;}
struct Map {
 std::map<uint32_t,uint32_t> regs;
 std::vector<uint32_t> reads,writes;
 uint32_t base=0,drop=0,delay=0,idleAfter=0,idleReg=0;
 Map():regs{{0x45404,0},{0x45454,0},{0x45444,0},{0x42000,0x8E380000}}{}
 uint32_t read(uint32_t reg){reads.push_back(reg);return regs.at(reg);}
 void write(uint32_t reg,uint32_t v){writes.push_back(reg);if(reg==drop)return;
  if(reg==0x45404)v=(v&~0x5405U)|((v&0xA80A)>>1);
  if(reg==0x45454||reg==0x45444)v=(v&~0x1555U)|((v&0x2aaaU)>>1);
  if(reg==base+0x694)for(unsigned n=0;n<4;++n)regs.at(base+0x894+0x100*n)=v;
  regs.at(reg)=v;
 }
};
struct Context {
 Display13Power::NativeBridge bridge;void *lock=reinterpret_cast<void *>(1),*controller=nullptr,*access=nullptr;
 Display13Power::MainLinkAUX mainLink[2];alignas(void *) uint8_t powerBytes[0x80]{};void *power=powerBytes;
 Map *map=nullptr;bool panelFaulted=false,clockFaulted=false,memoryFaulted=false,clockStopped=false;
}display13FBPower;
struct Display13FBPowerIO {
 Map *map;bool tcAuxSupported(){return true;}bool prepareTCAux(unsigned tc){return tc<4;}bool tcAuxHealthy(unsigned tc){return tc<4;}
 bool readable(uint32_t r){return map&&map->regs.count(r);}uint32_t read(uint32_t r){return map->read(r);}
 void write(uint32_t r,uint32_t v){map->write(r,v);}void delayUs(unsigned us){map->delay+=us;if(map->idleReg&&map->delay>=map->idleAfter)map->regs.at(map->idleReg)|=0x80;}
};
// TC lifecycle itself is exercised by display13_tc_mode. This fixture
// supplies the ownership witness and verifies the frontend does not borrow
// Combo leases; false is the default for all existing Combo regressions.
bool tcOwned=false,tcLinked=false,tcConnected=true;
struct Display13FBTCIO {
 OSObject *object;unsigned tc;
 bool retainedOwned(unsigned d){return tcOwned && tc<4 && d==tc+3 && (Display13FBMemory::field<unsigned>(object,0x550)==4 || Display13FBMemory::field<unsigned>(object,0x550)==3);}
 bool owned(unsigned d){return tcConnected && retainedOwned(d);}
 bool linkHeld(){return tcLinked && retainedOwned(tc+3);}
};
struct Display13FBSSTIO {
 Map *map;uint32_t read(uint32_t r){assert(locked);return map->read(r);}
 void write(uint32_t r,uint32_t v){assert(locked);map->write(r,v);}
};
bool adlnWriterMappingBound(){return bound;}
uintptr_t display13FBExpectedOwner(void*){return 0;}
struct XE {
 XELP::VBT::Ports firmwarePorts;bool tglSinkFramingReady=true;
 bool display13ClockOrderReady=false;
 static bool display13StopTranscoder(void*){assert(false);return false;}
 static bool display13TrainingPath(void*,uint32_t&,uint32_t&){return false;}
 bool display13FramebufferRequested=true,display13FBSSTHooksReady=true,display13FBTCHooksReady=false;XELP::Profile xeProfile;
 Display13OutputWiring adlnOutputWiring;void *adlnPlatform=nullptr,*adlnTopologyController=nullptr;
 uintptr_t orgDisplay13TrainingPattern=0,orgDisplay13SignalLevels=0;
 uintptr_t orgDisplay13EnablePort=0,orgDisplay13DisablePort=0;
 static bool display13MainLinkAUX(void*,bool,bool=false);
 static uint32_t wrapDisplay13EnablePort(void*);static uint32_t wrapDisplay13DisablePort(void*);
 static uint8_t *display13SSTPort(void*);static bool display13HDMIPort(void*);
 static uint32_t wrapDisplay13TCSignal(void*,uint32_t,uint32_t,void*){assert(false);return 1;}
 static uint32_t wrapDisplay13SignalLevels(void*,uint32_t,uint32_t,void*);
 static uint32_t wrapDisplay13TrainingPattern(void*,uint32_t);
}state,*callbackXE=&state;
uintptr_t dsiLogicalSurface(const void*){return 0;}
// BINDING
// FUNCTIONS
unsigned nativeSignal(void*,unsigned,unsigned,void*){assert(!locked);++calls;return nativeResult;}
unsigned linkCalls=0,linkResult=0;bool changeBuffer=true,bufferIdle=true,loseLinkPower=false;
unsigned (*realEnable)(void*)=nullptr,(*realDisable)(void*)=nullptr;void *currentPath=nullptr;unsigned portReplays=0;
unsigned nativeEnable(void *port){assert(!locked);++linkCalls;
 auto &c=display13FBPower;const auto ddi=Display13FBMemory::field<unsigned>(port,0x120);
 Display13FBPowerIO io{c.map};assert(Display13Power::held(io,c.bridge.state,Display13Power::auxDomain(ddi)));
 unsigned result=linkResult;
 if(realEnable&&changeBuffer&&!linkResult){++portReplays;result=realEnable(port);}
 else if(changeBuffer)c.map->regs.at(0x64000+0x100*ddi)=0x80000000;
 if(loseLinkPower)c.map->regs.at(0x45444)=0;
 return result;}
unsigned nativeDisable(void *port){assert(!locked);++linkCalls;
 const auto ddi=Display13FBMemory::field<unsigned>(port,0x120);
 if(realDisable&&changeBuffer&&!linkResult){++portReplays;return realDisable(port);}
 if(changeBuffer)display13FBPower.map->regs.at(0x64000+0x100*ddi)=bufferIdle?0x80:0;return linkResult;}
uint32_t (*realPattern)(void *,uint32_t)=nullptr;
void nativeWrite(void *access,uint64_t reg,uint32_t value){
 assert(!locked&&access==display13FBPower.access);display13FBPower.map->write(static_cast<uint32_t>(reg),value);
}
unsigned nativeRead(void *access,uint64_t reg){assert(!locked&&access==display13FBPower.access);return display13FBPower.map->read(static_cast<uint32_t>(reg));}
unsigned portRead(void *port,uint32_t reg){assert(!locked&&reg==Display13FBMemory::field<unsigned>(port,0x590));return display13FBPower.map->read(reg);}
void portWrite(void *port,uint32_t reg,uint32_t value){
 assert(!locked&&reg==Display13FBMemory::field<unsigned>(port,0x590));
 // Hardware model: DDI buffer enable clears idle; stop may acknowledge late.
 value=(value&~0x80U)|(!(value&0x80000000U)&&bufferIdle?0x80U:0);
 display13FBPower.map->write(reg,value);
}
void *firstPath(void *port){return Display13FBMemory::field<void *>(currentPath,0x3648)==port?currentPath:nullptr;}
void nativeDelay(unsigned us){assert(us==5||us==8);}
void nativeLog(const char*,...){}
void jump(uint8_t *to,uintptr_t target){to[0]=0x48;to[1]=0xB8;memcpy(to+2,&target,8);to[10]=0xFF;to[11]=0xE0;}
void redirect(uint8_t *image,unsigned at,unsigned stub,uintptr_t target){assert(image[at]==0xE8);jump(image+stub,target);const int32_t displacement=static_cast<int32_t>(stub-at-5);memcpy(image+at+1,&displacement,4);}
unsigned nativePattern(void *path,unsigned pattern){
 assert(!locked);++calls;
 const auto pipe=Display13FBMemory::field<unsigned>(path,0x3640);
 if(!nativeResult) {
  if(realPattern)assert(realPattern(path,pattern)==0);
  else {const uint32_t patterns[]={0x200,0,0x100,0x400,0x500,0x300};
   assert(pattern<6);display13FBPower.map->regs[0x60540+0x1000*pipe]=0x80000000|patterns[pattern];}
 }
 if(loseAfterNative)display13FBPower.map->regs[0x45454]=0;
 return nativeResult;
}
void setup(Fixture &f,Map &map,unsigned ddi,unsigned pipe,unsigned lanes,unsigned rate,bool panel=false){
 display13FBPower.~Context();new(&display13FBPower)Context;state={};calls=nativeResult=0;bound=true;loseAfterNative=false;
 tcOwned=tcLinked=false;tcConnected=true;assert(objectRefs.empty());linkCalls=linkResult=0;changeBuffer=bufferIdle=true;loseLinkPower=false;
 auto &c=display13FBPower;c.controller=f.ctl;c.access=f.access;c.map=&map;
 currentPath=f.path;
 put(f.ctl,0xC8,c.power);put(c.power,0x10,static_cast<void *>(f.ctl));put(c.power,0x78,static_cast<void *>(f.access));
 const unsigned peer=ddi>=3?0:1-ddi;
 f.wiring.hdmi=1U<<peer;f.wiring.dp=1U<<ddi;for(unsigned d=0;d<3;++d)f.wiring.ddc[d]=(f.wiring.hdmi&(1U<<d))?d+1:0;
 if(ddi>=3){put(f.donor,0x20+24*ddi+4,ddi+6);put(f.donor,0x20+24*ddi+12,1U);}
 if(panel){f.wiring.panel=1U<<ddi;f.wiring.dp=0;}
 assert(Display13FBTopology::apply(f.platform,sizeof(f.platform),f.donor,f.wiring));
 state.adlnOutputWiring=f.wiring;state.adlnPlatform=f.platform;state.adlnTopologyController=f.ctl;state.xeProfile.display=XELP::Display::XeLPD;
 state.orgDisplay13TrainingPattern=reinterpret_cast<uintptr_t>(nativePattern);state.orgDisplay13SignalLevels=reinterpret_cast<uintptr_t>(nativeSignal);
 state.orgDisplay13EnablePort=reinterpret_cast<uintptr_t>(nativeEnable);state.orgDisplay13DisablePort=reinterpret_cast<uintptr_t>(nativeDisable);
 static uintptr_t vtable[64]{};vtable[0x130/8]=reinterpret_cast<uintptr_t>(nativeWrite);put(f.access,0,static_cast<void *>(vtable));
 put(f.ctl,0,unsigned(sizeof(f.ctl)));put(f.fb,0,unsigned(sizeof(f.fb)));put(f.path,0,unsigned(sizeof(f.path)));
 put(f.plane,0,unsigned(sizeof(f.plane)));put(f.port,0,unsigned(sizeof(f.port)));
 put(f.port,0x578,static_cast<void *>(f.access));put(f.port,0x550,ddi>=3?4U:1U);put(f.port,0x124,lanes);put(f.port,0x128,rate);f.port[0x11A]=1;
 map.base=ddi?0x6C000:0x162000;
 put(f.port,0x620,map.base+0x804);put(f.port,0x5C8,map.base+0x28);put(f.access,0x60,0x200000U);
 put(f.port,0x590,0x64000+0x100*ddi);
 for(auto reg:{0x804U,0x14U,0x28U,0x894U,0x604U,0x694U})map.regs[map.base+reg]=0;
 for(unsigned n=0;n<4;++n)for(auto reg:{0x888U,0x890U,0x894U,0x89CU})map.regs[map.base+0x100*n+reg]=0;
 map.regs[0x46140+4*pipe]=0;map.regs[0x60400+0x1000*pipe]=0;map.regs[0x64000+0x100*ddi]=0;map.regs[0x60540+0x1000*pipe]=0;
 Display13FBPowerIO io{&map};using Display13Power::Status;
 assert(c.bridge.begin(io,peer,f.wiring.selected()).status==Status::Complete);
 const auto pg=pipe?pipe+2:1;
 assert(c.bridge.getPG(io,pg).status==Status::Complete&&c.bridge.getDDI(io,ddi).status==Status::Complete);
 assert(c.bridge.getAUX(io,ddi).status==Status::Complete&&c.bridge.putAUX(io,ddi).status==Status::Complete);
 // Remove boot peer first: training must depend only on its actual resources.
 assert(c.bridge.adoptBootPath(1,0,peer).status==Status::Complete);
 assert(c.bridge.putDDI(io,peer).status==Status::Complete&&c.bridge.putPG(io,1).status==Status::Complete);
 assert(c.bridge.outputHeld(io,ddi,pipe));map.reads.clear();map.writes.clear();
}
int main(int argc,char **argv){
 uint8_t *image=nullptr;
 if(argc>1){
  std::ifstream input(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
  assert(!bytes.empty()&&bytes.size()<0x400000);
  image=static_cast<uint8_t *>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));assert(image!=MAP_FAILED);
  memcpy(image,bytes.data(),bytes.size());realPattern=reinterpret_cast<decltype(realPattern)>(image+0x955B8);
  realEnable=reinterpret_cast<decltype(realEnable)>(image+0x5F9F4);realDisable=reinterpret_cast<decltype(realDisable)>(image+0x5FC56);
  assert(Display13FBMemory::field<uint64_t>(image,0xC95A0)==0x1B446&&Display13FBMemory::field<uint64_t>(image,0xC95A8)==0x1B456);
  put(image,0xC95A0,reinterpret_cast<uintptr_t>(image+0x1B446));put(image,0xC95A8,reinterpret_cast<uintptr_t>(image+0x1B456));
  jump(image+0x1FE8E,reinterpret_cast<uintptr_t>(portRead));jump(image+0x1FEA8,reinterpret_cast<uintptr_t>(portWrite));
  jump(image+0x5E344,reinterpret_cast<uintptr_t>(nativeRead));jump(image+0x91BCA,reinterpret_cast<uintptr_t>(firstPath));
  for(unsigned at:{0x5FA33U,0x5FAC4U,0x5FBEBU,0x5FC4EU})redirect(image,at,0x300000,reinterpret_cast<uintptr_t>(nativeLog));
  for(unsigned at:{0x5FBCBU,0x5FC99U})redirect(image,at,0x300020,reinterpret_cast<uintptr_t>(nativeDelay));
 }
 // eDP uses the same real path/power graph with a native internal connector.
 // Replay native training patterns for all pipe/DDI/lane/polarity combinations.
 for(unsigned ddi=0;ddi<2;++ddi)for(unsigned pipe=0;pipe<3;++pipe)
 for(unsigned lanes:{1U,2U,4U})for(unsigned rate:{6U,10U,20U,30U})
 for(bool low:{false,true})for(unsigned sr=0;sr<2;++sr) {
  Fixture f(0,pipe,ddi);Map map;setup(f,map,ddi,pipe,lanes,rate,true);
  state.firmwarePorts.panel.lowSwing=low;f.port[0x118]=sr;
  assert(!XE::wrapDisplay13SignalLevels(f.port,0,0,nullptr));
  XELP::ComboPolicy expected;assert(expected.select(XELP::ComboPolicy::Platform::ADLP,rate,true,low,false,0,0));
  assert(((map.regs[map.base+0x89c]>>24)&0x7f)==expected.coefficients.scalar);
  for(unsigned enhanced=0;enhanced<2;++enhanced)for(unsigned pattern=0;pattern<6;++pattern) {
   const uint32_t patterns[]={0x200,0,0x100,0x400,0x500,0x300};
   f.port[0x11A]=enhanced;assert(!XE::wrapDisplay13TrainingPattern(f.path,pattern));
   assert(map.regs[0x60540+0x1000*pipe]==(0x80000000|patterns[pattern]|(enhanced<<18)|(sr<<6)));
  }
  assert(!XE::wrapDisplay13EnablePort(f.port));assert(!XE::wrapDisplay13DisablePort(f.port));assert(objectRefs.empty());
  display13FBPower.panelFaulted=true;assert(!XE::display13SSTPort(f.port));
 }
 unsigned count=0,replays=0;
 for(unsigned ddi=0;ddi<2;++ddi)for(unsigned pipe=0;pipe<3;++pipe)for(unsigned lanes:{1U,2U,4U})
 for(unsigned rate:{6U,10U,20U,30U})for(unsigned swing=0;swing<4;++swing)for(unsigned emphasis=0;emphasis<4-swing;++emphasis){
  Fixture f(2-pipe,pipe,ddi);Map map;setup(f,map,ddi,pipe,lanes,rate);
  assert(!XE::wrapDisplay13SignalLevels(f.port,swing,emphasis,nullptr)&&!calls);
  XELP::ComboPolicy policy;assert(policy.select(XELP::ComboPolicy::Platform::ADLP,rate,false,false,false,swing,emphasis));
  assert(((map.regs[map.base+0x89C]>>24)&0x7F)==policy.coefficients.scalar);
  assert(!XE::wrapDisplay13TrainingPattern(f.path,1)&&calls==1);
  assert(map.regs[0x46140+4*pipe]==((ddi+1)<<28));
  assert(map.regs[0x60400+0x1000*pipe]==(((ddi+1)<<27)|0x02000000|((lanes-1)<<1)));
  assert(map.regs[0x60540+0x1000*pipe]==0x80040000);
  assert(!XE::wrapDisplay13TrainingPattern(f.path,2)&&calls==2);
  assert(!XE::wrapDisplay13TrainingPattern(f.path,0)&&calls==3);
  // Actual native instruction replay: all six pattern values lose enhanced
  // framing. The adapter restores only the negotiated bit, preserving the
  // pattern and SST mode for both enhanced-framing settings.
  if(realPattern){
   const uint32_t patterns[]={0x200,0,0x100,0x400,0x500,0x300};
   for(unsigned enhanced=0;enhanced<2;++enhanced)for(unsigned pattern=0;pattern<6;++pattern){
    f.port[0x11A]=enhanced;
    assert(realPattern(f.path,pattern)==0&&map.regs[0x60540+0x1000*pipe]==(0x80000000|patterns[pattern]));
    assert(!XE::wrapDisplay13TrainingPattern(f.path,pattern));
    assert(map.regs[0x60540+0x1000*pipe]==(0x80000000|patterns[pattern]|(enhanced<<18)));++replays;
   }
  }
  const auto completedCalls=calls;
  Display13FBPowerIO io{&map};using Display13Power::Status;
  auto &bridge=display13FBPower.bridge;const auto auxIndex=ddi?6:8;
  assert(!XE::wrapDisplay13EnablePort(f.port)&&linkCalls==1&&objectRefs[f.port]==1);
  assert(bridge.state.refs[auxIndex]==1&&!bridge.nativeAUXRefs(ddi));
  assert(!XE::wrapDisplay13EnablePort(f.port)&&linkCalls==2&&objectRefs[f.port]==1&&bridge.state.refs[auxIndex]==1);
  // Transactions retain/release their own lease while the link stays active.
  for(unsigned transaction=0;transaction<3;++transaction){
   assert(bridge.getAUX(io,ddi).status==Status::Complete&&bridge.state.refs[auxIndex]==2);
   assert(bridge.putAUX(io,ddi).status==Status::Complete&&bridge.state.refs[auxIndex]==1);
   assert(map.regs[0x45444]&(ddi?8:2));
  }
  assert(!XE::wrapDisplay13DisablePort(f.port)&&linkCalls==3&&objectRefs.empty()&&!bridge.state.refs[auxIndex]);
  assert(display13FBPower.bridge.putDDI(io,ddi).status==Status::Complete);
  assert(display13FBPower.bridge.putPG(io,pipe?pipe+2:1).status==Status::Complete);
  for(auto refs:display13FBPower.bridge.state.refs)assert(!refs);
  // Deliberately leave BIOS-like enabled/request bits. No native ownership.
  map.regs[0x45404]=0xFC0F;map.regs[0x45454]=15;map.writes.clear();map.reads.clear();
  assert(XE::wrapDisplay13TrainingPattern(f.path,1)==kIOReturnNotReady&&calls==completedCalls&&map.writes.empty()&&map.reads.empty());
  ++count;
 }
 // TC SST frontend: real graph/bridge and native pattern replay. The
 // separate lifecycle suite proves the ownership witness supplied here.
 for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned pipe=0;pipe<3;++pipe)
 for(unsigned lanes:{1U,2U,4U})for(unsigned rate:{6U,10U,20U,30U})for(bool alt:{false,true}){
  Fixture f(2-pipe,pipe,ddi);Map map;setup(f,map,ddi,pipe,lanes,rate);
  if(alt){state.adlnOutputWiring.typeC=1U<<ddi;put(f.port,0x550,3U);}
  Display13FBPowerIO power{&map};auto &bridge=display13FBPower.bridge;
  Display13Power::Lease modeAUX;
  assert(Display13Power::acquire(power,bridge.state,modeAUX,Display13Power::auxDomain(ddi)).status==Display13Power::Status::Complete);
  assert(!XE::display13MainLinkAUX(f.port,true));tcOwned=true;tcLinked=true;
  assert(XE::display13MainLinkAUX(f.port,true));
  assert(!XE::wrapDisplay13TrainingPattern(f.path,1));
  assert(map.regs[0x46140+4*pipe]==((ddi+3)<<28));
  assert(map.regs[0x60400+0x1000*pipe]==(((ddi+1)<<27)|0x02000000|((lanes-1)<<1)));
  assert(!XE::wrapDisplay13EnablePort(f.port));
  assert(!XE::wrapDisplay13TrainingPattern(f.path,0));
  tcConnected=false; // HPD/assignment disappeared before native disable.
  assert(XE::wrapDisplay13EnablePort(f.port)==kIOReturnNotReady);
  assert(!XE::wrapDisplay13DisablePort(f.port));
  assert(objectRefs.empty() && Display13Power::held(power,bridge.state,Display13Power::auxDomain(ddi)));
  assert(!bridge.nativeAUXRefs(ddi));
  tcOwned=false;assert(!XE::display13MainLinkAUX(f.port,true));
  assert(Display13Power::release(power,bridge.state,modeAUX).status==Display13Power::Status::Complete);
 }
 for(unsigned fault=0;fault<25;++fault){
  Fixture f(2,1,1);Map map;setup(f,map,1,1,4,20);auto &c=display13FBPower;
  if(fault==0)c.bridge.state.suspended=true;if(fault==1)c.bridge.state.faulted=true;
  if(fault==2)c.bridge.ddiRefs[1]=0;if(fault==3)c.bridge.ddiLease[1].owner=nullptr;
  if(fault==4)map.regs[0x45454]&=~8U;if(fault==5)map.regs[0x45454]&=~4U;
  if(fault==6)map.regs[0x45404]&=~0x2000U;if(fault==7)map.regs[0x45404]&=~0x1000U;
  if(fault==8)map.regs[0x42000]&=~0x100000U;if(fault==9)map.regs[0x45454]=~0U;
  if(fault==10)c.bridge.pgRefs[2]=0;if(fault==11)bound=false;
  if(fault==12)c.clockStopped=true;if(fault==13)put(f.port,0x548,reinterpret_cast<void *>(1));
  if(fault==14)f.port[0x117]=1;if(fault==15)put(f.port,0x578,static_cast<void *>(f.ctl));
  if(fault==16)put(f.path,0x3640,0U);if(fault==17)put(f.plane,0x7C,0U);
  if(fault==18)state.display13FramebufferRequested=false;if(fault==19)state.display13FBSSTHooksReady=false;
  if(fault==20)map.drop=0x46144;if(fault==21)map.drop=0x61400;
  if(fault==22)map.regs[0x46144]=0x10000000;
  if(fault==23)map.regs[0x61400]=0x88000000;
  if(fault==24)c.bridge.state.refs[0]=0;
  assert(XE::wrapDisplay13TrainingPattern(f.path,1)==kIOReturnNotReady&&!calls);
  if(fault!=20&&fault!=21)assert(map.writes.empty());
 }
 for(unsigned fault=0;fault<14;++fault){
  Fixture f(2,0,0);Map map;setup(f,map,0,0,4,30);auto &c=display13FBPower;
  if(fault==0)c.bridge.ddiRefs[0]=0;if(fault==1)c.bridge.state.suspended=true;
  if(fault==2)c.bridge.state.refs[0]=0;if(fault==3)map.regs[0x45454]&=~2U;
  if(fault==4)map.regs[0x42000]&=~0x4000000U;
  if(fault==5)put(f.port,0x620,map.base+0x804+4);if(fault==6)put(f.port,0x5C8,map.base+0x28+4);
  if(fault==7)put(f.access,0x60,map.base+0xB9C);
  if(fault==8)map.regs[map.base+0xA9C]=~0U;
  if(fault==9)put(f.port,0x124,3U);if(fault==10)put(f.port,0x128,12U);
  if(fault==11)map.drop=map.base+0x694;
  const auto result=XE::wrapDisplay13SignalLevels(f.port,fault==12?4:0,0,fault==13?f.path:nullptr);
  assert(result==kIOReturnNotReady&&!calls);if(fault!=11)assert(map.writes.empty());
 }
 {Fixture f(2,0,0);Map map;setup(f,map,0,0,2,10);nativeResult=42;
  assert(XE::wrapDisplay13TrainingPattern(f.path,1)==42&&calls==1);
  map.writes.clear();loseAfterNative=true;nativeResult=0;
  assert(XE::wrapDisplay13TrainingPattern(f.path,2)==kIOReturnNotReady&&calls==2);
  if(realPattern)assert(map.writes.size()==1&&map.writes[0]==0x60540);else assert(map.writes.empty());}
 // HDMI peer in a mixed wiring must retain native behavior and result.
 {Fixture f(2,0,0);Map map;setup(f,map,0,0,2,10);put(f.port,0x548,static_cast<void *>(f.platform+0x38));nativeResult=17;
  assert(XE::wrapDisplay13SignalLevels(f.port,0,0,nullptr)==17&&calls==1);
  assert(XE::wrapDisplay13TrainingPattern(f.path,1)==17&&calls==2&&map.writes.empty());}
 // Partial enable keeps AUX until a real stop; failure before enabling can
 // unwind immediately. Late idle succeeds; absent idle retains the owner.
 for(unsigned ddi=0;ddi<2;++ddi)for(unsigned fault=0;fault<6;++fault){
  Fixture f(0,0,ddi);Map map;setup(f,map,ddi,0,2,10);auto &c=display13FBPower;const auto buffer=0x64000+0x100*ddi;
  if(fault<2){linkResult=42;changeBuffer=fault==0;map.regs[buffer]=0x80;
   assert(XE::wrapDisplay13EnablePort(f.port)==42&&linkCalls==1);
   assert(bool(c.mainLink[ddi].owner)==(fault==0));}
  else {
   assert(!XE::wrapDisplay13EnablePort(f.port));
   if(fault==2){bufferIdle=false;map.idleReg=buffer;map.idleAfter=4;}
   if(fault==3)bufferIdle=false;
   if(fault==4)changeBuffer=false;
   if(fault==5)linkResult=42;
   const auto result=XE::wrapDisplay13DisablePort(f.port);
   assert(result==(fault==3||fault==4?kIOReturnNotReady:fault==5?42:0));
   assert(bool(c.mainLink[ddi].owner)==(fault==3||fault==4));
   if(fault==2)assert(map.delay==4);if(fault==3)assert(map.delay==8);
  }
  changeBuffer=bufferIdle=true;linkResult=0;
  if(c.mainLink[ddi].owner)assert(!XE::wrapDisplay13DisablePort(f.port));
  assert(objectRefs.empty()&&!c.mainLink[ddi].owner);
 }
 // Alien power owner/bank/record must never acquire a main-link reference.
 for(unsigned fault=0;fault<6;++fault){
  Fixture f(0,0,0);Map map;setup(f,map,0,0,4,20);auto &c=display13FBPower;
  if(fault==0)put(f.ctl,0xC8,static_cast<void *>(f.port));
  if(fault==1)put(c.power,0x10,static_cast<void *>(f.port));
  if(fault==2)put(c.power,0x78,static_cast<void *>(f.port));
  if(fault==3)put(f.port,0x590,0x64100U);
  if(fault==4)map.regs[0x64000]=0x80000000;
  if(fault==5)map.regs[0x64000]=~0U;
  assert(XE::wrapDisplay13EnablePort(f.port)==kIOReturnNotReady&&!linkCalls&&objectRefs.empty()&&map.writes.empty());
 }
 // A native success with a lost AUX request is not a successful enable.
 {Fixture f(0,0,0);Map map;setup(f,map,0,0,4,20);loseLinkPower=true;
  assert(XE::wrapDisplay13EnablePort(f.port)==kIOReturnNotReady&&linkCalls==1&&objectRefs[f.port]==1);
  assert(display13FBPower.mainLink[0].owner==reinterpret_cast<uintptr_t>(f.port));
  loseLinkPower=false;map.regs[0x45444]=3;
  assert(!XE::wrapDisplay13DisablePort(f.port)&&objectRefs.empty());}
 // Two live Combo DP links share PW1, but neither owns the other's AUX.
 for(unsigned first=0;first<2;++first){
  Fixture f(0,0,0);Map map;setup(f,map,0,0,4,20);auto &c=display13FBPower;Display13FBPowerIO io{&map};
  state.adlnOutputWiring.hdmi=0;state.adlnOutputWiring.dp=3;
  for(auto &pin:state.adlnOutputWiring.ddc)pin=0;
  assert(Display13FBTopology::apply(f.platform,sizeof(f.platform),f.donor,state.adlnOutputWiring));
  alignas(void *) uint8_t peer[sizeof(f.port)];memcpy(peer,f.port,sizeof(peer));
  put(peer,0x120,1U);put(peer,0x548,static_cast<void *>(f.platform+0x38));put(peer,0x590,0x64100U);map.regs[0x64100]=0x80;
  using Display13Power::Status;assert(c.bridge.getDDI(io,1).status==Status::Complete);
  void *ports[]={f.port,peer};
  assert(!XE::wrapDisplay13EnablePort(ports[0])&&!XE::wrapDisplay13EnablePort(ports[1]));
  assert(c.bridge.state.refs[8]==1&&c.bridge.state.refs[6]==1&&objectRefs.size()==2);
  assert(!XE::wrapDisplay13DisablePort(ports[first])&&c.bridge.putDDI(io,first).status==Status::Complete);
  const auto survivor=1-first;
  assert(c.mainLink[survivor].owner==reinterpret_cast<uintptr_t>(ports[survivor]));
  assert(map.regs[0x45444]&(survivor?8:2));
  assert(c.bridge.getAUX(io,survivor).status==Status::Complete&&c.bridge.putAUX(io,survivor).status==Status::Complete);
  assert(!XE::wrapDisplay13DisablePort(ports[survivor])&&objectRefs.empty());
 }
 printf("PASS production Display13 signal/SST: %u compositions after peer release, 39 admission/power/MMIO faults, native error/power-loss/HDMI preservation\n",count);
 puts("PASS main-link AUX: transaction independence, 720 repeated-enable/stop cycles, partial failure/idle timeout/foreign owner guards");
 if(image){munmap(image,0x400000);printf("PASS %u exact native pattern/framing calls and unmodified donor counterexamples; %u native port enable/disable calls\n",replays,portReplays);}
}
