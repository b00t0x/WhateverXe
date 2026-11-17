#!/usr/bin/env python3
"""Run production PWM binding/lifecycle and the exact donor PWM write tail."""
from pathlib import Path
import hashlib,os,re,subprocess,tempfile,sys
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
names=['adlsPanelPWMTarget','adlsCapturePanelPWM','wrapADLSLightUpEDP','wrapADLSPanelBacklight','wrapADLSPanelPWMEnable']
functions='\n'.join(re.search(r'(?:void|bool|uint32_t) XE::'+n+r'\(.*?\n\}',s,re.S).group() for n in names)
install=s[s.index('    // ADLS_PANEL_PWM_INSTALL:'):s.index('    // ADLS_PANEL_PWM_INSTALL_END')]
arrays={n:re.search(n+r'\[\]=\{([^}]+)',install).group(1) for n in ['lightEntry','brightnessEntry','enable']}
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 for n,off in [('lightEntry',0x5063e),('brightnessEntry',0x4a56a),('enable',0x507e9)]:
  raw=bytes(int(x,0) for x in arrays[n].split(','));assert b[off:off+len(raw)]==raw
 assert b[0x1b12c:0x1b12e]==bytes.fromhex('31c0')
 assert b[0x1b15b:0x1b162]==bytes.fromhex('48898750050000')
 # Native start chooses Camellia/Banksia from platform+190 only.
 assert b[0x40216:0x4021d]==bytes.fromhex('83b89001000002')
 assert b[0x402bb:0x402c2]==bytes.fromhex('83b89001000003')
