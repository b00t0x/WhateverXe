#!/usr/bin/env python3
"""Execute the production brightness adapter across retained display-off."""
from pathlib import Path
from production_source import read_source
import re, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
s=read_source(r/'WhateverXe/kern_xe.cpp')
body='\n'.join(re.search(r'uint32_t XE::'+name+r'\(.*?\n\}',s,re.S)[0] for name in ['setDSIBrightness','saveDSIBrightness'])
code=r'''
#include "kern_dsi_backlight.hpp"
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
struct IOService {alignas(8) unsigned char bytes[0x4B90]{};};
namespace Display13FBMemory {template<class T>T field(void *p,unsigned offset){T v;memcpy(&v,(char*)p+offset,sizeof(v));return v;}}
struct IOMemoryMap {std::vector<uint32_t> words=std::vector<uint32_t>(0x1000000/4);uintptr_t getVirtualAddress(){return (uintptr_t)words.data();}};
struct Display13FBBootIO {IOMemoryMap *map;bool readable(uint32_t r){return map && r<0x1000000;}uint32_t read(uint32_t r){return map->words[r/4];}};
unsigned depth=0,heldQueries=0;using IOLock=int;
void IOLockLock(IOLock*){assert(!depth);++depth;}void IOLockUnlock(IOLock*){assert(depth==1);--depth;}
struct Context {
 IOLock *lock=nullptr;IOMemoryMap *map=nullptr;void *controller=nullptr;
 bool memoryFaulted=false,clockFaulted=false,watermarkFaulted=false;
 struct {struct {bool faulted=false,suspended=false;}state;bool outputHeld(Display13FBBootIO&,int,int){assert(depth);++heldQueries;return true;}}bridge;
 struct {bool faulted=false;}dbuf;
 struct {IOService *memoryFramebuffer=nullptr;bool destroyingPath=false;}pipe[1];
}display13FBPower;
bool mapping=true,awake=true;IOService *target=nullptr;
bool adlnWriterMappingBound(){return mapping;}bool dsiModeTarget(IOService *fb){return fb==target;}
constexpr uint32_t kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotReady=2;
struct XE {
 XELP::DSI::Backlight dsiBacklight;bool dsiResourcesReady=true,dsiPowerTest=true,dsiStopAttempted=false;
 bool dsiIRQParked=false,dsiMemoryReleased=false,dsiDDIReleased=false;void *adlnTopologyController=nullptr;
 struct {bool sleeping=false,failed=false;}dsiRuntimePower;
 struct {bool done=false;bool complete(){return done;}}dsiStop;
 struct {bool complete=false;}dsiPanelStop;
 unsigned published=0;void publishDSIBacklight(){assert(!depth);++published;}
 bool dsiNativeUpdateAllowed(IOService*){assert(!depth);return awake;}
 uint32_t setDSIBrightness(IOService*,uintptr_t);
 uint32_t saveDSIBrightness(IOService*,uintptr_t);
};
BODY
int main(){
 IOService fb,controller;IOLock lock;IOMemoryMap map;XE xe;auto &c=display13FBPower;
 c.lock=&lock;c.map=&map;c.controller=&controller;c.pipe[0].memoryFramebuffer=&fb;
 xe.adlnTopologyController=&controller;target=&fb;memcpy(fb.bytes+0x1D0,&c.controller,sizeof(void*));
 XELP::DSI::Profile p;p.status=XELP::DSI::ProfileStatus::Valid;p.precision=8;p.minimum=6;
 using Light=XELP::DSI::Backlight;
 map.words[Light::Control/4]=0x80000000;map.words[Light::Period/4]=480;map.words[Light::Duty/4]=154;
 assert(xe.dsiBacklight.capture(p,0x80000000,480,154));
 assert(xe.setDSIBrightness(&fb,32768)==0 && map.words[Light::Duty/4]==240);
 assert(Display13FBMemory::field<uint32_t>(&controller,0xE58)==32768);
 awake=false;xe.dsiRuntimePower.sleeping=true;xe.dsiStopAttempted=xe.dsiIRQParked=xe.dsiStop.done=xe.dsiPanelStop.complete=true;
 auto queries=heldQueries,writes=xe.dsiBacklight.writes;
 assert(xe.setDSIBrightness(&fb,65535)==0 && map.words[Light::Duty/4]==240);
 assert(heldQueries==queries && xe.dsiBacklight.writes==writes);
 assert(Display13FBMemory::field<uint32_t>(&controller,0xE58)==65535 && xe.dsiBacklight.requested==65535);
 assert(xe.saveDSIBrightness(&fb,12345)==0);
 assert(Display13FBMemory::field<uint32_t>(&controller,0xE5C)==12345);
 assert(xe.dsiBacklight.requested==65535 && xe.dsiBacklight.writes==writes);
 assert(xe.saveDSIBrightness(&fb,0x100000001ULL)==kIOReturnBadArgument);
 xe.dsiDDIReleased=true;assert(xe.setDSIBrightness(&fb,0)==kIOReturnNotReady);
 xe.dsiDDIReleased=false;c.bridge.state.suspended=true;assert(xe.setDSIBrightness(&fb,0)==kIOReturnNotReady);
 c.bridge.state.suspended=false;xe.dsiRuntimePower.failed=true;assert(xe.setDSIBrightness(&fb,0)==kIOReturnNotReady);
 assert(xe.setDSIBrightness(&fb,0x100000001ULL)==kIOReturnBadArgument && !depth);
 puts("PASS actual DSI brightness adapter: active PWM, native readback, deferred value without MMIO, retired/suspended/failed rejection");
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='dsi-backlight-adapter-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
