#!/usr/bin/env python3
"""Replay the production kext dispatcher with independent Render/Display inputs."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('\t// Display dispatch is independent')
b=s.index('\n}',a)+2
body=s[a:b].rstrip()[:-1]
kernel_start=s.index('\tif (adlnDetected || display13FramebufferRequested) {')
kernel_end=s.index('\n\tif (tglRequested) {',kernel_start)
kernel=s[kernel_start:kernel_end]
bootstrap=s[s.index('void XE::adlnBootstrap('):s.index('bool XE::xelpBindProbe(')]
def expression(name):
 return re.findall(r'\b'+name+r'\s*=\s*(.*?);',bootstrap,re.S)[-1]
code=r'''
#include <cassert>
#define SYSLOG(...) ((void)0)
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
using mach_vm_address_t=uintptr_t;
struct KernelPatcher {};
struct Kext {size_t loadIndex;};Kext kextIntelTGL{2};
bool loadOnly=false;
bool checkKernelArgument(const char *arg) {assert(!strcmp(arg,"-wex-adln-load-only"));return loadOnly;}
struct XE {
 bool adlpDisplaySelected=false,adlpRenderRequested=false,tglRequested=false;
 bool display13FramebufferRequested=false,adlnRequested=false,adlnDetected=false;
 bool adlnNativeRequested=false,adlnNativeReady=false,display13BootReady=false,tglActive=false;
 Kext *currentFramebuffer=nullptr,*currentGraphics=nullptr;
 enum Call {None,Display13,Render,Rejected,LoadOnly,Display12};Call call=None;
 bool processFramebufferKext(KernelPatcher&,size_t,mach_vm_address_t,size_t){call=display13FramebufferRequested?Display13:Display12;return true;}
 bool processADLPLoadOnly(KernelPatcher&,size_t,mach_vm_address_t,size_t){call=Rejected;return true;}
 bool processTGLAccelerator(KernelPatcher&,size_t,mach_vm_address_t,size_t){call=Render;return true;}
 bool processAlderLakeNRejectedAccelerator(KernelPatcher&,size_t,mach_vm_address_t,size_t){call=Rejected;return true;}
 bool processAlderLakeNLoadOnly(KernelPatcher&,size_t,mach_vm_address_t,size_t){call=LoadOnly;return true;}
 bool processKextFull(KernelPatcher&,size_t,mach_vm_address_t,size_t){assert(false && "dispatcher entered framebuffer installer");return false;}
 int firmware=0,pool=0,bootstrap=0,vm=0,mmio=0;
 unsigned tglDeviceId=0;
 struct {bool enabled=true;}tglVM;
 bool tglMMIOUCRouted=true,tglMMIOUCReady=true;
 struct GPU {
  unsigned configRead16(unsigned r){assert(r==2);return 0x46d0;}
  void setProperty(const char*,bool){}
  struct Map {void release(){}}map;
  Map *mapDeviceMemoryWithRegister(unsigned r){assert(r==0x10);return &map;}
 }gpu;
 struct {void processKernel(KernelPatcher&,void*){}}modDVMTCalcFix;
 void xelpReadFirmware(GPU*){++firmware;}
 void xelpLoadDisplayPool(GPU*){++pool;}
 void adlnBootstrap(GPU*){++bootstrap;}
 void tglVMPrepare(KernelPatcher&,GPU*){++vm;}
 void tglPrepareMMIOUC(KernelPatcher&,GPU*){++mmio;}
 void tglUpdateMMIOMap(GPU::Map*){}
 void kernel(KernelPatcher&patcher) {auto physicalGPU=&gpu;void *info=nullptr;KERNEL}
 bool dispatch(KernelPatcher&patcher,size_t index,mach_vm_address_t address,size_t size) {BODY}
};
namespace AlderLakeN {enum class GTStatus {Complete,Failed};}
void certificates() {
 unsigned args=0;
 const auto checkKernelArgument=[&](const char *name) {
  if(!strcmp(name,"-allow3d"))return bool(args&1);
  if(!strcmp(name,"-disablegfxfirmware"))return bool(args&2);
  if(!strcmp(name,"-wex-adln-scanout"))return bool(args&4);
  if(!strcmp(name,"-wex-adln-gpu"))return bool(args&8);
  assert(!strcmp(name,"-wex-noaccel"));return bool(args&16);
 };
 struct PCI {unsigned command;unsigned configRead16(unsigned r){assert(r==4);return command;}}device;
 auto pci=&device;
 struct Aperture {uint64_t length;uint64_t getLength(){return length;}}bar;
 struct {AlderLakeN::GTStatus status;}gt;
 bool adlnRequested=true,adlnKernelReady=true;
 const auto xelpDisplay13RenderDevice=[](PCI*){return true;};
 for(unsigned mask=0;mask<8192;++mask) {
  gt.status=(mask&1)?AlderLakeN::GTStatus::Complete:AlderLakeN::GTStatus::Failed;
  unsigned edram=(mask&2)?0:1;
  bool prefix=mask&4;
  auto aperture=(mask&8)?&bar:nullptr;bar.length=(mask&16)?0x10000000:0x8000000;
  unsigned page_size=(mask&32)?4096:16384;
  bool physicalOptions=mask&64;device.command=(mask&128)?6:2;
  args=mask>>8;
  const bool renderRequested=RENDER_REQUEST;
  const bool renderReady=renderRequested && (RENDER);
  const bool memoryReady=MEMORY;
  const bool display13BootReady=DISPLAY;
  const bool adlnNativeReady=COMBINED;
  // Display needs every memory prerequisite, independently of GT or noaccel.
  assert(display13BootReady==((mask&0xffc)==0x3fc));
  // Render still needs all prerequisites and must be disabled by noaccel.
  assert(adlnNativeReady==(mask==0x3ff));
 }
 puts("PASS 8192 FB/render preflight combinations: noaccel preserves FB memory, never admits render");
}
int main() {
 for(unsigned module=1;module<=3;++module){
  XE x;KernelPatcher p;Kext fb{1};x.adlpDisplaySelected=true;x.display13FramebufferRequested=true;x.currentFramebuffer=&fb;
  const bool result=x.dispatch(p,module,0,0);
  assert(x.call==(module==1?XE::Display13:module==2?XE::Rejected:XE::None));assert(result==(module!=3));
 }

 for(bool ready:{false,true})for(unsigned module=1;module<=3;++module) {
  XE x;KernelPatcher p;Kext fb{1},graphics{2};
  x.adlpDisplaySelected=x.adlpRenderRequested=x.display13FramebufferRequested=true;
  x.adlnRequested=x.adlnDetected=x.adlnNativeRequested=true;x.adlnNativeReady=x.tglActive=ready;
  x.currentFramebuffer=&fb;x.currentGraphics=&graphics;
  const bool result=x.dispatch(p,module,0,0);
  assert(x.call==(module==1?XE::Display13:module==2?(ready?XE::Render:XE::Rejected):XE::None));
  assert(result==(module!=3));
 }

 certificates();
 Kext fb{1},graphics{2};KernelPatcher patcher;unsigned cases=0;
 // Removing/rejecting Render must not redirect a selected Display13 kext
 // through the Display12 frame, nor make a graphics kext run Display code.
 for(unsigned render=0;render<4;++render)for(bool display:{false,true})
 for(bool framebuffer:{false,true})for(unsigned index=1;index<=3;++index) {
  XE x;x.currentFramebuffer=framebuffer?&fb:nullptr;x.currentGraphics=&graphics;
  x.display13FramebufferRequested=display;x.adlnRequested=x.adlnDetected=x.adlnNativeRequested=render!=0;
  x.adlnNativeReady=x.tglActive=render>=2;loadOnly=render==3;
  const bool result=x.dispatch(patcher,index,0x1000,0x2000);
  if(display && framebuffer && index==1)assert(result && x.call==XE::Display13);
  else if(!render)assert(!result && x.call==XE::None);
  else if(index!=2)assert(!result && x.call==XE::None);
  else assert(result && x.call==(render==1?XE::Rejected:render==2?XE::Render:XE::LoadOnly));
  ++cases;
 }
 // TGL Render dispatch stays outside the framebuffer installer, including
 // inactive/unknown kexts and absent graphics/framebuffer objects.
 for(bool requested:{false,true})for(bool active:{false,true})
 for(bool framebuffer:{false,true})for(bool accelerator:{false,true})
 for(unsigned index=1;index<=3;++index) {
  XE x;x.tglRequested=requested;x.tglActive=active;
  x.currentFramebuffer=framebuffer?&fb:nullptr;x.currentGraphics=accelerator?&graphics:nullptr;
  const auto expected=!requested||!active?XE::None:
   index==1&&framebuffer?XE::Display12:index==2&&accelerator?XE::Render:XE::None;
  assert(x.dispatch(patcher,index,0x1000,0x2000)==(expected!=XE::None));
  assert(x.call==expected);++cases;
 }
 for(bool render:{false,true})for(bool display:{false,true})for(bool ready:{false,true})for(bool gt:{false,true}) {
  XE x;x.adlnDetected=render;x.display13FramebufferRequested=display;
  x.adlnNativeRequested=render;x.adlnNativeReady=ready&&gt;x.display13BootReady=ready;x.kernel(patcher);
  assert(x.firmware==int(render||display) && x.pool==x.firmware && x.bootstrap==x.firmware);
  assert(x.vm==int((render||display)&&ready) && x.mmio==x.vm);
 }
 // Missing graphics object and missing native request never bypass rejection.
 for(bool detected:{false,true}) {
  XE x;x.adlnRequested=true;x.adlnDetected=detected;x.adlnNativeRequested=true;
  assert(!x.dispatch(patcher,2,0x1000,0x2000) && x.call==XE::None);
  x.currentGraphics=&graphics;x.adlnNativeRequested=false;
  assert(!x.dispatch(patcher,2,0x1000,0x2000) && x.call==XE::None);
 }
 printf("PASS %u independent Display/Render dispatch cases, native preflight rejection and load-only routing\n",cases);
}
'''.replace('BODY',body).replace('KERNEL',kernel).replace('RENDER_REQUEST',expression('renderRequested')).replace('RENDER',expression('renderReady')).replace('DISPLAY',expression('display13BootReady')).replace('MEMORY',expression('memoryReady')).replace('COMBINED',expression('adlnNativeReady')).replace('#include <cstdio>','#include <cstdio>\n#include <initializer_list>')
with tempfile.TemporaryDirectory(prefix='wex-display-dispatch-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
