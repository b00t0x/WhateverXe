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
#include "kern_display13_firmware.hpp"
#include "kern_display13_panel.hpp"
#include "kern_alderlake_s.hpp"
#include "firmware_panel_fixture.hpp"
bool auditEnabled=false;
bool checkKernelArgument(const char *arg){assert(!strcmp(arg,"-wex-dsi-audit"));return auditEnabled;}
#define OSDynamicCast(type,ptr) (ptr)
constexpr unsigned kIODirectionIn=1,kIOMapReadOnly=2;
struct OSData {
 std::vector<uint8_t> bytes;
 static OSData *withBytes(const void *p,unsigned n){auto d=new OSData;d->bytes.assign((const uint8_t*)p,(const uint8_t*)p+n);return d;}
 void release(){delete this;}const void *getBytesNoCopy(){return bytes.data();}unsigned getLength(){return bytes.size();}
};
std::vector<uint8_t> opregion(8192),externalVBT;bool mapAvailable=true;
struct IOMemoryDescriptor {
 std::vector<uint8_t> *storage;
 struct Map {std::vector<uint8_t> *storage;uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(storage->data());}unsigned getLength(){return storage->size();}void release(){delete this;}};
 static IOMemoryDescriptor *withPhysicalAddress(uint64_t address,unsigned bytes,unsigned){
  assert((address==0x100000 && bytes==8192)||(address==0x200000 && bytes==externalVBT.size()));
  return new IOMemoryDescriptor{address==0x100000?&opregion:&externalVBT};}
 Map *map(unsigned){return mapAvailable?new Map{storage}:nullptr;}void release(){delete this;}
};
struct IOPCIDevice {unsigned reads=0,publications=0;uint32_t asls=0;uint64_t id=1;
 uint32_t configRead32(unsigned off){assert(off==0xfc);++reads;return asls;}
 uint64_t getRegistryEntryID(){return id;}
 std::map<std::string,OSData> properties;
 OSData *getProperty(const char *key){auto i=properties.find(key);return i==properties.end()?nullptr:&i->second;}
 void setProperty(const char*,uint32_t,unsigned){++publications;}void setProperty(const char*,const void*,unsigned){++publications;}
 void setProperty(const char *key,OSData *d){++publications;properties[key]=*d;}
};
struct XE {XELP::Profile xeProfile;XELP::VBT::Ports firmwarePorts;uint64_t firmwareOwnerID=0;XELP::Family firmwareFamily{};
 void xelpReadFirmware(IOPCIDevice*);void captureFirmware(IOPCIDevice*,const XELP::Profile&);};
BODY

