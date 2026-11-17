#!/usr/bin/env python3
"""Real setup-workloop -> SST selection -> boot AUX adoption -> port teardown.

IRQ provider registration and the already-qualified framebuffer memory are OS
boundaries here. Actual port/path associations, source readout, power bridge,
AUX lease transfer and stop/enable wrappers are production code.
"""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
def fn(sig):return re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group()
base=(r/'tests/display13_sst_fixture.cpp').read_text().split('int main(')[0]
base=base.replace('// NATIVE_OBJECTS',(r/'tests/tgl_fb_binding.cpp').read_text().split('int main()')[0].replace('port[0x580]','port[0x700]'))
base=base.replace('struct IOService:OSObject {};', '''
struct IOService:OSObject {void *getProvider(){return this;}};
struct IOPCIDevice:IOService {};
struct IOWorkLoop:OSObject {};
struct IOFilterInterruptEventSource:OSObject {
 void *provider;IOWorkLoop *loop;bool enabled=true;
 void *getProvider(){return provider;}int getIntIndex(){return 0;}
 IOWorkLoop *getWorkLoop(){return loop;}bool isEnabled(){return enabled;}
};
#define OSDynamicCast(T,p) reinterpret_cast<T*>(p)
constexpr unsigned kIOReturnSuccess=0;
struct IORegistryEntry {static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}
 template<class... T>void setProperty(T...){} };
''')
base=base.replace('return regs.at(reg);','return regs[reg];')
base=base.replace('struct Context {','''struct Context {
 IOWorkLoop *workLoop=nullptr;AlderLakeN::DisplayReadout displayScratch;
 struct {IOService *memoryFramebuffer=nullptr;Display13FBMemory::Readout memory;} pipe[3];
''')
base=base.replace('struct XE {','''struct XE {
 bool display13FBIRQReady=true,display13FBBootHandoffHookReady=true;
 uintptr_t orgAlderLakeNSetupWorkLoop=0;
 static void wrapAlderLakeNSetupWorkLoop(IOService*,IOWorkLoop*);
 static unsigned wrapDisplay13FBEnableIRQ(IOService*){assert(!locked);return 0;}
 static void wrapDisplay13FBDisableIRQ(IOService*){assert(!locked);}
 bool adoptDSIBootPath(IOService*,OSObject*){assert(false);return false;}
''')
base=base.replace('// BINDING',fn('static TGLFBBinding::Readout adlnInspectPathBinding('))
methods='\n'.join(fn(x) for x in ['uint8_t *XE::display13SSTPort(', 'bool XE::display13HDMIPort(', 'bool XE::display13MainLinkAUX(', 'uint32_t XE::wrapDisplay13EnablePort(', 'uint32_t XE::wrapDisplay13DisablePort(', 'uint32_t XE::wrapDisplay13SignalLevels(', 'uint32_t XE::wrapDisplay13TrainingPattern(', 'void XE::wrapAlderLakeNSetupWorkLoop('])
boundaries='''
struct Display13FBBootIO:Display13FBPowerIO {
 bool readable(uint32_t r){return r<0x1000000;}
};
bool adlnIRQOwner(IOService*p){return p==display13FBPower.controller;}
Display13FBMemory::Readout memoryResult;
Display13FBMemory::Readout display13FBInspectMemory(IOService*){return memoryResult;}
void adlnFault(bool&f,unsigned){f=true;}
void display13FBPowerRecord(uint64_t*,unsigned,unsigned,const Display13Power::Result&){}
void adlnPublishPowerRecord(const char*,uint64_t*){}
'''
base=base.replace('// FUNCTIONS',boundaries+methods)
main=r'''
void nativeSetup(IOService *controller,IOWorkLoop*loop){assert(!locked);put(controller,0xe00,loop);}
int main(){
 for(unsigned ddi:{0U,1U})for(unsigned mode:{0U,1U,3U,4U})for(bool foreign:{false,true}) {
  objectRefs.clear();Fixture f(0,0,ddi);Map map;setup(f,map,ddi,0,4,20,true);
  auto &c=display13FBPower;c.bridge.~NativeBridge();new(&c.bridge)Display13Power::NativeBridge;
  map.regs[0x45404]=0xfc0f;map.regs[0x45454]=15;
  map.regs[0x45444]=1U<<(2*ddi);map.regs[0x42000]=0x8e380000;
  map.regs[0x60400]=0x82030006|((ddi+1)<<27);map.regs[0x70008]=0xc0000000;
  map.regs[0x64000+ddi*0x100]=0x80000006;map.regs[0x46140]=(ddi+1)<<28;
  map.regs[0x60540]=0x80040300;map.regs[0x60030]=0x7e100000;
  map.regs[0x60034]=map.regs[0x60044]=0x800000;map.regs[0x60040]=0x100000;
  Display13FBBootIO io{&map};assert(c.bridge.begin(io,ddi,3).status==Display13Power::Status::Complete);
  auto boot=AlderLakeN::readDisplay(io,true);
  assert(AlderLakeN::holdDPBootAUX(io,boot,c.bridge,c.mainLink[ddi],ddi,true).status==Display13Power::Status::Complete);
  assert(Display13Power::publishObject(c.bridge,c.power,0x80,f.ctl,sizeof(f.ctl)));
  IOWorkLoop loop;loop.size=sizeof(loop);IOFilterInterruptEventSource irq;
  irq.provider=f.ctl;irq.loop=&loop;irq.enabled=true;put(f.ctl,0xe20,&irq);
  state.orgAlderLakeNSetupWorkLoop=reinterpret_cast<uintptr_t>(nativeSetup);
  c.pipe[0].memoryFramebuffer=reinterpret_cast<IOService*>(f.fb);
  put(f.fb,0x44de,uint8_t(1));put(f.fb,0x1e7,uint8_t(1));put(f.fb,0x3fd8,0U);
  put(f.fb,0x4a00,1U);put(f.fb,0x4a08,f.path+0);
  put(f.port,0x550,mode);if(foreign)put(f.port,0x578,static_cast<void*>(f.fb));
  memoryResult={};memoryResult.status=Display13FBMemory::Status::Complete;
  memoryResult.control=0x94000008;memoryResult.stride=120;memoryResult.size=0x0437077f;
  put(f.plane,0x100,memoryResult.control);put(f.plane,0x118,memoryResult.stride);put(f.plane,0x11c,memoryResult.size);
  XE::wrapAlderLakeNSetupWorkLoop(reinterpret_cast<IOService*>(f.ctl),&loop);
  const bool valid=mode<2&&!foreign;
  assert(c.bridge.bootAdopted==valid && c.bridge.state.faulted==!valid);
  if(valid){
   assert(c.mainLink[ddi].owner==reinterpret_cast<uintptr_t>(f.port)&&!c.mainLink[ddi].bootPending);
   assert(c.bridge.bootPath==reinterpret_cast<uintptr_t>(f.path)&&objectRefs[f.port]==1);
   // Native first mode change stops the inherited buffer, then can reacquire
   // the same AUX owner for normal training/enable. Mode0 is not rewritten.
   assert(!XE::wrapDisplay13DisablePort(f.port));assert(!c.mainLink[ddi].owner);
   assert(!objectRefs.count(f.port));assert(!XE::wrapDisplay13EnablePort(f.port));
   assert(!XE::wrapDisplay13DisablePort(f.port));assert(!objectRefs.count(f.port));
   assert(Display13FBMemory::field<unsigned>(f.port,0x550)==mode);
  } else assert(!c.mainLink[ddi].owner&&!objectRefs.count(f.port));
  assert(!locked);
 }
 puts("PASS production workloop handoff, protected boot AUX adoption and first native port stop/enable; physical training not modeled");
}
'''
with tempfile.TemporaryDirectory(prefix='display13-handoff-') as d:
 p=Path(d);(p/'test.cpp').write_text(base+main)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
