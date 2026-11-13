// Panel sequencing facts and a bounded bridge to the exact donor's PPS fields.
// Units/layout checked against Linux v6.12 intel_bios.h/intel_pps.c. No Linux
// implementation is included. Native PPS sequencing remains the single owner.
#ifndef KERN_XELP_PANEL_POWER_HPP
#define KERN_XELP_PANEL_POWER_HPP
#include "kern_vbt.hpp"
namespace XELP {
struct PanelPower {
 // The current donor has one panel and addresses PPS/PWM controller 0.
 // PPS1 and non-Combo panels need separate adapters. Callers with the VESA
 // backend may opt into AUX panel admission; PWM-only adapters stay unchanged.
 static bool auxBacklight(const VBT::Ports::Panel &p) {
  // VBT interface 5 is VESA AUX. A display-DDI panel without PWM also needs
  // capability probing; never manufacture a PWM period for that panel.
  return p.backlightValid && (p.backlightType==5 || (p.backlightType==2 && !p.pwmPresent));
 }
 static bool nativePPS0(const VBT::Ports &v,Family family,bool allowAux=false) {
  const auto &p=v.panel;
  const auto adapter=displayAdapter(family);
  const bool backend=(adapter==DisplayAdapter::TGL && p.ddi<2) ||
                     (adapter==DisplayAdapter::ADLS && p.ddi==0) ||
                     ((adapter==DisplayAdapter::ADLP || adapter==DisplayAdapter::ADLN) && p.ddi<2);
  if(!backend || !v.valid() || v.unmapped || !p.valid ||
     !p.backlightValid || (!(p.pwmPresent && p.backlightType==2) && !(allowAux && auxBacklight(p))) || p.controller)return false;
  const auto &port=v.port[p.ddi];const auto aux=p.ddi?0x10U:0x40U;
  if(!(v.present&(1U<<p.ddi)) || !port.internal() || !port.dp() ||
     (port.type&(0x400|0x20|1)) || (port.flags&0x17) || (port.capabilities&3) ||
     VBT::ddi(family,port.dvo)!=p.ddi || port.aux!=aux)return false;
  for(unsigned d=0;d<PortCount;++d)if(d!=p.ddi && (v.present&(1U<<d)) &&
     v.port[d].dp() && v.port[d].aux==aux)return false;
  return true;
 }
 struct Plan {
  // Donor +1548: T8, T9, T1+T3, T10 (100us), cycle (100ms encoding).
  uint32_t fields[5] {};
  bool valid {};
 };
 static Plan select(const VBT::Ports::Panel &panel,const uint32_t native[5],
                    uint32_t on,uint32_t off,uint32_t control) {
  Plan p;
  if(!panel.valid || !native || on==~0U || off==~0U || control==~0U)return p;
  // Reject overflow before the donor masks silently shorten a delay. Merge
  // each explicit native property, even if its T8 property is absent.
  for(unsigned i=0;i<4;++i)if(native[i]>0x1fff)return p;
  if(native[4]>31)return p;
  const uint32_t bios[]={on>>16&0x1fff,on&0x1fff,off&0x1fff,off>>16&0x1fff,(control>>4&31)*1000};
  const uint32_t defaults[]={2100,500,500,5000,6100};
  const unsigned index[]={2,0,1,3,4};
  bool any=false;for(auto v:panel.delays)any|=v!=0;
  for(unsigned i=0;i<5;++i) {
   uint32_t v=panel.delays[i];
   // Linux adjusts VBT T12 once, before merging; never add to a BIOS readback.
   if(i==4 && any)v+=1000;
   if(v<bios[i])v=bios[i];
   const auto n=native[index[i]]*(i==4?1000U:1U);
   if(v<n)v=n;
   if(!v)v=defaults[i];
   if(i==4)v=(v+999)/1000;
   if(v>(i==4?31U:0x1fffU))return {};
   p.fields[index[i]]=v;
  }
  // Unlike i915, the donor owns the hardware backlight waits. Keep T8/T9;
  // do not copy i915's value 1, which relies on separate software sleeps.
  p.valid=true;return p;
 }
};
}
#endif
