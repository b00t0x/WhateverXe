#!/usr/bin/env python3
"""Exercise plugin selection and argument-independent provider startup."""
from pathlib import Path
import re
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
xe = (root / 'WhateverXe/kern_xe.cpp').read_text()
provider = (root / 'AppleIntelTGLCompat/AppleIntelTGLCompat.cpp').read_text()
start = (root / 'WhateverXe/kern_start.cpp').read_text()
assert 'PE_parse_boot_arg' not in provider and 'checkKernelArgument' not in provider
# Guard the public namespace across every reader, including plugin arrays.
external = {'-igfxdvmt', 'gfxrst', '-allow3d', '-disablegfxfirmware'}
used = set()
for directory in ['WhateverXe', 'AppleIntelTGLCompat']:
    for path in (root / directory).iterdir():
        if path.suffix not in {'.cpp', '.inc'}: continue
        used.update(re.findall(r'(?:checkKernelArgument|PE_parse_boot_argn)\("([^"]+)"', path.read_text()))
for array, expected in [('Off', '-wexoff'), ('Debug', '-wexdbg'), ('Beta', '-wexbeta')]:
    body = re.search(r'bootarg' + array + r'\[\] \{(.*?)\}', start, re.S).group(1)
    assert re.findall(r'"([^"]+)"', body) == [expected]
    used.add(expected)
assert external <= used
assert all(name.startswith('-wex') or name in external for name in used), used
assert not (root / 'WhateverXe/kern_boot_args.hpp').exists()
# Keep the complete diagnostic namespace regression independent of user docs.
names = json.loads((root / 'tests/fixtures/boot_argument_names.json').read_text())
assert set(names) == used - external
removed = [name for aliases in names.values() for name in aliases]
assert not set(removed) & used
assert not {"-wex-display13", "-wex-adln-native", "-wex-adln-rcs-profile"} & used
assert "-wex-nopanel" not in used and "-tglnopanel" not in used
assert "const bool linearTest = display13FramebufferRequested;" in xe
init = xe[xe.index('void XE::init() {'):xe.index('void XE::deinit()')]
assert 'BaseDeviceInfo' not in init and 'CPU_MODEL' not in init
entry = re.search(r'extern "C" kern_return_t tglStart\(.*?\n\}', provider, re.S).group()
# Execute the production prerequisite expressions at all three entry points.
prerequisites = []
for method,stop in [('wrapTGLFramebufferStart',' tglRecordStartOrder'),
                    ('wrapTGLAcceleratorStart',' if (callbackXE->adlnDetected'),
                    ('wrapTGLAcceleratorProbe',' if (!callbackXE->tglAcceleratorRequested')]:
    at=xe.index('XE::'+method+'(')
    body=xe[xe.index('{',at)+1:xe.index(stop,at)]
    expression=re.search(r'if\((.*?)\)(?:return| \{)',body,re.S).group(1)
    prerequisites.append('bool '+method+'Gate() { return !('+expression+'); }')

