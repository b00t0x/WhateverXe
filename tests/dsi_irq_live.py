#!/usr/bin/env python3
"""Exercise the production memory-only DSI IRQ snapshot and lock lifetime."""
from pathlib import Path
from production_source import read_source
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=read_source(root/'WhateverXe/kern_xe.cpp')
parts=[re.search(pattern,s,re.S)[0] for pattern in [
 r'struct Display13FBIRQContext \{.*?\n\};',
 r'static void adlnIRQRecord\(.*?\n\}',
 r'static bool dsiIRQLiveSerialize\(.*?\n\}']]
code=r'''
#include "kern_display13_fb_irq.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
struct IOSimpleLock {};struct IOService {};struct OSSerialize {};
bool held=false,failAllocation=false,failSerialize=false;unsigned reads=0,releases=0;
uint64_t output[32]{};
unsigned IOSimpleLockLockDisableInterrupt(IOSimpleLock *p){assert(p && !held);held=true;++reads;return 7;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *p,unsigned v){assert(p && held && v==7);held=false;}
struct OSData {
 static OSData *withBytes(const void *p,unsigned n){assert(!held && n==sizeof(output));memcpy(output,p,n);static OSData data;return failAllocation?nullptr:&data;}
 bool serialize(OSSerialize*){assert(!held);return !failSerialize;}
 void release(){assert(!held);++releases;}
};
CONTEXT
static Display13FBIRQContext display13FBIRQ;
RECORD
SERIALIZE
int main(){
 auto &c=display13FBIRQ;OSSerialize serializer;
 assert(!dsiIRQLiveSerialize(nullptr,nullptr,&serializer) && !reads);
 IOService controller;c.controller=&controller;
 assert(!dsiIRQLiveSerialize(nullptr,nullptr,&serializer) && !reads);
 IOSimpleLock lock;c.lock=&lock;c.state.begun=true;c.state.enabled=true;
 c.status=Display13FBIRQ::Status::Complete;c.state.active=1;c.state.configured=1;
 c.state.interrupts=100;c.state.vblanks[0]=70;c.state.flips[0]=20;
 c.state.underruns[0]=2;c.state.dsbErrors[2]=4;c.handlers=1;
 auto before=c;
 assert(dsiIRQLiveSerialize(nullptr,nullptr,&serializer) && !held && reads==1 && releases==1);
 assert(!memcmp(&before,&c,sizeof(c)));
 assert(output[0]==1 && output[1]==3 && output[2]==1 && output[4]==1);
 assert(output[10]==100 && output[11]==70 && output[14]==20 && output[17]==2 && output[22]==4 && output[30]==1);
 c.state.interrupts+=10;c.state.vblanks[0]+=8;c.state.flips[0]+=2;
 assert(dsiIRQLiveSerialize(nullptr,nullptr,&serializer));
 assert(output[10]==110 && output[11]==78 && output[14]==22 && output[17]==2);
 c.quiescing=true;c.blockedPipes=1;c.softwareHandlers=2;c.state.enabled=false;
 assert(dsiIRQLiveSerialize(nullptr,nullptr,&serializer));
 assert(output[4]==0 && output[31]==(1ULL|256|(2ULL<<32)));
 failAllocation=true;assert(!dsiIRQLiveSerialize(nullptr,nullptr,&serializer) && !held && releases==3);
 failAllocation=false;failSerialize=true;assert(!dsiIRQLiveSerialize(nullptr,nullptr,&serializer) && !held && releases==4);
 puts("PASS DSI IRQ live: current counters, read-only state, unlock before allocation/serialization, failure cleanup");
}
'''.replace('CONTEXT',parts[0]).replace('RECORD',parts[1]).replace('SERIALIZE',parts[2])
with tempfile.TemporaryDirectory(prefix='dsi-irq-live-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