// Synthetic Intel OpRegion/VBT wire records, with a physical HDMI-B/DDC2
// child from the existing independent firmware_ports contract. No Apple input.
void makeFirmware(){
 opregion.assign(8192,0);memcpy(opregion.data(),"IntelGraphicsMem",16);opregion[16]=8;
 uint8_t *b=opregion.data()+0x400;memcpy(b,"$VBT",4);
 auto w16=[&](unsigned at,unsigned v){b[at]=v;b[at+1]=v>>8;};
 w16(20,100);w16(22,48);w16(24,117);b[28]=48;
 memcpy(b+48,"BIOS_DATA_BLOCK ",16);w16(64,250);w16(66,22);w16(68,69);
 b[70]=2;w16(71,44);b[77]=39;w16(80,0x60d2);b[94]=1;b[97]=2;b[101]=0xc0;
 uint8_t sum=0;for(unsigned i=0;i<117;++i)sum+=b[i];b[26]-=sum;
}
void snapshotLifetime(){
 const auto tgl=XELP::identify(0x9a498086,1);
 for(uint64_t owner:{1ULL,0x123456789ULL}) {
  makeFirmware();XE x;IOPCIDevice pci;pci.id=owner;pci.asls=0x100000;x.xeProfile=tgl;
  x.captureFirmware(&pci,tgl);assert(x.firmwarePorts.valid());
  auto original=pci.properties["TGLVBT"].bytes;
  assert(original.size()==117 && x.firmwarePorts.hdmi(3).hdmi==2 && x.firmwarePorts.hdmi(3).ddc[1]==2);
  // The loader can clear ASLS or reclaim the backing table before the native
  // image callback. The consumed wiring must come from the original copy.
  pci.asls=0;opregion.assign(8192,0);x.xelpReadFirmware(&pci);
  auto wiring=XELP::VBT::selectHDMI(false,nullptr,0,x.firmwarePorts,3);
  assert(wiring.hdmi==2 && wiring.ddc[1]==2 && pci.reads==1);
  assert(pci.properties["TGLVBT"].bytes==original);
  // A different physical registry owner must never consume this snapshot.
  IOPCIDevice other;other.id=owner+1;x.xelpReadFirmware(&other);
  assert(!x.firmwarePorts.valid() && other.reads==1);
 }
 // Missing/failed early input must remain retryable at actual activation.
 for(bool failedMap:{false,true}) {
  makeFirmware();XE x;IOPCIDevice pci;x.xeProfile=tgl;
  pci.asls=failedMap?0x100000:0;mapAvailable=!failedMap;
  x.captureFirmware(&pci,tgl);assert(!x.firmwarePorts.valid());
  mapAvailable=true;pci.asls=0x100000;x.xelpReadFirmware(&pci);
  assert(x.firmwarePorts.valid() && x.firmwarePorts.hdmi(3).hdmi==2 && pci.reads==2);
 }
 // Registry identity and parser family are independent cache requirements.
 for(bool zeroOwner:{false,true}) {
  makeFirmware();XE x;IOPCIDevice pci;pci.id=zeroOwner?0:1;pci.asls=0x100000;x.xeProfile=tgl;
  x.captureFirmware(&pci,tgl);pci.asls=0;
  if(!zeroOwner)x.xeProfile=XELP::identify(0x46d18086,0);
  x.xelpReadFirmware(&pci);assert(!x.firmwarePorts.valid() && pci.reads==2);
 }
}
// Production capture -> owner cache -> wiring/PPS/PWM consumers. MMIO is
// modeled; these checks do not establish that a firmware provides mailbox 5.
void automaticPanel(){
 for(unsigned id:{0x9a498086U,0x46808086U,0xa7808086U,0x46a68086U,0xa7a88086U,0x46d18086U})for(bool external:{false,true})for(bool pointers:{false,true}) {
  PanelFixture::Input f(pointers,true);opregion=f.op;externalVBT=f.vbt;
  if(external){opregion[23]=2;PanelFixture::w32(opregion,0x58,0x14);PanelFixture::w32(opregion,0x3ba,0x200000);PanelFixture::w32(opregion,0x3c2,externalVBT.size());memset(opregion.data()+0x400,0,externalVBT.size());}
  XE x;IOPCIDevice pci;pci.asls=0x100000;x.xeProfile=XELP::identify(id,12);
  x.xelpReadFirmware(&pci);assert(x.firmwarePorts.panel.type==9 && x.firmwarePorts.panel.valid);
  // Reclaim both source mappings before consuming the copied facts.
  opregion.assign(8192,0);externalVBT.clear();pci.asls=0;x.xelpReadFirmware(&pci);
  const auto &v=x.firmwarePorts;assert(pci.reads==1);
  const auto adapter=XELP::displayAdapter(x.xeProfile.family);
  if(adapter==XELP::DisplayAdapter::TGL) {
   assert(v.hdmi(3).hdmi==2 && XELP::PanelPower::nativePPS0(v,x.xeProfile.family));
   assert(TGLComboDP::fromFirmware(v,2)==0); // internal eDP retains the native panel route
  } else if(adapter==XELP::DisplayAdapter::ADLS) {
   const auto wiring=AlderLakeS::Wiring::fromFirmware(v);assert(wiring.panel==1 && wiring.hdmi==8); // ADL-S DVO HDMI-B is TC1, not Combo-B
  } else {
   const auto wiring=XELP::VBT::display13Fixed(v,0x7b,x.xeProfile.family);
   assert(wiring.panel==1 && wiring.hdmi==2);
  }
  const uint32_t native[5]={};auto timing=XELP::PanelPower::select(v.panel,native,0,0,0);
  assert(timing.valid && timing.fields[0]==911 && timing.fields[2]==910 && timing.fields[4]==2);
  struct IO {uint32_t read(uint32_t r){return r==0xc2014?256:0;}} io;
  Display13Panel::State panel;assert(panel.capture(io,v.panel) && panel.pwm.period==114833);
  uint32_t duty=0;assert(panel.pwm.duty(32768,duty) && duty==57417);
  IOPCIDevice foreign;foreign.id=2;x.xelpReadFirmware(&foreign);assert(!x.firmwarePorts.panel.valid);
 }
}
int main(int argc,char **argv){
 snapshotLifetime();automaticPanel();

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
 puts("PASS owner-bound firmware snapshot lifetime, retry and production VBT acquisition: physical and legacy-selector ADL-P plus TGL/ADL-N read firmware; unsupported RKL remains closed");}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='firmware-readout-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),'-I'+str(r/'tests'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),*sys.argv[1:]],check=True)
 for name,before,after in [('lost-snapshot','if(owner && firmwareOwnerID==owner','if(false && firmwareOwnerID==owner'),
                           ('foreign-owner','firmwareOwnerID==owner','true'),
                           ('lost-edid','profile.family,identity','profile.family,nullptr')]:
  assert before in code
  (p/'fault.cpp').write_text(code.replace(before,after,1))
  subprocess.run(['clang++','-std=c++17','-O1','-I'+str(r/'WhateverXe'),'-I'+str(r/'tests'),str(p/'fault.cpp'),'-o',str(p/'fault')],check=True)
  result=subprocess.run([str(p/'fault')],capture_output=True,text=True)
  assert result.returncode!=0 and 'Assertion failed:' in result.stderr,(name,result.returncode,result.stderr)
  print('PASS rejected semantic fault:',name)
