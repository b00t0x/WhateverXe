// Xe_LPD PPS0 / PCH PWM. Register facts: Linux v6.12 intel_pps.c and
// intel_backlight.c. Native code remains the owner of panel delays/sequence.
#ifndef KERN_DISPLAY13_PANEL_HPP
#define KERN_DISPLAY13_PANEL_HPP
#include "kern_xelp_panel_power.hpp"
#include "kern_xelp_panel_pwm.hpp"
#include "kern_edp_aux_backlight.hpp"
namespace Display13Panel {
// Exact c004fc1a LightUpEDP: reject failed power/train before enabling scanout.
// This adapter admits only the standard native panel (controller+D0 is null),
// so the donor PSR-only branches are skipped without inventing PSR ownership.
struct LightPatch {uint32_t offset;uint8_t size,before[16],after[16];};
static constexpr LightPatch lightPatches[]={
 {0x50669,16,{0x48,0x8b,0x83,0x40,0x0d,0x00,0x00,0x83,0xb8,0x90,0x01,0x00,0x00,0x02,0x75,0x5b},
              {0x41,0x89,0xc6,0x85,0xc0,0x0f,0x85,0xcd,0x01,0x00,0x00,0xe9,0x5b,0x00,0x00,0x00}},
 {0x506fd,6,{0x0f,0x84,0x50,0x01,0x00,0x00},{0x0f,0x85,0x3e,0x01,0x00,0x00}}
};
constexpr uint32_t PeriodSite=0x507BD;
static constexpr uint8_t periodBefore[]={0x8b,0x93,0x60,0x0e,0x00,0x00,0x48,0x8b,0x07,0xbe,0x54,0x82,0x0c,0x00,0xff,0x90,0x30,0x01,0x00,0x00};
struct State {
 XELP::PanelPWM pwm;
 uint32_t bios[3] {};
 bool captured {},aux {};
 uint32_t brightness {65535};
 template<class IO> bool capture(IO &io,const XELP::VBT::Ports::Panel &panel) {
  if(captured)return aux || pwm.valid();
  const bool useAux=XELP::PanelPower::auxBacklight(panel);
  const auto on=io.read(0xC7208),off=io.read(0xC720C),control=io.read(0xC7204);
  const auto period=panel.pwmPresent?io.read(0xC8254):0,polarity=panel.pwmPresent?io.read(0xC8250):0;
  const auto selected=XELP::PanelPWM::select(period,polarity,period || !panel.pwmPresent?0:io.read(0xC2014),panel.pwmHz,panel.activeLowPWM);
  if(on==~0U || off==~0U || control==~0U || (!useAux && !selected.valid()))return false;
  bios[0]=on;bios[1]=off;bios[2]=control;pwm=panel.pwmPresent?selected:XELP::PanelPWM{};aux=useAux;captured=true;return true;
 }
};
// WA 22019252566 (Display13/14): block PCH DPLS clock gating over panel on.
// Keep a pre-existing firmware/peer request and unrelated bits. Call finish
// after every native exit, including timeout. The caller owns PG1 and serializes PPS.
struct OnGate {
 static constexpr uint32_t Reg=0xC2020,Bit=1U<<29;
 bool changed {};
 template<class IO> bool begin(IO &io) {
  const auto before=io.read(Reg);if(before==~0U)return false;
  if(before&Bit)return true;
  changed=true;io.write(Reg,before|Bit);return io.read(Reg)==(before|Bit);
 }
 template<class IO> bool finish(IO &io) {
  if(!changed)return true;
  const auto value=io.read(Reg);if(value==~0U)return false;
  io.write(Reg,value&~Bit);const bool ok=io.read(Reg)==(value&~Bit);
  if(ok)changed=false;return ok;
 }
};
}
#endif
