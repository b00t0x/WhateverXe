#include "kern_display13_panel.hpp"
#include "kern_display13_firmware.hpp"
#include "kern_display13_fb_topology.hpp"
#include "kern_xelp_probe.hpp"
#include <cassert>
#include <map>
#include <vector>
#include <cstdio>
using namespace XELP;
struct IO {
 std::map<uint32_t,uint32_t> r;
 std::vector<uint32_t> writes;
 bool drop=false;
 uint32_t read(uint32_t a){return r.at(a);}
 void write(uint32_t a,uint32_t v){writes.push_back(a);if(!drop)r.at(a)=v;}
 bool readable(uint32_t){return false;}
};
int main() {
 for(auto family:{Family::AlderLakeP,Family::RaptorLakeP,Family::AlderLakeN})for(unsigned ddi=0;ddi<2;++ddi) {
  VBT::Ports v;v.status=VBT::Status::Valid;v.present=1U<<ddi;
  v.port[ddi]={0x78C6,ddi?7U:10U,0,ddi?0x10U:0x40U,0,0};
  auto &p=v.panel;p.valid=p.backlightValid=p.pwmPresent=true;p.ddi=ddi;p.backlightType=2;p.pwmHz=200;
  assert(PanelPower::nativePPS0(v,family));
  auto w=VBT::display13Fixed(v,0x7b,family);assert(w.panel==(1U<<ddi) && !w.dp && !w.hdmi);
  alignas(8) uint8_t src[0x1c0]{},dst[0x1c0]{};src[0x12]=9;
  for(unsigned d=0;d<9;++d) {uint32_t c[]={d,d<3?d+1:d+6,1,d<3?0U:1U,0x400,0x201};memcpy(src+0x20+24*d,c,24);}
  const uint32_t panel[]={0,0,0,0,2,0x18};memcpy(src+0x20,panel,24);
  assert(Display13FBTopology::apply(dst,sizeof(dst),src,w));
  auto cfg=reinterpret_cast<uint32_t *>(dst+0x20+24*ddi);
  assert(cfg[0]==ddi && !cfg[1] && !cfg[2] && cfg[4]==2 && cfg[5]==0x18 && w.matches(cfg));
  assert(Display13FBTopology::supportedWiring(w,true) && Display13FBTopology::deferredBootPrepare(w,false));
  assert(Display13FBTopology::bootDDI(w,0x82000000|((ddi+1)<<27),true)==ddi);
  for(unsigned bad=0;bad<10;++bad) {
   auto b=v;
   switch(bad) {case 0:b.panel.controller=1;break;case 1:b.panel.backlightType=3;break;
    case 2:b.panel.backlightValid=false;break;case 3:b.unmapped=1;break;case 4:b.panel.valid=false;break;
    case 5:b.port[ddi].flags=4;break;case 6:b.port[ddi].capabilities=1;break;
    case 7:b.port[ddi].aux=0x90;break;case 8:b.present|=1U<<(1-ddi);b.port[1-ddi]=b.port[ddi];break;
    case 9:b.panel.ddi=2;break;}
   assert(!PanelPower::nativePPS0(b,family));assert(!VBT::display13Fixed(b,0x7b,family).panel);
  }
  for(unsigned type:{2U,5U}) {
   auto aux=v;aux.panel.backlightType=type;aux.panel.pwmPresent=false;
   assert(!PanelPower::nativePPS0(aux,family)); // PWM-only callers stay unchanged.
   assert(PanelPower::nativePPS0(aux,family,true));
   assert(VBT::display13Fixed(aux,0x7b,family).panel==(1U<<ddi));
  }
  auto overlap=w;overlap.dp=w.panel;assert(!overlap.described());overlap=w;overlap.panel=3;assert(!overlap.described());
  ProbeBinding binding;binding.controller=1;binding.access=2;binding.platform=reinterpret_cast<uintptr_t>(dst);
  binding.config.profile=identify(family==Family::AlderLakeN?0x46D28086:0xA7A88086,4);
  binding.config.combo=w.panel;binding.config.comboBackend=3;
  IO io;assert(probeMode(io,binding,2,reinterpret_cast<uintptr_t>(cfg),cfg)==1);
  assert(!probeMode(io,binding,3,reinterpret_cast<uintptr_t>(cfg),cfg));
  // BIOS PWM wins over VBT; a large period must not overflow at full brightness.
  io.r={{0xC7208,0x083401f4},{0xC720C,0x138801f4},{0xC7204,0x67},{0xC8254,120000},{0xC8250,0xa0000000},{0xC2014,256}};
  Display13Panel::State state;assert(state.capture(io,p));uint32_t duty=0;
  assert(state.pwm.period==120000 && state.pwm.polarity==(1U<<29));
  assert(state.pwm.duty(65535,duty) && duty==120000);assert(state.pwm.duty(0,duty) && !duty);
  assert(!state.pwm.duty(65536,duty));io.r[0xC8254]=0;assert(state.capture(io,p) && state.pwm.period==120000);
  state={};assert(state.capture(io,p) && state.pwm.period==120000);
  for(auto reg:{0xC7208U,0xC720CU,0xC7204U,0xC8254U,0xC8250U,0xC2014U}) {
   auto bad=io;bad.r[reg]=~0U;state={};assert(!state.capture(bad,p) && !state.captured);
  }
  assert(io.writes.empty());
 }
 for(bool initial:{false,true})for(bool failure:{false,true}) {
  IO io;io.r[0xC2020]=0x1234|(initial?1U<<29:0);io.drop=failure;Display13Panel::OnGate gate;
  assert(gate.begin(io)==(initial || !failure));
  io.drop=false;io.r[0xC2020]|=1U<<17;
  assert(gate.finish(io));assert(io.r[0xC2020]==(0x21234|(initial?1U<<29:0)));
  assert(io.writes.size()==(initial?0U:2U));
 }
 IO io;io.r[0xC2020]=~0U;Display13Panel::OnGate gate;assert(!gate.begin(io) && gate.finish(io) && io.writes.empty());
 puts("PASS Display13 panel topology/probe/PPS0 qualification, PWM preservation/fallback, WA cleanup");
}
