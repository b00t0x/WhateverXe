#!/usr/bin/env python3
"""Replay the production port-state boundary, including the normal native path."""
from pathlib import Path
from production_source import read_source
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=read_source(r/'WhateverXe/kern_xe.cpp')
body=re.search(r'uint32_t XE::wrapAlderLakeNPortState\(.*?\n\}',s,re.S).group()
code=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
constexpr uint32_t kIOReturnSuccess=0;
struct OSObject { alignas(8) unsigned char bytes[0x580]{}; OSObject *getMetaClass(){return this;}unsigned getClassSize(){return sizeof(bytes);} };
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}void setProperty(const char *,bool){}void setProperty(const char *,const void *,unsigned){}};
namespace Display13FBMemory {template<class T>T &field(void *p,unsigned off){return *reinterpret_cast<T*>((char*)p+off);}}
struct Wiring {unsigned dsi=0;unsigned selected(){return 8;}};
namespace Display13FBTopology {constexpr unsigned Ports=9;bool selected(void *p,uint32_t *c,Wiring){return c==(uint32_t*)p;}bool bound(void *,uint32_t *,unsigned){return false;}}
static bool display13Offline;static unsigned calls,binds;
uint32_t native(OSObject *){++calls;return 1;}
#define FunctionCast(type,fn) fn
struct XE {static uint32_t setDSIPortMode(OSObject*,uint32_t){assert(false);return 1;}bool display13FBTopologyHooksReady=true;void *adlnTopologyController=(void*)1;void *adlnPlatform;Wiring adlnOutputWiring;uint32_t (*orgAlderLakeNPortState)(OSObject*)=native;bool xelpBindProbe(void*){++binds;return true;}static uint32_t wrapAlderLakeNPortState(OSObject*);};
static XE *callbackXE;
BODY
int main(){XE x;callbackXE=&x;uint32_t config[3]={3,9,1};x.adlnPlatform=config;OSObject port;auto cp=config;memcpy(port.bytes+0x548,&cp,sizeof(cp));uint32_t connected=4;memcpy(port.bytes+0x554,&connected,4);
 display13Offline=true;assert(XE::wrapAlderLakeNPortState(&port)==0);assert(!calls && !binds);assert(Display13FBMemory::field<uint32_t>(&port,0x554)==0);
 display13Offline=false;assert(XE::wrapAlderLakeNPortState(&port)==1);assert(calls==1 && binds==1);
 display13Offline=true;x.display13FBTopologyHooksReady=false;assert(XE::wrapAlderLakeNPortState(&port)==0);assert(calls==1 && binds==1);
 puts("PASS production offline boundary: disconnected state, no provider/native probe, normal path preserved");}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='display13-offline-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
