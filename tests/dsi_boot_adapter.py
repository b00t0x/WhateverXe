#!/usr/bin/env python3
"""Execute production firmware-copy and DSI pre-native handoff with mock IOKit."""
from pathlib import Path
from production_source import read_source
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=read_source(root/'WhateverXe/kern_xe.cpp')
# Observation must precede the destructive external-output trial and must
# use scratch storage, not publish the certificate to the live DSI owner.
audit_source=re.search(r'void XE::auditDSIBoot\([^;{]*\{.*?\n\}',source,re.S)[0]
assert 'if(!checkKernelArgument("-wex-dsi-audit"))return;' in audit_source
assert 'scratch->boot,scratch->audit' in audit_source and '"WEXDSIBootAudit"' in audit_source
assert 'dsiBoot=' not in audit_source and 'write(' not in audit_source
bootstrap=re.search(r'void XE::adlnBootstrap\([^;{]*\{.*?\n\}',source,re.S)[0]
assert bootstrap.index('auditDSIBoot(pci,') < bootstrap.index('Display13DSITrial::transfer(')
functions=audit_source+'\n'+'\n'.join(re.search(r'bool XE::'+name+r'\(.*?\n\}',source,re.S)[0]
                    for name in ['prepareDSIFirmware','prepareDisplay13BootScanout'])
