#!/usr/bin/env python3
"""Replay native pending scanout allocation/free; allocator leaves are mocked.

Requires the exact accelerator; stores no Apple bytes. This establishes record
and display-pipe reference behavior, not GPU allocation or live PTE validity.
"""
from pathlib import Path
import hashlib, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
donor=Path(sys.argv[1]).resolve()
assert hashlib.sha256(donor.read_bytes()).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
code=r'''
#include "kern_dsi_native_backing.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <sys/mman.h>
using namespace XELP::DSI;
template<class T>void put(void *p,unsigned off,T v){memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
template<class T>T get(const void *p,unsigned off){return Display13FBMemory::field<T>(p,off);}
alignas(8) uint8_t owner[0x1500]{},pipe[0x1300]{},memory[0x198]{},mapping[0x128]{};
uintptr_t memVT[64]{},mapVT[64]{};
unsigned allocations=0,releases=0,unmaps=0;bool allocationFails=false,mapFails=false;
void *allocate(void *o,void *,void *shared,void *resource,uint32_t options,uint64_t bytes){
 assert(o==owner && !shared && !resource && options==0x23 && bytes==8355840);++allocations;
 return allocationFails?nullptr:memory;
}
void *createMap(void *m,void *addressSpace,uint32_t flags){
 assert(m==memory && addressSpace==reinterpret_cast<void*>(0x1234) && flags==0x800000B);
 return mapFails?nullptr:mapping;
}
uint64_t address(void *p){assert(p==mapping);return 0x10000000;}
bool ready(void *p){assert(p==mapping);return true;}
void release(void *p){assert(p==mapping || p==memory);++releases;}
void unmap(void *p){assert(p==mapping);++unmaps;}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream file(argv[1],std::ios::binary);
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
 constexpr size_t size=0x2000000;assert(bytes.size()<size/2);
 auto image=static_cast<uint8_t*>(mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(image!=MAP_FAILED);memcpy(image,bytes.data(),bytes.size());
 // Replace the imported IOAccelSysMemory::withOptions call with a bounded
 // allocation leaf; execute native allocation/map/publication/free bodies.
 constexpr unsigned stub=0x1000000,call=0x7F674;
 assert(image[call]==0xE8);image[stub]=0x48;image[stub+1]=0xB8;
 put(image,stub+2,reinterpret_cast<uintptr_t>(allocate));image[stub+10]=0xFF;image[stub+11]=0xE0;
 put(image,call+1,int32_t(stub-call-5));
 uint64_t pageSize=4096,kernelTask=0;
 put(image,0xC8238,reinterpret_cast<uintptr_t>(&pageSize));put(image,0xC8228,reinterpret_cast<uintptr_t>(&kernelTask));
 put(pipe,0x88,owner);put(owner,0x1470,3U);put(owner,0x150,uintptr_t(0x1234));
 put(memory,0,memVT);put(mapping,0,mapVT);mapping[0x10]=1;
 memVT[0x138/8]=reinterpret_cast<uintptr_t>(createMap);memVT[0x28/8]=reinterpret_cast<uintptr_t>(release);
 mapVT[0x128/8]=reinterpret_cast<uintptr_t>(address);mapVT[0x138/8]=reinterpret_cast<uintptr_t>(ready);
 mapVT[0x140/8]=reinterpret_cast<uintptr_t>(unmap);mapVT[0x28/8]=reinterpret_cast<uintptr_t>(release);
 Profile p;p.status=ProfileStatus::Valid;p.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
 auto alloc=reinterpret_cast<uint32_t(*)(void*,void*,uint32_t)>(image+0x7E5B2);
 auto free=reinterpret_cast<uint32_t(*)(void*,void*,uint32_t)>(image+0x7E772);
 NativeBacking::Record record;assert(NativeBacking::request(p,3,record));
 assert(!alloc(pipe,&record,0));
 assert(record.map==reinterpret_cast<uintptr_t>(mapping) && record.memory==reinterpret_cast<uintptr_t>(memory));
 assert(record.gpu==0x10000000 && record.pitch==4352 && record.refs==1 && !record.bytes);
 assert(get<void*>(pipe,0x1298)==memory && get<void*>(pipe,0x12A0)==mapping && !get<uintptr_t>(pipe,0x12A8));
 assert(!free(pipe,&record,0) && releases==2 && unmaps==1 && !record.refs && !record.map && !record.memory && !record.gpu && !record.pitch);
 assert(!get<uintptr_t>(pipe,0x1298) && !get<uintptr_t>(pipe,0x12A0));
 assert(!free(pipe,&record,0) && releases==2 && unmaps==1);
 allocationFails=true;assert(NativeBacking::request(p,3,record));assert(alloc(pipe,&record,0)==0xE00002BC);
 assert(!record.refs && !record.map && !get<uintptr_t>(pipe,0x1298));
 allocationFails=false;mapFails=true;assert(alloc(pipe,&record,0)==0xE00002BC && releases==3);
 assert(!record.refs && !record.map && !record.memory && !get<uintptr_t>(pipe,0x1298));
 assert(allocations==3);assert(!munmap(image,size));
 puts("PASS exact native DSI Y allocation/free: request geometry, pipe references, failed allocation/map cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='dsi-backing-native-') as directory:
    d=Path(directory);(d/'test.cpp').write_text(code)
    subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-I'+str(root/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test'),str(donor)],check=True)
