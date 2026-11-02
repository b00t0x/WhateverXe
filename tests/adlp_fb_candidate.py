#!/usr/bin/env python3
"""Replay production board selection and WEX/provider binding for firmware-selected fixed HDMI."""
from pathlib import Path
from production_source import read_source
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=read_source(r/'WhateverXe/kern_xe.cpp')
f='\n'.join(re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group() for sig in ['Display13OutputWiring XE::display13BoardWiring(', 'bool XE::xelpBindProbe('])
code=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "kern_display13_firmware.hpp"
#include "kern_display13_fb_topology.hpp"
#include "kern_alderlake_s.hpp"
bool adopt=false,hdmiTrial=false;
bool checkKernelArgument(const char *s){return !strcmp(s,"-wex-dsi-adopt")?adopt:!strcmp(s,"-wex-dsi-hdmi-test")?hdmiTrial:false;}
struct OSData {void *data=nullptr;unsigned bytes=0;void *getBytesNoCopy(){return data;}unsigned getLength(){return bytes;}};
#define OSDynamicCast(T,p) static_cast<T *>(p)
struct IOService {OSData *property=nullptr;OSData *getProperty(const char *){return property;}};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}void setProperty(const char*,bool){}};
XELP::ProbeConfig seen;unsigned calls=0;
bool configure(void *c,const XELP::ProbeConfig *p){assert(c==(void*)1);seen=*p;++calls;return p->valid();}
struct XE {
 bool dsiPanelOnly=false,dsiCommandTest=false,dsiPanelTest=false,dsiHostTest=false,dsiStopTest=false,dsiPowerTest=false;
 XELP::Profile xeProfile=XELP::identify(0x46A68086,12,0x9A498086);
 XELP::VBT::Ports firmwarePorts;
 bool adlpDisplaySelected=true,adlsDisplaySelected=false,tglHDMIWiringApplied=false,tglTCWiringLoaded=false;
 bool display13PanelProbeReady=true,display13PanelPowerReady=true;
 bool dsiResourcesReady=false,profileReady=true;
 bool prepareDSIFirmware(IOService *){return profileReady;}
 bool display13FBTopologyHooksReady=true,display13TCProbeReady=true,display13FBTCHooksReady=true;
 bool display13DPAltProbeReady=true,display13TCDPProbeReady=true,display13DPProbeReady=false,display13FBSSTHooksReady=true,display13FBAUXHooksReady=true;
 uintptr_t xelpProviderProbe=1,xelpProviderConfigureProbe=reinterpret_cast<uintptr_t>(configure);
 void *adlnTopologyController=(void*)1;Display13OutputWiring adlnOutputWiring,tglHDMIWiring;
 struct {bool valid=false;unsigned excluded=0;unsigned selected(){return 0;}}adlsWiring;
 struct {bool valid=false;unsigned typeC=0,excluded=0;}tglTCWiring;
 unsigned tglAUXParentMask=0;unsigned tglTCProbeMask(){return 0;}
 Display13OutputWiring display13BoardWiring(IOService *);
 bool xelpBindProbe(void *);
};
FUNCTIONS
int main(){
 XE dsi;dsi.adlnOutputWiring.valid=true;dsi.adlnOutputWiring.dsi=1;
 dsi.adlnOutputWiring.hdmi=2;dsi.adlnOutputWiring.ddc[1]=2;
 assert(!dsi.xelpBindProbe((void*)1));dsi.dsiResourcesReady=true;
 assert(dsi.xelpBindProbe((void*)1) && seen.combo==2);
 XE x;IOService pci;x.firmwarePorts.status=XELP::VBT::Status::Valid;x.firmwarePorts.present=0x38;
 for(unsigned d=3;d<=5;++d){auto &p=x.firmwarePorts.port[d];p.type=d==5?4:0x12;p.ddc=d;p.capabilities=3;p.hdmiPreset=7;}
 auto w=x.display13BoardWiring(&pci);assert(w.described() && w.hdmi==0x18 && w.ddc[3]==9 && w.ddc[4]==10 && !w.dp);
 assert(!Display13FBTopology::supportedWiring(w) && Display13FBTopology::supportedWiring(w,true));
 adopt=true;
 auto dw=x.display13BoardWiring(&pci);assert(dw.described() && dw.dsi==1 && dw.hdmi==w.hdmi);
 assert(!Display13FBTopology::supportedWiring(dw,true) && Display13FBTopology::supportedWiring(dw,true,true));
 hdmiTrial=true;assert(!x.display13BoardWiring(&pci).described());hdmiTrial=false;
 x.profileReady=false;assert(!x.display13BoardWiring(&pci).described());x.profileReady=true;
 x.firmwarePorts.present|=1;x.firmwarePorts.port[0].type=0x12;x.firmwarePorts.port[0].ddc=1;
 assert(!x.display13BoardWiring(&pci).described());x.firmwarePorts.present&=~1U;
 adopt=false;assert(!x.display13BoardWiring(&pci).dsi);
 x.adlnOutputWiring=w;assert(x.xelpBindProbe((void*)1));
 assert(seen.valid() && !seen.combo && seen.comboBackend==3 && seen.legacy==0x18 && seen.legacyBackend==0x78);
 assert(seen.profile.family==XELP::Family::AlderLakeP && seen.profile.admitted());
 for(unsigned f=0;f<5;++f){XE bad=x;
  if(f==0)bad.display13TCProbeReady=false;if(f==1)bad.display13FBTCHooksReady=false;
  if(f==2)bad.display13FBTopologyHooksReady=false;if(f==3)bad.xelpProviderConfigureProbe=0;
  if(f==4)bad.adlnTopologyController=(void*)2;
  const auto before=calls;assert(!bad.xelpBindProbe((void*)1) && calls==before);
 }
 OSData property;pci.property=&property;assert(!x.display13BoardWiring(&pci).described());pci.property=nullptr;
 x.firmwarePorts.port[3].type=4;
 assert(x.display13BoardWiring(&pci).hdmi==0x10); // Exclude DP without dropping unrelated HDMI.
 x.firmwarePorts.port[4].type=4;assert(!x.display13BoardWiring(&pci).described());
 // DP-capable records with TC AUX F..I compose with the existing HDMI.
 for(unsigned ddi=3;ddi<=6;++ddi) {
  XE dp;dp.firmwarePorts.status=XELP::VBT::Status::Valid;dp.firmwarePorts.present=1|(1U<<ddi);
  dp.firmwarePorts.port[0].type=0x12;dp.firmwarePorts.port[0].ddc=1;
  auto &v=dp.firmwarePorts.port[ddi];v.type=0x60d6;v.aux=(ddi+3)*0x10;v.capabilities=3;
  dp.adlnOutputWiring=dp.display13BoardWiring(&pci);
  assert(dp.adlnOutputWiring.hdmi==1 && dp.adlnOutputWiring.dp==(1U<<ddi));
  assert(Display13FBTopology::supportedWiring(dp.adlnOutputWiring,true) && dp.xelpBindProbe((void*)1));
  assert(seen.legacy==(1U<<ddi));
  assert(Display13FBTopology::bootDDI(dp.adlnOutputWiring,0x82000000|((ddi+1)<<27))==~0U); // DP GOP stays unqualified.
  dp.display13TCDPProbeReady=false;const auto before=calls;assert(!dp.xelpBindProbe((void*)1) && calls==before);
  dp.firmwarePorts.port[ddi].aux=0x40;assert(!dp.display13BoardWiring(&pci).dp);
 }
 unsigned cases=0;
 for(unsigned identity : {0x46A68086U,0x46D18086U})
 for(unsigned mask=1;mask<128;++mask)if(!(mask&~Display13OutputWiring::Physical)) {
  XE each;each.xeProfile=XELP::identify(identity,12);each.adlpDisplaySelected=each.xeProfile.mobileDisplay13();
  each.firmwarePorts.status=XELP::VBT::Status::Valid;each.firmwarePorts.present=mask;
  for(unsigned d=0;d<7;++d)if(mask&(1U<<d)) {
   auto &p=each.firmwarePorts.port[d];p.type=0x12;p.ddc=d<2?d+1:d;p.capabilities=3;
  }
  auto w=each.display13BoardWiring(&pci);
  assert(w.described() && w.hdmi==mask && Display13FBTopology::supportedWiring(w,true));
  assert(Display13FBTopology::supportedWiring(w)==!(mask&~3U));
  assert(Display13FBTopology::deferredBootPrepare(w,each.adlpDisplaySelected)==(each.adlpDisplaySelected || bool(mask&0x78)));
  each.adlnOutputWiring=w;assert(each.xelpBindProbe((void*)1));
  assert(seen.combo==(mask&3) && seen.legacy==(mask&0x78));
  for(unsigned d=0;d<7;++d)if(mask&(1U<<d)) {
   assert(w.ddc[d]==(d<2?d+1:d+6));
   for(unsigned mode=0;mode<8;++mode)
    assert(Display13FBTopology::bootDDI(w,0x80000000|((d+1)<<27)|(mode<<24))==(mode<=1?d:~0U));
  }
  ++cases;
 }
 assert(cases==126);
 XE adln;adln.xeProfile=XELP::identify(0x46D18086,0);adln.adlpDisplaySelected=false;
 uint32_t explicitB[]={1,2,0,2,0};property.data=explicitB;property.bytes=sizeof(explicitB);pci.property=&property;
 auto explicitW=adln.display13BoardWiring(&pci);assert(explicitW.described() && explicitW.hdmi==2);
 property.bytes=1;assert(!adln.display13BoardWiring(&pci).described());pci.property=nullptr;
 adln.xeProfile=XELP::identify(0x9A498086,1);assert(!adln.display13BoardWiring(&pci).described());
 puts("PASS production fixed-HDMI selection: all 63 Combo/TC subsets on ADL-N and ADL-P, firmware pins, boot DDI and capability/hook-gated provider binding");
}
'''.replace('FUNCTIONS',f)
with tempfile.TemporaryDirectory(prefix='adlp-fb-candidate-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
