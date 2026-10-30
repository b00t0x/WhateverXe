#!/usr/bin/env python3
"""Replay the bootstrap command trial's real ownership and MMIO boundary."""
from pathlib import Path
from production_source import read_source
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
body=re.search(r'bool XE::probeDSICommands\(.*?\n\}',s,re.S)[0]
code=r'''
#include "kern_dsi_commands.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
unsigned level=0;struct IOService{};struct IOLock{};struct IORecursiveLock{};
void IORecursiveLockLock(IORecursiveLock*){assert(!level);level=1;}
void IORecursiveLockUnlock(IORecursiveLock*){assert(level==1);level=0;}
void IOLockLock(IOLock*){assert(level==1);level=2;}
void IOLockUnlock(IOLock*){assert(level==2);level=1;}
void IODelay(unsigned){assert(level==2);}
struct IOMemoryMap {std::vector<uint32_t> bar=std::vector<uint32_t>(0x1000000/4);
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(bar.data());}};
struct Display13FBBootIO {IOMemoryMap *map;bool readable(uint32_t r){return map && r<0x1000000 && !(r&3);}
 uint32_t read(uint32_t r){assert(level==2 && readable(r));return map->bar[r/4];}};
struct Profile {struct {unsigned clockKHz=135255;unsigned htotal() const {return 1126;}unsigned vtotal() const {return 2002;}}mode;bool valid(){return true;}};
struct Boot {struct {struct {unsigned pll=1;}identity;unsigned conf=0xC0000000;}source;};
bool unchanged=true;int xelpMemory;
namespace XELP {namespace DSI {template<class IO>bool bootUnchanged(IO&,const Boot&,int){return unchanged;}}}
struct {IORecursiveLock *lock;uintptr_t thread=0;struct {uintptr_t external[3]{};unsigned binding[3]{};}allocation;}comboClockTransaction;
struct {IOLock *lock;IOService *controller;IOMemoryMap *map;
 bool engineEnabled=false,memoryFaulted=false,clockFaulted=false;
 struct {struct {bool faulted=false,suspended=false;}state;bool held=true;
 template<class IO>bool outputHeld(IO&,unsigned,unsigned){assert(level==2);return held;}}bridge;}display13FBPower;
uint64_t receipt[20]{};unsigned published=0;
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *key,const void *p,unsigned size){assert(!level && !strcmp(key,"WEXDSICommandTrial") && size==sizeof(receipt));memcpy(receipt,p,size);++published;}};
struct XE {IOService *adlnTopologyController;bool dsiResourcesReady=true,dsiStopAttempted=false;
 struct {unsigned dsi=1;}adlnOutputWiring;Profile dsiProfile;Boot dsiBoot;XELP::DSI::Host dsiCommandHost;
 bool probeDSICommands(IOService*);};
BODY
int main(){
 IOService controller;IOLock owner;IORecursiveLock clock;IOMemoryMap map;
 auto &c=display13FBPower;c.lock=&owner;c.controller=&controller;c.map=&map;
 auto &k=comboClockTransaction;k.lock=&clock;k.allocation.external[0]=reinterpret_cast<uintptr_t>(&controller);k.allocation.binding[0]=2;
 map.bar[0x45404/4]=0x403;map.bar[0x6B0D0/4]=0x1040;
 XE xe;xe.adlnTopologyController=&controller;
 c.engineEnabled=true;assert(!xe.probeDSICommands(&controller) && !map.bar[0x6B100/4]);c.engineEnabled=false;
 unchanged=false;assert(!xe.probeDSICommands(&controller) && !map.bar[0x6B100/4]);unchanged=true;
 map.bar[0x6B0D8/4]=1;assert(!xe.probeDSICommands(&controller) && !map.bar[0x6B100/4]);map.bar[0x6B0D8/4]=0;
 assert(xe.probeDSICommands(&controller) && map.bar[0x6B100/4]==0x40000005 && !level);
 assert(receipt[1] && receipt[4] && receipt[5] && receipt[6] && receipt[9]);
 assert(receipt[7]==0x1040 && !receipt[8] && !receipt[10] && !receipt[11] && receipt[12]==2 && !receipt[13] && receipt[15]==2);
 assert(!xe.probeDSICommands(&controller) && receipt[15]==0); // no replay
 unsigned nonzero=0;for(auto word:map.bar)nonzero+=bool(word);assert(nonzero==3);
 puts("PASS DSI command trial: owner/clock locks, no writes before admission, unchanged source, bootstrap-only and one-shot");
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='dsi-command-adapter-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
