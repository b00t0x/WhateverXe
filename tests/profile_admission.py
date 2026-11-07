#!/usr/bin/env python3
"""Run complete production profile selection across GPU, OS and opt-out contracts."""
from pathlib import Path
import re, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
f=re.search(r'void XE::selectProfile\(.*?\n\}',s,re.S).group()
code=r'''
#include "kern_xelp_platform.hpp"
#include "kern_xelp_os.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <cstring>
const char *version="Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64";
bool noaccel=false,tglOff=false;
bool checkKernelArgument(const char *s){return (noaccel && !strcmp(s,"-wex-noaccel")) || (tglOff && !strcmp(s,"-wex-tgl-off"));}
int kextIntelTGL,kextIntelTGLFb;
bool adlnNativeRCSProfileRequested=false,display13TraceEnabled=false,display13Offline=false;
struct XE {
 XELP::Profile xeProfile;
 bool tglGT2Detected=false,tglGT2Ready=false;
 bool display12SmallGTDetected=false,adlsDisplaySelected=false;
 bool adlnRequested=false,adlnDetected=false,adlnKernelReady=false,adlnNativeRequested=false;
 bool adlpDisplaySelected=false,adlpRenderRequested=false,display13FramebufferRequested=false;
 bool tglRequested=false,tglAcceleratorRequested=false,tglInactivePorts=false,tglPG1Poll=false;
 bool tglClockOrderRequested=false,tglTrainingLeaseRequested=false,tglProgramEnabled=false;
 int *currentFramebuffer=nullptr,*currentGraphics=nullptr;
 struct {bool available=false;}modDVMTCalcFix;
 void selectProfile(const XELP::Profile &);
};
FUNCTION
int main(){
 const uint32_t mobile[]={0x4626,0x4628,0x462a,0x46a0,0x46a1,0x46a2,0x46b0,0x46b1,0x46b2,0x46c0,0x46c1,0x46c2,0xa720,0xa721,0x46a3,0x46a6,0x46a8,0x46aa,0x46b3,0x46c3,0xa7a0,0xa7a1,0xa7a8,0xa7a9,0xa7aa,0xa7ab,0xa7ac,0xa7ad};
 constexpr unsigned mobileSelectors=2*sizeof(mobile)/sizeof(mobile[0]);
 unsigned cases=0;
 for(unsigned which=0;which<mobileSelectors+2;++which)for(unsigned rev=0;rev<256;++rev)for(unsigned off=0;off<2;++off) {
  noaccel=off;XE x;const bool isMobile=which<mobileSelectors,isN=which==mobileSelectors;
  const auto pci=(isMobile?mobile[which/2]:isN?0x46d1:0x9a49)<<16|0x8086;
  const auto effective=isMobile && (which&1)?0x9a498086:pci;
  const auto p=XELP::identify(pci,rev,effective);x.selectProfile(p);
  assert(XELP::same(x.xeProfile,p) && p.admitted());
  assert(x.currentFramebuffer==&kextIntelTGLFb && x.currentGraphics==&kextIntelTGL);
  assert(x.display13FramebufferRequested==(isMobile || isN));
  assert(x.adlpDisplaySelected==isMobile);
  assert(x.adlpRenderRequested==(isMobile&&!off));
  assert(x.adlnDetected==(isN || (isMobile&&!off)));
  assert(x.tglAcceleratorRequested==!off);
  assert(adlnNativeRCSProfileRequested==(isN || (isMobile&&!off)));++cases;
 }
 // Independent backend examples: PCI family classifications from Linux
 // i915_pciids.h v6.12, with the supported OS scope as a separate contract.
 // IDs never come from the current production DeviceTable.
 struct Target {unsigned id;bool display13,mobile,tgl;};
 const Target targets[]={
  // TGL
  {0x9a40,false,false,true},{0x9a49,false,false,true},{0x9a59,false,false,true},{0x9ac0,false,false,true},{0x9ac9,false,false,true},{0x9ad9,false,false,true},{0x9af8,false,false,true},{0x9a60,false,false,true},{0x9a68,false,false,true},{0x9a70,false,false,true},{0x9a78,false,false,true},
  // ADL/RPL-S
  {0x4680,false,false,false},{0x4682,false,false,false},{0x4688,false,false,false},{0x468a,false,false,false},{0x468b,false,false,false},{0x4690,false,false,false},{0x4692,false,false,false},{0x4693,false,false,false},{0xa780,false,false,false},{0xa781,false,false,false},{0xa782,false,false,false},{0xa783,false,false,false},{0xa788,false,false,false},{0xa789,false,false,false},{0xa78a,false,false,false},{0xa78b,false,false,false},
  // ADL-N/Twin Lake
  {0x46d0,true,false,false},{0x46d1,true,false,false},{0x46d2,true,false,false},{0x46d3,true,false,false},{0x46d4,true,false,false},
  // ADL/RPL mobile
  {0x4626,true,true,false},{0x4628,true,true,false},{0x462a,true,true,false},{0x46a0,true,true,false},{0x46a1,true,true,false},{0x46a2,true,true,false},{0x46b0,true,true,false},{0x46b1,true,true,false},{0x46b2,true,true,false},{0x46c0,true,true,false},{0x46c1,true,true,false},{0x46c2,true,true,false},{0xa720,true,true,false},{0xa721,true,true,false},{0x46a3,true,true,false},{0x46a6,true,true,false},{0x46a8,true,true,false},{0x46aa,true,true,false},{0x46b3,true,true,false},{0x46c3,true,true,false},{0xa7a0,true,true,false},{0xa7a1,true,true,false},{0xa7a8,true,true,false},{0xa7a9,true,true,false},{0xa7aa,true,true,false},{0xa7ab,true,true,false},{0xa7ac,true,true,false},{0xa7ad,true,true,false},
 };
 char text[180];
 for(unsigned major=20;major<=25;++major)for(unsigned patch:{0U,73U,998U})
 for(unsigned rev:{0U,1U,12U,255U})for(bool disabled:{false,true})for(bool off:{false,true})
 for(const auto &target:targets){
  snprintf(text,sizeof(text),"Darwin Kernel Version %u.6.%u: arbitrary-servicing-metadata/RELEASE_X86_64",major,patch);
  version=text;noaccel=disabled;tglOff=off;XE x;
  x.selectProfile(XELP::identify((target.id<<16)|0x8086,rev));
  const bool admitted=target.display13 || !off;
  assert(x.display13FramebufferRequested==target.display13);
  assert(x.tglRequested==(admitted && !(target.mobile && disabled)));
  assert(x.tglAcceleratorRequested==(admitted && !disabled));
  assert((x.currentFramebuffer==&kextIntelTGLFb)==admitted);
  assert((x.currentGraphics==&kextIntelTGL)==admitted);
  assert(x.adlpDisplaySelected==target.mobile);
  assert(x.adlsDisplaySelected==(!target.display13 && !target.tgl));
  ++cases;
 }
 // Preserve the earlier exact-DP1 selector contract separately from host OS
 // servicing: it is a donor identity, not a general version allowlist.
 version="Darwin Kernel Version 20.0.0: Sat Jun 13 17:58:16 PDT 2020; root:xnu-7090.0.0.111.5~1/RELEASE_X86_64";
 tglOff=false;
 for(bool disabled:{false,true})for(const auto &target:targets){
  noaccel=disabled;XE x;x.selectProfile(XELP::identify((target.id<<16)|0x8086,0));
  assert(x.tglRequested==target.tgl && x.tglAcceleratorRequested==(target.tgl&&!disabled));
  assert((x.currentFramebuffer==&kextIntelTGLFb)==target.tgl);
  assert(!x.display13FramebufferRequested);
 }
 // A family name is not enough: unsupported ABI families and malformed
 // versions must not select a consumer merely because its GPU is known.
 const char *invalid[]={"Darwin Kernel Version 26.6.0: future/RELEASE_X86_64",
  "Darwin Kernel Version 25.6.0: wrong/RELEASE_ARM64", "broken"};
 noaccel=tglOff=false;
 for(const auto bad:invalid)for(const auto &target:targets){
  version=bad;XE x;x.selectProfile(XELP::identify((target.id<<16)|0x8086,0));
  assert(!x.tglRequested && !x.tglAcceleratorRequested && !x.display13FramebufferRequested);
  assert(!x.currentFramebuffer && !x.currentGraphics);
 }
 noaccel=false;XE unknown;unknown.selectProfile(XELP::identify(0x4c8a8086,12));
 assert(!unknown.adlpRenderRequested && !unknown.adlnDetected && !unknown.tglAcceleratorRequested);
 puts("PASS production profile selections: complete profile selection, independent backend families, servicing metadata and opt-outs");
}
'''.replace('FUNCTION',f)
with tempfile.TemporaryDirectory(prefix='wex-adlp-render-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
