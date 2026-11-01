#!/usr/bin/env python3
"""Exercise the production boot adoption transaction; mode setter is a fault-injectable leaf."""
from pathlib import Path
from production_source import read_source
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
body=re.search(r'bool XE::adoptDSIBootPath\(.*?\n\}',s,re.S)[0]
binding=re.search(r'static TGLFBBinding::Readout adlnInspectPathBinding\(.*?\n\}',s,re.S)[0]
boot=(root/'tests/dsi_boot.cpp').read_text();io=boot[boot.index('struct IO {'):boot.index('Boot capture(')]
# Reuse the exact native object graph and lease setup from the cache fixture.
base=(root/'tests/dsi_native_cache.py').read_text().split("code=r'''",1)[1].split("'''.replace('IO_FIXTURE'",1)[0]
base=base[:base.index(' const auto unchanged=')]
base=base.replace('#include "kern_dsi_native_cache.hpp"','#include "kern_dsi_native_cache.hpp"\n#include "kern_display13_fb_irq.hpp"')
base=base.replace('unsigned size=sizeof(bytes);','unsigned size=sizeof(bytes);bool readyProperty=true;void setProperty(const char*,bool b){readyProperty=b;}')
base=base.replace('struct IOLock{};struct IORecursiveLock{};','''struct IOLock{};struct IORecursiveLock{};struct IOSimpleLock{};
unsigned IOSimpleLockLockDisableInterrupt(IOSimpleLock*){assert(level==3);level=4;return 1;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock*,unsigned v){assert(v==1 && level==4);level=3;}''')
base=base.replace('IOLock *lock;IOService *controller;OSObject *access,*dsiPort;IO *map;','IOLock *lock;IOService *controller;OSObject *access,*dsiPort,*power;IO *map;\n struct {IOService *memoryFramebuffer;} pipe[3];')
base=base.replace('BINDING','''struct {IOSimpleLock *lock;IOService *controller;uint32_t admittedPipes=1;bool quiescing=false;Display13FBIRQ::State state;} display13FBIRQ;
struct {IOService *framebuffer=nullptr;bool ready=false;} dsiFixedMode;
bool dsiModeTarget(IOService *fb){return dsiFixedMode.framebuffer==fb;}
Display13FBMemory::Readout measuredMemory;
Display13FBMemory::Readout display13FBInspectMemory(IOService*,bool boot,unsigned pipe,bool publish){assert(level==3 && boot && !pipe && !publish);return measuredMemory;}
BINDING''')
base=base.replace('bool dsiCacheHooksReady=true,dsiResourcesReady=true;','bool dsiCacheHooksReady=true,dsiResourcesReady=true,dsiBootProbeReady=true;Profile dsiProfile=profile();')
base=base.replace(' bool updateDSIBootCache(', ' bool adoptDSIBootPath(IOService *,OSObject *);\n bool bindDSIFixedMode(IOService *,const Profile &);\n bool updateDSIBootCache(')
base=base.replace('FUNCTIONS','''unsigned modeCalls=0;bool publishMode=true,changeSource=false;
bool XE::bindDSIFixedMode(IOService *fb,const Profile&){assert(level==1);++modeCalls;
 dsiFixedMode.framebuffer=fb;dsiFixedMode.ready=publishMode;fb->setProperty("WEXDSIFixedModeReady",publishMode);
 if(changeSource)display13FBPower.map->regs[0x6B000]^=1;
 return publishMode;
}
FUNCTIONS''')
base+=r'''
 IOSimpleLock irqLock;OSObject power;
 c.power=&power;put(&controller,0xC8,&power);c.pipe[0].memoryFramebuffer=&fb;
 put(&fb,0x4A00,1U);put(&fb,0x4A08,&path);put(&fb,0x44DE,uint8_t(1));put(&fb,0x1E7,uint8_t(1));put(&path,0x3644,2U);
 b.bootPort=0;b.bootPGProtected=b.bootDDIProtected=true;
 measuredMemory.status=Display13FBMemory::Status::Complete;measuredMemory.control=xe.dsiBoot.control;
 measuredMemory.stride=xe.dsiBoot.stride;measuredMemory.size=xe.dsiBoot.size;measuredMemory.surface=xe.dsiBoot.surface;
 auto &irq=display13FBIRQ;irq.lock=&irqLock;irq.controller=&controller;irq.state.begun=irq.state.enabled=true;irq.state.transcoders.dsi0Pipe=0;
 const auto rejected=[&]{const auto calls=modeCalls;assert(!xe.adoptDSIBootPath(&fb,&path));
  assert(modeCalls==calls && c.memoryFaulted && !b.bootAdopted && !b.bootPath && b.bootPGProtected && b.bootDDIProtected && !level);
  c.memoryFaulted=false;};
 irq.state.enabled=false;rejected();irq.state.enabled=true;
 irq.quiescing=true;rejected();irq.quiescing=false;
 measuredMemory.surface=4096;rejected();measuredMemory.surface=0;
 put(&fb,0x4A08,&plane);rejected();put(&fb,0x4A08,&path);
 b.ddiRefs[0]=0;rejected();b.ddiRefs[0]=1;
 publishMode=false;assert(!xe.adoptDSIBootPath(&fb,&path) && modeCalls==1 && c.memoryFaulted);
 assert(!b.bootAdopted && b.bootPGProtected && b.bootDDIProtected && dsiModeTarget(&fb) && !dsiFixedMode.ready && !fb.readyProperty);
 c.memoryFaulted=false;publishMode=true;changeSource=true;
 assert(!xe.adoptDSIBootPath(&fb,&path) && modeCalls==2 && !dsiFixedMode.ready && !b.bootAdopted);
 assert(b.pgRefs[0]==1 && b.ddiRefs[0]==1 && b.bootPGProtected && b.bootDDIProtected);
 c.memoryFaulted=false;changeSource=false;io.regs[0x6B000]^=1;
 assert(xe.adoptDSIBootPath(&fb,&path) && dsiFixedMode.ready && b.bootAdopted && b.bootPath==reinterpret_cast<uintptr_t>(&path));
 assert(!b.bootPGProtected && !b.bootDDIProtected && b.pgRefs[0]==1 && b.ddiRefs[0]==1);
 assert(Display13FBMemory::field<void*>(&power,0x10)==&controller && Display13FBMemory::field<void*>(&power,0x78)==&access);
 assert(xe.adoptDSIBootPath(&fb,&path) && b.pgRefs[0]==1 && b.ddiRefs[0]==1);
 b.bootPath=reinterpret_cast<uintptr_t>(&plane);
 assert(!xe.adoptDSIBootPath(&fb,&path) && c.memoryFaulted && !dsiFixedMode.ready);
 assert(b.bootPath==reinterpret_cast<uintptr_t>(&plane) && b.pgRefs[0]==1 && b.ddiRefs[0]==1 && !level && io.writes.empty());
 puts("PASS production DSI adoption: real path/leases/IRQ and memory prerequisites, mode publication outside owner lock, revalidation, failure retention and single boot transfer");
}
'''
code=base.replace('IO_FIXTURE',io).replace('BINDING',binding).replace('FUNCTIONS',body)
with tempfile.TemporaryDirectory(prefix='dsi-boot-adoption-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
