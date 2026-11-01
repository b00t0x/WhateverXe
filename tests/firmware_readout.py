#!/usr/bin/env python3
"""Execute production firmware acquisition with independent Display/Render profiles."""
from pathlib import Path
import subprocess,tempfile,sys
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_firmware.cpp').read_text();body=s[s.index('static void publishBytes'):]
code=r'''
#include <cassert>
#include <cstdio>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <iterator>
#include "kern_xelp_platform.hpp"
#include "kern_vbt.hpp"
#include "kern_dsi_math.hpp"
bool auditEnabled=false;
bool checkKernelArgument(const char *arg){assert(!strcmp(arg,"-wex-dsi-audit"));return auditEnabled;}
#define OSDynamicCast(type,ptr) (ptr)
constexpr unsigned kIODirectionIn=1,kIOMapReadOnly=2;
struct OSData {
 std::vector<uint8_t> bytes;
 static OSData *withBytes(const void *p,unsigned n){auto d=new OSData;d->bytes.assign((const uint8_t*)p,(const uint8_t*)p+n);return d;}
 void release(){delete this;}const void *getBytesNoCopy(){return bytes.data();}unsigned getLength(){return bytes.size();}
};
struct IOMemoryDescriptor {
 struct Map {uintptr_t getVirtualAddress(){return 0;}unsigned getLength(){return 0;}void release(){}};
 static IOMemoryDescriptor *withPhysicalAddress(uint64_t,unsigned,unsigned){return nullptr;}
 Map *map(unsigned){return nullptr;}void release(){}
};
struct IOPCIDevice {unsigned reads=0,publications=0;uint32_t configRead32(unsigned off){assert(off==0xfc);++reads;return 0;}
 std::map<std::string,OSData> properties;
 OSData *getProperty(const char *key){auto i=properties.find(key);return i==properties.end()?nullptr:&i->second;}
 void setProperty(const char*,uint32_t,unsigned){++publications;}void setProperty(const char*,const void*,unsigned){++publications;}
 void setProperty(const char *key,OSData *d){++publications;properties[key]=*d;}
};
struct XE {XELP::Profile xeProfile;XELP::VBT::Ports firmwarePorts;void xelpReadFirmware(IOPCIDevice*);};
BODY
int main(int argc,char **argv){
 for(unsigned kind=0;kind<5;++kind){XE x;IOPCIDevice pci;
  x.xeProfile=kind==0?XELP::identify(0x46a68086,12,0x9a498086):kind==1?XELP::identify(0x46a68086,12):kind==2?XELP::identify(0x9a498086,1):kind==3?XELP::identify(0x46d18086,0):XELP::identify(0x4c8a8086,0);
  const bool expected=kind!=4;
  if(!kind)assert(x.xeProfile.admitted() && x.xeProfile.displayAdmitted());
  x.xelpReadFirmware(&pci);assert(pci.reads==unsigned(expected));assert(bool(pci.publications)==expected);
  assert(!pci.getProperty("WEXDSIFirmwareAudit"));
  x.xelpReadFirmware(nullptr);
 }
 auditEnabled=true;XE x;IOPCIDevice pci;x.xeProfile=XELP::identify(0x46d48086,0);
 x.xelpReadFirmware(&pci);auto absent=pci.getProperty("WEXDSIFirmwareAudit");assert(absent&&absent->bytes.size()==33*4);
 assert(XELP::VBT::u32(absent->bytes.data()+4)==unsigned(XELP::DSI::ProfileStatus::Bounds));
 if(argc==2){
  std::ifstream f(argv[1],std::ios::binary);assert(f);
  pci.properties["TGLVBT"].bytes.assign(std::istreambuf_iterator<char>(f),{});
  x.xelpReadFirmware(&pci);auto data=pci.getProperty("WEXDSIFirmwareAudit");assert(data);
  assert(XELP::VBT::u32(data->bytes.data()+4)==unsigned(XELP::DSI::ProfileStatus::Valid));
  assert(XELP::VBT::u32(data->bytes.data()+16*4)==359);
 }
 puts("PASS production VBT acquisition: physical and legacy-selector ADL-P plus TGL/ADL-N read firmware; unsupported RKL remains closed");}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='firmware-readout-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),*sys.argv[1:]],check=True)