code=r'''
#include "kern_alderlake_s.hpp"
#include "kern_xelp_panel_pwm.hpp"
#include "kern_tgl_tc_native.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <iterator>
#include <sys/mman.h>
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
struct IORegistryEntry {static IORegistryEntry* getRegistryRoot(){static IORegistryEntry o;return &o;}void setProperty(const char*,bool){}};
struct KernelPatcher {
 enum class Error{NoError,Failed};bool fail=false;unsigned routes=0,blocks=0;uintptr_t delta=0;
 struct RouteRequest {template<class T> RouteRequest(const char*,T,uintptr_t&) {}};
 uintptr_t solveSymbol(unsigned,const char*s,uintptr_t a,size_t){return a+(strstr(s,"LightUp")?0x5063e:0x4a56a)+delta;}
 bool routeMultiple(unsigned,RouteRequest*,unsigned,uintptr_t,size_t){++routes;return !fail;}
 void routeBlock(uintptr_t a,const uint8_t*b,size_t n){++blocks;memcpy((void*)a,b,n);}
 Error getError(){return Error::NoError;}void clearError(){}
};
struct XE {
 static XE*callbackXE;
 bool display13FramebufferRequested=false,display13PanelPWMReady=false;
 struct {unsigned panel=0;}adlnOutputWiring;
 static bool display13PanelHealthy(){assert(false);return false;}
 static bool display13PanelCapture(void*){assert(false);return false;}
 static uint32_t display13PanelBrightness(void*,uint32_t){assert(false);return 1;}
 static void display13PanelPWMEnable(void*){assert(false);}

 bool adlsDisplaySelected=true,adlsPanelPWMReady=true,tglActive=true;XELP::PanelPWM adlsPanelPWM;
 XELP::VBT::Ports firmwarePorts;AlderLakeS::Wiring adlsWiring;
 struct {XELP::Family family=XELP::Family::AlderLakeS;}xeProfile;
 TGLTCNativeFunctions tglTCNativeFunctions;void*tglObservationController=nullptr;
 uint64_t tglPlatformCopy[0x1c0/8]{};
 uintptr_t orgTGLRegisterRead=0,orgTGLRegisterWrite=0,orgADLSLightUpEDP=0,orgADLSPanelBacklight=0;
 static bool adlsPanelPWMTarget(void*,void*&,void*&);static bool adlsCapturePanelPWM(void*);
 static uint32_t wrapADLSLightUpEDP(void*,void*,void*,const void*);static uint32_t wrapADLSPanelBacklight(void*,uint32_t);
 static void wrapADLSPanelPWMEnable(void*);
 static uint32_t wrapTGLRegisterRead(void*,unsigned long);static void wrapTGLRegisterWrite(void*,unsigned long,uint32_t);
 void install(KernelPatcher&patcher,unsigned index,uintptr_t address,size_t size){INSTALL}
};XE*XE::callbackXE;
FUNCTIONS
alignas(8) uint8_t ctl[0x1600]{},port[0x600]{},access[0x80]{},power[0x90]{};
template<class T>T&field(void*p,unsigned n){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+n);}
static void*owner=ctl;static uint32_t period=120000,control=1U<<29,duty=0,strap=256,wells=1;
static unsigned get=0,put=0,refs=0,reads=0,writes=0,nativeLight=0,nativeBacklight=0;
static uint32_t base(void*,uint32_t){return 0;}static void aux(void*){}static void*lookup(uint32_t d){assert(d==0);return port;}
static void getPG(void*p,uint32_t n){assert(p==power&&n==1);++get;++refs;}static void putPG(void*p,uint32_t n){assert(p==power&&n==1&&refs);++put;--refs;}
static uint32_t read(void*a,unsigned long reg){assert(a==access&&refs);++reads;switch(reg){case 0x45404:return wells;case 0x42000:return 0x1000000;case 0xc8254:return period;case 0xc8250:return control;case 0xc2014:return strap;default:assert(false);return ~0U;}}
void XE::wrapTGLRegisterWrite(void*a,unsigned long reg,uint32_t v){assert(a==access);++writes;
 switch(reg){case 0xc8254:period=v;break;case 0xc8250:control=v;break;case 0xc8258:duty=v;break;default:assert(false);}}
static uint32_t light(void*c,void*,void*,const void*){assert(c==ctl);++nativeLight;return 0x1234;}
static uint32_t backlight(void*c,uint32_t){assert(c==ctl);++nativeBacklight;return 0x5678;}
static void setup(XE&s){
 XE::callbackXE=&s;memset(ctl,0,sizeof(ctl));memset(port,0,sizeof(port));memset(access,0,sizeof(access));memset(power,0,sizeof(power));
 owner=ctl;period=120000;control=1U<<29;duty=0;strap=256;wells=1;get=put=refs=reads=writes=nativeLight=nativeBacklight=0;
 s.tglObservationController=ctl;s.tglTCNativeFunctions={base,aux,aux,getPG,putPG,lookup,&owner,true};
 s.orgTGLRegisterRead=(uintptr_t)read;s.orgTGLRegisterWrite=(uintptr_t)XE::wrapTGLRegisterWrite;
 s.orgADLSLightUpEDP=(uintptr_t)light;s.orgADLSPanelBacklight=(uintptr_t)backlight;
 auto &v=s.firmwarePorts;v.status=XELP::VBT::Status::Valid;v.present=1;v.port[0]={0x78c6,10,0,0x40,0,0};
 v.panel.valid=true;v.panel.ddi=0;v.panel.backlightValid=true;v.panel.pwmPresent=true;v.panel.backlightType=2;v.panel.pwmHz=200;v.panel.activeLowPWM=true;
 s.adlsWiring=AlderLakeS::Wiring::fromFirmware(v);assert(s.adlsWiring.panel==1);
 const uint32_t record[]={0,0,0,0,2,0x18};memcpy((uint8_t*)s.tglPlatformCopy+0x20,record,sizeof(record));
 field<void*>(ctl,0xc40)=access;field<void*>(ctl,0xc8)=power;field<void*>(ctl,0xd40)=s.tglPlatformCopy;
 field<void*>(power,0x10)=ctl;field<void*>(power,0x78)=access;
 field<void*>(port,0x548)=(uint8_t*)s.tglPlatformCopy+0x20;field<void*>(port,0x578)=access;field<uint32_t>(port,0x550)=1;
 field<void*>(access,0x50)=access;field<uint32_t>(access,0x60)=0x200000;
}
static void patchCall(uint8_t*m,unsigned site,unsigned target){assert(m[site]==0xe8);int32_t d=target-site-5;memcpy(m+site+1,&d,4);}
static void jump(uint8_t*p,uintptr_t target){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&target,8);p[10]=0xff;p[11]=0xe0;}
int main(int argc,char**argv){
 std::vector<uint8_t> image(0x51000);const uint8_t l[]={LIGHT},b[]={BRIGHT},e[]={ENABLE};
 for(unsigned n=0;n<6;++n){
  memcpy(image.data()+0x5063e,l,sizeof(l));memcpy(image.data()+0x4a56a,b,sizeof(b));memcpy(image.data()+0x507e9,e,sizeof(e));
  XE s;s.adlsPanelPWMReady=false;KernelPatcher p;size_t size=image.size();if(n==0)size=0x508c3;if(n==1)p.delta=1;if(n==2)image[0x507e9]^=1;if(n==3)image[0x4a56a]^=1;if(n==4)p.fail=true;
  s.install(p,0,(uintptr_t)image.data(),size);assert(s.adlsPanelPWMReady==(n==5));assert(p.blocks==(n==5));
 }
 for(unsigned bad=0;bad<16;++bad){XE s;setup(s);
  if(bad==0)s.adlsPanelPWMReady=false;if(bad==1)s.tglActive=false;if(bad==2)owner=nullptr;if(bad==3)s.firmwarePorts.panel.controller=1;
  if(bad==4)field<void*>(ctl,0xd0)=port;if(bad==5)field<void*>(ctl,0xd40)=nullptr;if(bad==6)field<void*>(port,0x548)=nullptr;
  if(bad==7)field<void*>(power,0x78)=nullptr;if(bad==8)field<uint32_t>(access,0x60)=0xc8258;
  if(bad==9)wells=0;if(bad==10)period=~0U;if(bad==11){period=0;s.firmwarePorts.panel.pwmHz=0;}
  if(bad==14)field<uint32_t>(port,0x550)=3;if(bad==15)s.xeProfile.family=XELP::Family::TigerLake;
  if(bad==12)control=~0U;if(bad==13){period=0;strap=~0U;}
  assert(XE::wrapADLSLightUpEDP(ctl,nullptr,nullptr,nullptr)==kIOReturnNotReady&&nativeLight==0&&writes==0&&get==put&&!refs);
 }
 // PPS/PWM capture precedes the native body and cannot require its mode1
 // publication. Preserve all physical panel/owner guards for initial mode0.
 for(unsigned mode:{0U,1U}){XE initial;setup(initial);field<uint32_t>(port,0x550)=mode;
  assert(XE::wrapADLSLightUpEDP(ctl,nullptr,nullptr,nullptr)==0x1234 && nativeLight==1);
  assert(field<uint32_t>(ctl,0xe60)==120000 && get==put && !refs);
 }
 // Display12 PWM and native delegation remain independent of Display13's
 // AUX backend. A Display13 call in this fixture is a hard failure above.
 for(auto family:{XELP::Family::AlderLakeS,XELP::Family::RaptorLakeS}) {
  XE legacy;setup(legacy);legacy.xeProfile.family=family;
  assert(XE::wrapADLSLightUpEDP(ctl,nullptr,nullptr,nullptr)==0x1234);
  assert(!XE::wrapADLSPanelBacklight(ctl,32768) && duty==60000);
  XE::wrapADLSPanelPWMEnable(access);assert(control==0xa0000000);
 }
 for(bool adls:{false,true}) {
  XE external;setup(external);external.adlsDisplaySelected=adls;external.adlsWiring.panel=0;
  assert(XE::wrapADLSLightUpEDP(ctl,nullptr,nullptr,nullptr)==0x1234 && nativeLight==1);
  assert(XE::wrapADLSPanelBacklight(ctl,100)==0x5678 && nativeBacklight==1);
  assert(!reads && !writes && !get && !put);
 }
 XE s;setup(s);assert(XE::wrapADLSLightUpEDP(ctl,nullptr,nullptr,nullptr)==0x1234&&nativeLight==1&&field<uint32_t>(ctl,0xe60)==120000);
 assert(get==put&&get==1&&!refs);const auto capturedReads=reads;
 for(unsigned v:{0U,1U,32768U,65535U}){assert(!XE::wrapADLSPanelBacklight(ctl,v));assert(duty==uint64_t(120000)*v/65535&&field<uint32_t>(ctl,0xe58)==v&&get==put&&!refs);}
 assert(!nativeBacklight);auto oldWrites=writes;assert(XE::wrapADLSPanelBacklight(ctl,65536)==kIOReturnNotReady&&writes==oldWrites);
 XE::wrapADLSPanelPWMEnable(access);assert(control==0xa0000000);
 control=0;period=0;assert(XE::adlsCapturePanelPWM(ctl)&&s.adlsPanelPWM.period==120000); // native disable cannot erase snapshot
 s.adlsPanelPWM={};strap=0;assert(XE::adlsCapturePanelPWM(ctl)&&s.adlsPanelPWM.period==96000&&s.adlsPanelPWM.polarity==(1U<<29));
 s.adlsWiring.panel=0;assert(XE::wrapADLSPanelBacklight(ctl,7)==0x5678&&nativeBacklight==1);
 if(argc>1){
  std::ifstream f(argv[1],std::ios::binary);std::vector<char>raw((std::istreambuf_iterator<char>(f)),{});assert(raw.size()>0xdfb00);
  const unsigned stubs=(raw.size()+4095)&~4095U;const size_t length=stubs+4096;
  auto mem=(uint8_t*)mmap(nullptr,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mem!=MAP_FAILED);memcpy(mem,raw.data(),raw.size());
  XE state;setup(state);state.adlsPanelPWMReady=false;KernelPatcher patcher;state.install(patcher,0,(uintptr_t)mem,length);assert(state.adlsPanelPWMReady);
  jump(mem+stubs,(uintptr_t)XE::wrapADLSPanelBacklight);patchCall(mem,0x507da,stubs);
  // Execute the native PWM tail with the exact controller register convention.
  uint8_t entry[]={0x53,0x48,0x89,0xfb,0xe9,0,0,0,0};int32_t d=0x507b6-(stubs+32+sizeof(entry));memcpy(entry+5,&d,4);memcpy(mem+stubs+32,entry,sizeof(entry));
  mem[0x507f9]=0x5b;mem[0x507fa]=0xc3;
  assert(mprotect(mem,length,PROT_READ|PROT_WRITE|PROT_EXEC)==0);
  uintptr_t vtable[40]{};vtable[0x130/8]=(uintptr_t)XE::wrapTGLRegisterWrite;field<void*>(access,0)=vtable;
  auto tail=(void(*)(void*))(mem+stubs+32);
  state.orgADLSLightUpEDP=(uintptr_t)+[](void*c,void*,void*,const void*)->uint32_t{assert(c==ctl);return 0;};
  for(unsigned cycle=0;cycle<16;++cycle){
   field<uint32_t>(port,0x550)=cycle?1:0;
   assert(XE::wrapADLSLightUpEDP(ctl,nullptr,nullptr,nullptr)==0);
   field<uint32_t>(ctl,0xe58)=65535;tail(ctl);
   assert(period==120000&&duty==120000&&control==0xa0000000&&get==put&&!refs);
   XE::wrapTGLRegisterWrite(access,0xc8250,0);XE::wrapTGLRegisterWrite(access,0xc8258,0);
  }
  // Real unadapted duty math provides the overflow counterexample.
  auto native=(uint32_t(*)(void*,uint32_t))(mem+0x4a56a);native(ctl,65535);assert(duty!=120000);
  munmap(mem,length);
 }
 puts("PASS production ADL-S PWM qualification, faults, balanced power, BIOS/VBT fallback, native PWM tail and 32-bit overflow counterexample");
}
'''.replace('INSTALL',install).replace('FUNCTIONS',functions).replace('LIGHT',arrays['lightEntry']).replace('BRIGHT',arrays['brightnessEntry']).replace('ENABLE',arrays['enable'])
with tempfile.TemporaryDirectory(prefix='adls-panel-pwm-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+sys.argv[1:],check=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))

 with tempfile.TemporaryDirectory(prefix='adls-panel-mode-fault-') as d:
  p=Path(d);before='port+0x550)>1';assert before in code
  (p/'fault.cpp').write_text(code.replace(before,'port+0x550)!=1'))
  subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-I'+str(r/'WhateverXe'),str(p/'fault.cpp'),'-o',str(p/'fault')],check=True)
  result=subprocess.run([str(p/'fault')],capture_output=True,text=True)
  assert result.returncode!=0 and 'Assertion failed:' in result.stderr
  print('PASS rejected semantic fault: initial panel mode requires later native publication')
