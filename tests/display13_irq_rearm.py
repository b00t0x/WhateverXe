#!/usr/bin/env python3
"""Production IRQ enable/disable/VBL wrappers and bank effects.

Native enablePipe writes PIPECONF before VBL (fixed donor 0x49193/0x491ad).
Native calls/association validation and locks are OS boundaries; the IRQ state
machine and MMIO register model are real existing test code. No real IRQ timing.
"""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
names=['wrapDisplay13FBEnableIRQ','wrapDisplay13FBDisableIRQ','wrapDisplay13FBConfigureVBL']
methods='\n'.join(re.search(r'(?:IOReturn|void) XE::'+n+r'\(.*?\n\}',s,re.S)[0] for n in names)
base=(r/'tests/display13_fb_irq.cpp').read_text().split('int main()')[0]
code='#include "kern_display13_fb_memory.hpp"\n#include "kern_display13_fb_topology.hpp"\n'+base+r'''
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_fb_topology.hpp"
struct OSObject {
 alignas(8) uint8_t data[0x3800]{};
 OSObject* getMetaClass(){return this;}size_t getClassSize(){return sizeof(data);}
 template<class...T>void setProperty(T...){}
};using IOService=OSObject;using IOReturn=uint32_t;
struct IORegistryEntry:OSObject {static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}};
constexpr unsigned kIOReturnNotReady=1,kIOReturnSuccess=0;
bool locked=false;unsigned IOSimpleLockLockDisableInterrupt(void*){assert(!locked);locked=true;return 0;}
void IOSimpleLockUnlockEnableInterrupt(void*,unsigned){assert(locked);locked=false;}
struct {IO *base;State state;void *lock=nullptr;bool quiescing=false,powerLost=false,rearmPending=false;
 uint32_t admittedPipes=7,blockedPipes=0;Status status=Status::Unavailable;}display13FBIRQ;
struct Display13FBIRQIO {
 IO *io;bool readable(unsigned r){return io->readable(r);}uint32_t read(unsigned r){assert(locked);return io->read(r);}
 void write(unsigned r,unsigned v){assert(locked);io->write(r,v);}
};
IOService *owner=nullptr,*framebuffer=nullptr;
bool adlnIRQOwner(IOService*p){return p==owner;}
bool display13FBAssociations(IOService*c,unsigned,IOService*f,size_t,uintptr_t){return c==owner && f==framebuffer;}
uintptr_t display13FBExpectedOwner(IOService*){return 1;}
uint32_t adlnSharedGT(IOService*){return NativeGT;}
void adlnIRQCache(IOService*,Display13FBIRQIO&){}void adlnIRQRecord(uint64_t*,unsigned){}
struct XE {
 bool display13FBIRQReady=true;IOService *adlnTopologyController=nullptr;
 uint8_t adlnPlatform[0x1c0]{};Display13OutputWiring adlnOutputWiring;
 static IOReturn wrapDisplay13FBEnableIRQ(IOService*);static IOReturn wrapDisplay13FBDisableIRQ(IOService*);
 static void wrapDisplay13FBConfigureVBL(IOService*,IOService*,OSObject*,bool,bool);
}*callbackXE;
METHODS
int main(){
 for(bool allPowered:{false,true})for(bool multi:{false,true})for(bool lost:{false,true})for(unsigned pipe:{0U,1U,2U}) {
  if((!multi || !allPowered) && pipe)continue;
  IO io;if(!allPowered)io.wells=0xc0f;IOService ctl,fb,other;OSObject path;XE xe;callbackXE=&xe;owner=&ctl;framebuffer=&fb;
  xe.adlnOutputWiring.valid=true;xe.adlnOutputWiring.dp=multi?3:2;
  display13FBIRQ={};auto &c=display13FBIRQ;c.base=&io;c.admittedPipes=multi?7:1;
  for(unsigned p=0;p<3;++p)io.r[0x70008+0x1000*p]=p==pipe?0xc0000000:0;
  assert(enable(io,c.state,0,NativeGT,1U<<pipe,xe.adlnOutputWiring.hotplug())==Status::Complete);
  assert(XE::wrapDisplay13FBDisableIRQ(&ctl)==0);c.powerLost=lost;
  io.r[0x70008+0x1000*pipe]=0;
  assert(XE::wrapDisplay13FBEnableIRQ(&ctl)==kIOReturnNotReady && !c.state.faulted && c.rearmPending);
  *reinterpret_cast<uint32_t*>(path.data+0x3640)=pipe;
  c.quiescing=true;io.r[0x70008+0x1000*pipe]=0xc0000000;
  XE::wrapDisplay13FBConfigureVBL(&ctl,&fb,&path,true,true);assert(!c.state.enabled && c.rearmPending);
  c.quiescing=false;c.blockedPipes=1U<<pipe;
  XE::wrapDisplay13FBConfigureVBL(&ctl,&fb,&path,true,true);assert(!c.state.enabled);
  c.blockedPipes=0;XE::wrapDisplay13FBConfigureVBL(&other,&fb,&path,true,true);assert(!c.state.enabled);
  XE::wrapDisplay13FBConfigureVBL(&ctl,&fb,&path,true,true);
  assert(c.state.enabled && !c.state.faulted && !c.rearmPending && !c.powerLost);
  assert(io.r[Master]&Enable);assert(io.r[PipeIIR(pipe)+4]&1);assert(!(io.r[PipeIIR(pipe)-4]&1));
  const auto before=io.writes.size();XE::wrapDisplay13FBConfigureVBL(&ctl,&fb,&path,true,true);
  assert(c.state.enabled && !c.state.faulted && io.writes.size()>=before);
 }
 // Connector identity must not change the shared resume contract. Use a
 // full stop/resume followed by an independently waking peer. The second
 // callback must not ACK pending peer completions or reset a running bank.
 for(unsigned transport=0;transport<5;++transport)for(unsigned first=0;first<3;++first)for(bool getLock:{false,true}) {
  IO io;IOService ctl,fb;OSObject path;XE xe;callbackXE=&xe;owner=&ctl;framebuffer=&fb;
  xe.adlnOutputWiring.valid=true;
  switch(transport) {
   case 0:xe.adlnOutputWiring.dp=3;break; // Combo DP
   case 1:xe.adlnOutputWiring.hdmi=3;xe.adlnOutputWiring.ddc[0]=1;xe.adlnOutputWiring.ddc[1]=2;break;
   case 2:xe.adlnOutputWiring.dp=0x18;xe.adlnOutputWiring.typeC=0x18;break;
   case 3:xe.adlnOutputWiring.panel=1;xe.adlnOutputWiring.dp=8;break;
   case 4:xe.adlnOutputWiring.dsi=1;xe.adlnOutputWiring.dp=8;break;
  }
  assert(xe.adlnOutputWiring.described());
  display13FBIRQ={};auto &c=display13FBIRQ;c.base=&io;c.admittedPipes=7;
  if(transport==4) {c.state.transcoders.dsi0Pipe=first;io.r[0x7B008]=0;io.r[0x6B400]=(first?first+4:0)<<12;}
  for(unsigned p=0;p<3;++p)io.r[0x70008+0x1000*p]=0;
  const auto reg=c.state.transcoders.control(first);
  io.r[reg]=0xc0000000;if(transport==4)io.r[0x6B400]|=0x80000000;
  assert(enable(io,c.state,0,NativeGT,1U<<first,xe.adlnOutputWiring.hotplug(),xe.adlnOutputWiring.typeC)==Status::Complete);
  assert(!XE::wrapDisplay13FBDisableIRQ(&ctl));io.r[reg]=0;
  assert(XE::wrapDisplay13FBEnableIRQ(&ctl)==kIOReturnNotReady && c.rearmPending && !c.state.faulted);
  *reinterpret_cast<uint32_t*>(path.data+0x3640)=first;
  auto vbl=[&](bool on){locked=!getLock;XE::wrapDisplay13FBConfigureVBL(&ctl,&fb,&path,on,getLock);if(!getLock){assert(locked);locked=false;}else assert(!locked);};
  vbl(false);assert(!c.state.faulted && c.rearmPending && !c.state.enabled);
  io.r[reg]=0xc0000000;vbl(true);assert(c.state.enabled && !c.state.faulted && !c.rearmPending);
  const auto peer=(first+1)%3;io.r[0x70008+0x1000*peer]=0xc0000000;
  io.r[PipeIIR(first)]|=8;io.r[DSBInterrupt(first,0)]|=2;
  const auto peerMask=io.r[PipeIIR(first)-4],peerEnable=io.r[PipeIIR(first)+4];
  const auto before=io.writes.size();*reinterpret_cast<uint32_t*>(path.data+0x3640)=peer;vbl(true);
  assert(!c.state.faulted && c.state.enabled && c.state.active==((1U<<first)|(1U<<peer)));
  assert(io.r[PipeIIR(first)]==8 && (io.r[DSBInterrupt(first,0)]&2));
  assert(io.r[PipeIIR(first)-4]==peerMask && io.r[PipeIIR(first)+4]==peerEnable);
  for(size_t n=before;n<io.writes.size();++n)assert(io.writes[n]!=Master && io.writes[n]!=GfxMaster && io.writes[n]!=0xC4008);
  if(transport==4)assert(!(io.r[DDIControl]&8)); // DSI reset GPIO never becomes HPD-A.
 }
 for(unsigned bad=0;bad<4;++bad) {
  IO io;State s;assert(enable(io,s)==Status::Complete);assert(disable(io,s)==Status::Complete);io.r[0x70008]=0;
  if(bad==0)io.wells=~0U;if(bad==1)io.wells=0;if(bad==2)io.r[0x70008]=~0U;if(bad==3)io.r[0x70008]=0x40000000;
  assert(!deferRearm(io,s,1));
 }
 puts("PASS production IRQ deferred enable -> VBL -> active bank, single/multi pipe and quiesce/owner/power boundaries");
}
'''
with tempfile.TemporaryDirectory(prefix='irq-rearm-') as d:
 p=Path(d);(p/'test.cpp').write_text(code.replace('METHODS',methods))
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