fixture=(root/'tests/dsi_profile.cpp').read_text()
fixture=fixture[fixture.index('using Bytes='):fixture.index('Profile parse(')]
code=r'''
#include "kern_dsi_boot.hpp"
#include "kern_display13_boot.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace XELP::DSI;
FIXTURE
bool copyOK=true,deviceOK=true,memoryOK=true,auditEnabled=false;unsigned alive=0,releases=0,audits=0,allocations=0;
BootAudit lastAudit;
bool checkKernelArgument(const char *key){assert(!strcmp(key,"-wex-dsi-audit"));return auditEnabled;}
void *IOMalloc(size_t bytes){++allocations;return calloc(1,bytes);}
void IOFree(void *p,size_t){--allocations;free(p);}
struct OSObject {OSObject(){++alive;}virtual ~OSObject(){--alive;}void release(){delete this;}};
struct OSData:OSObject {
 std::vector<uint8_t> bytes;
 unsigned getLength(){return bytes.size();}void *getBytesNoCopy(){return bytes.data();}
 static OSData *withBytes(const void *p,unsigned n){if(!copyOK)return nullptr;auto d=new OSData;
  d->bytes.assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);return d;}
};
struct IOService {OSObject *property=nullptr;OSObject *getProperty(const char *){return property;}
 void setProperty(const char *key,const void *p,unsigned bytes){
  if(!strcmp(key,"WEXDSIBootAudit")){assert(bytes==sizeof(lastAudit));memcpy(&lastAudit,p,bytes);++audits;}
 };};
struct IOMemoryMap {
 std::vector<uint8_t> bytes=std::vector<uint8_t>(0x1000000);
 uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(bytes.data());}
 size_t getLength(){return bytes.size();}void release(){++releases;}
};
struct IOPCIDevice:IOService {IOMemoryMap *map;bool mapped=true;unsigned command=6;
 uint16_t configRead16(unsigned o){assert(o==4);return command;}
 IOMemoryMap *mapDeviceMemoryWithRegister(unsigned reg,unsigned){assert(reg==0x10);return mapped?map:nullptr;}
};
struct IORegistryEntry:IOService {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry p;return &p;}};
constexpr unsigned kIOMapInhibitCache=1;
void IODelay(unsigned){assert(false && "preserved DSI handoff must not poll or reinitialize");}
#define SYSLOG(...)
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
struct XE {
 OSData *dsiFirmware=nullptr;Profile dsiProfile;Boot dsiBoot;bool display13BootReady=true;
 XELPMemory::Layout xelpMemory;Display13OutputWiring adlnOutputWiring;
 bool xelpDisplay13Device(IOPCIDevice *){return deviceOK;}
 bool xelpMemoryMatches(IOPCIDevice *){return memoryOK;}
 void auditDSIBoot(IOPCIDevice *,volatile const uint8_t *,uint64_t,const AlderLakeN::DisplayReadout &,bool);
 bool prepareDSIFirmware(IOService *);
 bool prepareDisplay13BootScanout(IOPCIDevice *);
};
FUNCTIONS
struct IO {
 IOMemoryMap &map;
 bool readable(uint32_t r){return r<map.bytes.size();}
 uint32_t read(uint32_t r){uint32_t v;memcpy(&v,map.bytes.data()+r,4);return v;}
 uint64_t readPTE(uint32_t r){uint64_t v;memcpy(&v,map.bytes.data()+r,8);return v;}
 void word(uint32_t r,uint32_t v){memcpy(map.bytes.data()+r,&v,4);}
};
int main(){
 XE xe;IOService provider;assert(!xe.prepareDSIFirmware(&provider));
 auto bytes=fixture();provider.property=OSData::withBytes(bytes.data(),bytes.size());
 copyOK=false;assert(!xe.prepareDSIFirmware(&provider) && !xe.dsiFirmware && alive==1);copyOK=true;
 assert(xe.prepareDSIFirmware(&provider) && xe.dsiProfile.valid() && alive==2);
 assert(xe.dsiFirmware!=provider.property && xe.prepareDSIFirmware(&provider) && alive==2);
 auto start=static_cast<const uint8_t*>(xe.dsiFirmware->getBytesNoCopy());
 for(auto span:xe.dsiProfile.sequences.sequence)if(span.data)assert(span.data>=start && span.data+span.size<=start+bytes.size());
 auto source=static_cast<OSData*>(provider.property);source->bytes[0]^=1;
 assert(!xe.prepareDSIFirmware(&provider) && xe.dsiProfile.valid() && start[0]=='$');
 source->release();provider.property=nullptr;
 assert(xe.dsiProfile.mode.width==1080 && xe.dsiProfile.sequences.sequence[1].data[0]==0);
 IOMemoryMap map;IOPCIDevice pci;pci.map=&map;IO io{map};
 xe.xelpMemory=XELPMemory::decode(0x03C1,0x7A800000,0);
 assert(XELPMemory::reservation(xe.xelpMemory,xe.xelpMemory.base+xe.xelpMemory.bytes-0x100000));
 const uint32_t regs[][2]={{0x7B008,0xC0000000},{0x6B400,0x80030006},{0x45404,0xFC0F},
  {0x6B030,0x20130300},{0x6B094,1},{0x64000,0x80000000},{0x164280,0x1E07801},
  {0x46014,0xCC000000},{0x16428C,0x3001FB},{0x164290,0xE84},
  {0x6B000,0x04650437},{0x6B008,0x044F044D},{0x6B00C,0x07D1077F},{0x6B014,0x07A907A7},
  {0x51004,0x40000000},{0x70180,0x94000008},{0x70188,68},{0x70190,0x077F0437}};
 for(auto &r:regs)io.word(r[0],r[1]);
 for(unsigned i=0;i<xe.xelpMemory.bytes/4096;++i){const auto pte=xe.xelpMemory.pte(uint64_t(i)*4096);memcpy(map.bytes.data()+0x800000+8*i,&pte,8);}
 auto normal=AlderLakeN::readDisplay(io,true);
 pci.property=OSData::withBytes(bytes.data(),bytes.size());auto auditBefore=map.bytes;
 xe.auditDSIBoot(&pci,map.bytes.data(),map.bytes.size(),normal,true);
 assert(!audits && !allocations);auditEnabled=true;
 xe.auditDSIBoot(&pci,map.bytes.data(),map.bytes.size(),normal,true);
 assert(audits==1 && !allocations && lastAudit.words[1]==uint32_t(BootStatus::Complete));
 assert(map.bytes==auditBefore && xe.dsiBoot.status==BootStatus::Unavailable);
 io.word(0x70B08,1);auditBefore=map.bytes;
 xe.auditDSIBoot(&pci,map.bytes.data(),map.bytes.size(),normal,true);
 assert(audits==2 && !allocations && lastAudit.words[1]==uint32_t(BootStatus::Peers));
 assert(lastAudit.words[7]==0x70B08 && map.bytes==auditBefore);io.word(0x70B08,0);
 pci.property->release();pci.property=nullptr;
 xe.auditDSIBoot(&pci,map.bytes.data(),map.bytes.size(),normal,true);
 assert(audits==3 && !allocations && lastAudit.words[1]==uint32_t(BootStatus::Unavailable));
 xe.dsiBoot=captureBoot(io,normal,xe.dsiProfile,xe.xelpMemory,true);
 xe.adlnOutputWiring.valid=true;xe.adlnOutputWiring.dsi=1;
 auto before=map.bytes;
 assert(!xe.prepareDisplay13BootScanout(&pci) && releases==1 && map.bytes==before); // prefix not certified yet
 assert(verifyBootPrefix(io,xe.dsiBoot,xe.xelpMemory));
 assert(xe.prepareDisplay13BootScanout(&pci) && releases==2 && map.bytes==before);
 memoryOK=false;assert(!xe.prepareDisplay13BootScanout(&pci) && releases==2);memoryOK=true;
 pci.command=0;assert(!xe.prepareDisplay13BootScanout(&pci) && releases==2);pci.command=6;
 io.word(0x7019C,4096);before=map.bytes;
 assert(!xe.prepareDisplay13BootScanout(&pci) && releases==3 && map.bytes==before && xe.dsiBoot.status==BootStatus::Changed);
 xe.dsiFirmware->release();assert(!alive);
 puts("PASS production DSI bootstrap: immutable firmware spans, stale firmware rejection, full-prefix prerequisite, zero-write handoff and map cleanup");
}
'''.replace('FIXTURE',fixture).replace('FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='dsi-boot-adapter-') as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