fixture = r'''
#include <cassert>
#include <cstring>
#include <set>
#include <string>
#include <initializer_list>
#include "kern_xelp_platform.hpp"
#include "kern_xelp_os.hpp"
static const char *qualified = "Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64";
static const char *version = qualified;
static std::set<std::string> args;
bool checkKernelArgument(const char *name) { return args.count(name); }
#define SYSLOG(...) ((void)0)
#define IOLog(...) ((void)0)
static bool display13Offline;
static int kextIntelTGLFb, kextAppleIntelTGLCompat, kextIntelTGL;
static struct { void onKextLoadForce(int *) {} } lilu;
struct XE {
 XELP::Profile xeProfile;
 bool tglGT2Detected=false,tglGT2Ready=false;
 bool adlnRequested=false, adlnDetected=false, adlnKernelReady=false, adlnNativeRequested=false, adlsDisplaySelected=false, display12SmallGTDetected=false;
 bool display13FramebufferRequested=false, display13TraceEnabled=false, adlpDisplaySelected=false, adlpRenderRequested=false;
 bool display12SmallGTReady=false, adlsDisplayHooksReady=false;
 bool adlsBootConsumersFailed=false;
 bool tglRequested=false, tglAcceleratorRequested=false;
 bool tglInactivePorts=false, tglPG1Poll=false, tglClockOrderRequested=false;
 bool tglProgramEnabled=false, tglTrainingLeaseRequested=false;
 struct { bool available=false; } modDVMTCalcFix;
 int *currentGraphics=nullptr, *currentFramebuffer=nullptr;
 void init();
 void selectProfile(const XELP::Profile &);
};
static XE *callbackXE;
static bool adlnNativeRCSProfileRequested;
using kern_return_t = int;
struct kmod_info_t {};
static constexpr int KERN_FAILURE=5, KERN_SUCCESS=0;
// Activation is an established prerequisite here; compat_exports executes the
// real publisher/consumer and rejects missing or incompatible activation.
namespace WEXGraphicsActivation {bool ready(){return true;}}
// ACTUAL_SOURCE
// PREREQUISITES
static XE boot(std::initializer_list<const char *> names, uint32_t pci=0x46D28086,uint32_t effective=0) {
 args.clear(); for (const auto name:names) args.insert(name);
 XE xe; xe.init(); xe.selectProfile(XELP::identify(pci,0,effective?effective:pci)); assert(tglStart(nullptr,nullptr)==KERN_SUCCESS);
 return xe;
}
int main() {
 // Rendering and Display prerequisites can fail independently. The Display
 // callbacks themselves are replayed elsewhere without any Render flag.
 for(unsigned bits=0;bits<16;++bits) {
  XE selected;selected.display12SmallGTDetected=bits&1;selected.adlsDisplaySelected=bits&2;
  selected.display12SmallGTReady=bits&4;selected.adlsDisplayHooksReady=bits&8;callbackXE=&selected;
  const bool ready=(!(bits&1) || (bits&4)) && (!(bits&2) || (bits&8));
  assert(wrapTGLFramebufferStartGate()==ready);
  assert(wrapTGLAcceleratorStartGate()==ready);
  assert(wrapTGLAcceleratorProbeGate()==ready);
 }
 for(unsigned bits=0;bits<4;++bits) {
  XE selected;selected.tglGT2Detected=bits&1;selected.tglGT2Ready=bits&2;callbackXE=&selected;
  assert(wrapTGLFramebufferStartGate());
  assert(wrapTGLAcceleratorStartGate()==(!(bits&1) || (bits&2)));
  assert(wrapTGLAcceleratorProbeGate()==(!(bits&1) || (bits&2)));
 }
 // Every implemented ADL-N/Twin ID enables the qualified normal path without args.
 XE xe;
 for(uint32_t pci:{0x46D08086U,0x46D18086U,0x46D28086U,0x46D38086U,0x46D48086U}) {
  xe=boot({},pci);
  assert(xe.adlnRequested && xe.adlnKernelReady && xe.adlnNativeRequested && xe.display13FramebufferRequested);
  assert(adlnNativeRCSProfileRequested && xe.tglAcceleratorRequested);
  assert(xe.currentGraphics==&kextIntelTGL && xe.currentFramebuffer==&kextIntelTGLFb);
 }
 xe=boot({"-wex-display13-offline","-wex-noaccel"},0x46A68086U,0x9A498086U);
 assert(display13Offline && xe.adlpDisplaySelected && !xe.tglAcceleratorRequested);
 xe=boot({"-wex-display13-offline"},0x9A498086U);
 assert(!display13Offline);
 xe=boot({"-wex-display13-trace"});
 assert(xe.display13TraceEnabled && xe.adlnNativeRequested && adlnNativeRCSProfileRequested);
 xe=boot({"-wex-adln-kernel-ctx"});
 assert(xe.adlnNativeRequested && xe.display13FramebufferRequested && !adlnNativeRCSProfileRequested);
 xe=boot({});assert(adlnNativeRCSProfileRequested);
 for(const char *obsolete:{"-igfxn97","-igfxadln","-wex-adln","-wex-tgl","-wex-display13","-wex-adln-native","-wex-adln-rcs-profile"}) {
  xe=boot({obsolete},0x12348086);
  assert(!xe.adlnRequested && !xe.tglRequested && !xe.adlnNativeRequested && !adlnNativeRCSProfileRequested);
 }
 xe=boot({},0x9A498086);
 assert(xe.tglRequested && xe.tglAcceleratorRequested && xe.tglProgramEnabled);
 xe=boot({"-wex-nopanel","-wex-tgl-prog-off"},0x9A408086);
 assert(xe.tglRequested && !xe.tglProgramEnabled);
 xe=boot({"-wex-tgl-off"},0x9A498086);assert(!xe.tglRequested);
 xe=boot({"-wex-noaccel"},0x9A498086);assert(xe.tglRequested && !xe.tglAcceleratorRequested);
 const char *allKernels[]={
 "Darwin Kernel Version 24.6.1: future servicing; root:xnu-future/RELEASE_X86_64",
 "Darwin Kernel Version 25.7.0: future update; root:xnu-future/RELEASE_X86_64",
 "Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64",
 "Darwin Kernel Version 21.6.0: Mon Jun 24 00:56:10 PDT 2024; root:xnu-8020.240.18.709.2~1/RELEASE_X86_64",
 "Darwin Kernel Version 22.6.0: Tue Jul 15 08:22:28 PDT 2025; root:xnu-8796.141.3.713.2~2/RELEASE_X86_64",
 "Darwin Kernel Version 23.6.0: Tue Jul 21 21:56:37 PDT 2026; root:xnu-10063.141.1.713.39~1/RELEASE_X86_64",
 "Darwin Kernel Version 24.6.0: Sun Aug 23 20:30:56 PDT 2026; root:xnu-11417.140.69.712.69~1/RELEASE_X86_64",
 "Darwin Kernel Version 25.6.0: Tue Aug 18 17:46:40 PDT 2026; root:xnu-12377.161.15.700.19~2/RELEASE_X86_64",
};
 for(auto kernel:allKernels) for(unsigned id:{0x9A40U,0x9A49U,0x9A78U,0x46D0U,0x46D1U,0x46D2U,0x46D3U,0x46D4U}) {
  version=kernel;xe=boot({},(id<<16)|0x8086);
  assert(xe.tglRequested && xe.tglAcceleratorRequested);
  assert(xe.currentGraphics==&kextIntelTGL && xe.currentFramebuffer==&kextIntelTGLFb);
  if(id<0x9000)assert(xe.adlnKernelReady && xe.adlnNativeRequested && xe.display13FramebufferRequested);
  assert(tglStart(nullptr,nullptr)==KERN_SUCCESS);
 }
 // Exercise the complete production selection, including the shared Display
 // adapter on registered desktop products and the still-unimplemented ADL-P.
 for(auto kernel:allKernels)for(auto id:{0x9A49U,0x9A78U,0x9A59U,0x9AC0U,0x9AC9U,0x9AD9U,0x9AF8U,0x4690U,0x4680U,0xA780U,0x4688U,0x468BU,0xA788U,0xA78BU,0x9A60U,0x9A68U,0x9A70U,0x46A6U,0x46D2U})
 for(auto rev:{0U,4U,12U,255U})for(unsigned option=0;option<4;++option) {
  version=kernel;args={};
  if(option&1)args.insert("-wex-tgl-off");
  if(option&2)args.insert("-wex-noaccel");
  XE selected;selected.init();selected.selectProfile(XELP::identify((id<<16)|0x8086,rev));
  const bool desktop=id==0x4690 || id==0x4680 || id==0xA780 || id==0x4688 || id==0x468B || id==0xA788 || id==0xA78B;
  const bool gt1=id==0x9A60 || id==0x9A68 || id==0x9A70;
  const bool mobile=XELP::tigerLakeGT2Device((id<<16)|0x8086) || gt1;
  assert(selected.tglGT2Detected==(mobile && !gt1));
  const bool mobile13=id==0x46A6 || id==0xA7A0;
  const bool small13=id==0x46D2;
  const bool render13=small13 || (mobile13 && !(option&2));
  assert(selected.display12SmallGTDetected==(desktop || gt1) && selected.adlsDisplaySelected==desktop);
  assert(selected.adlnDetected==render13 && selected.display13FramebufferRequested==(small13 || mobile13));
  const bool d12=(desktop || mobile) && !(option&1);
  assert(selected.tglRequested==(render13 || d12));
  assert(selected.tglAcceleratorRequested==((render13 || d12) && !(option&2)));
  assert((selected.currentFramebuffer==&kextIntelTGLFb)==(small13 || mobile13 || d12));
 }
 version="Darwin Kernel Version 21.6.0: Mon Jun 24 00:56:10 PDT 2024; root:xnu-8020.240.18.709.2~1/RELEASE_X86_64";
 xe=boot({},0x9A498086);assert(xe.tglRequested && xe.tglAcceleratorRequested);
 xe=boot({"-wex-noaccel"},0x9A498086);
 assert(xe.tglRequested && !xe.tglAcceleratorRequested && xe.currentFramebuffer==&kextIntelTGLFb);
 // Display13 keeps FB and the graphics-kext rejection route selected,
 // while noaccel removes permission to execute the accelerator.
 xe=boot({"-wex-noaccel"},0x46D28086);
 assert(xe.adlnNativeRequested && xe.display13FramebufferRequested && xe.tglRequested && !xe.tglAcceleratorRequested);
 version=qualified;

 xe=boot({},0x12348086,0x46D28086);
 assert(xe.adlnRequested && xe.adlnNativeRequested && xe.xeProfile.identity==0x12348086);
 xe=boot({},0x12348086,0x9A498086);assert(xe.tglRequested);
 xe=XE{};xe.selectProfile(XELP::identify(0x46A68086,12,0x9A498086));
 assert(xe.adlpDisplaySelected && xe.adlpRenderRequested && xe.display13FramebufferRequested && xe.tglAcceleratorRequested && xe.adlnRequested && xe.tglRequested);
 assert(xe.xeProfile.family==XELP::Family::AlderLakeP && xe.xeProfile.gt==XELP::GT::ADLPMobile);
 xe=boot({},0x46A68086);assert(xe.tglRequested && xe.adlnRequested);
 for(uint32_t pci:{0x4C8A8086U,0x46808086U}) {
  xe=boot({},pci);assert(!xe.adlnNativeRequested && !xe.display13FramebufferRequested && !adlnNativeRCSProfileRequested);
 }
 xe=XE{};xe.selectProfile(XELP::identify(0x46D28086,1));
 assert(xe.adlnNativeRequested && xe.display13FramebufferRequested && adlnNativeRCSProfileRequested);
 auto malformed=XELP::identify(0x46D28086,0);malformed.display=XELP::Display::XeD;
 xe=XE{};xe.selectProfile(malformed);assert(!xe.adlnRequested && !adlnNativeRCSProfileRequested);
 args={};version="unqualified";
 xe=XE{};xe.init();xe.selectProfile(XELP::identify(0x46D28086,0));
 assert(xe.adlnRequested && !xe.adlnKernelReady && !xe.adlnNativeRequested && !xe.display13FramebufferRequested);
 assert(!adlnNativeRCSProfileRequested);
 assert(tglStart(nullptr,nullptr)==KERN_FAILURE);
 xe=XE{};xe.selectProfile(XELP::identify(0x9A498086,0));assert(!xe.tglRequested);

}
'''.replace('// ACTUAL_SOURCE', init + entry).replace('// PREREQUISITES', '\n'.join(prerequisites))
with tempfile.TemporaryDirectory(prefix='wex-boot-args-') as directory:
    directory = Path(directory)
    cpp, exe = directory / 'test.cpp', directory / 'test'
    cpp.write_text(fixture)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I'+str(root/'WhateverXe'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print(f'boot namespace: {len(used-external)} options, {len(removed)} removed spellings; plugin/provider startup passed')
