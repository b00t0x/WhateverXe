#include "kern_vbt.hpp"
#include "kern_display13_firmware.hpp"
#include "kern_alderlake_s.hpp"
#include "firmware_panel_fixture.hpp"
#include <cassert>
#include <cstdio>
using namespace XELP;
using namespace XELP::VBT;
Ports parseInput(PanelFixture::Input &f){return parse(f.vbt.data(),f.vbt.size(),Family::AlderLakeP,panelEDID(f.op.data(),f.op.size()));}
int main(){
 // Optional panel identity/backlight failures cannot remove HDMI-B. Check
 // the shared parser with Display12 and Display13 consumers, not just ADL-P.
 for(auto family:{Family::TigerLake,Family::AlderLakeS,Family::RaptorLakeS,Family::AlderLakeP,Family::RaptorLakeP,Family::AlderLakeN}) {
  for(unsigned damaged=0;damaged<4;++damaged) {
   PanelFixture::Input f;f.vbt[f.options]=4;
   if(damaged==1)f.vbt[f.options]=0xff; // no matching identity below
   if(damaged==2)f.vbt[f.backlight]=5; // malformed optional table
   if(damaged==3)f.vbt[f.edp-2]=0xff; // malformed optional tail length
   const auto p=parse(f.vbt.data(),f.vbt.size(),family);
   assert(p.valid());
   if(family==Family::AlderLakeS || family==Family::RaptorLakeS) {
    const auto w=AlderLakeS::Wiring::fromFirmware(p);assert(w.hdmi==8);
   } else assert(p.hdmi(3).hdmi==2 && p.hdmi(3).ddc[1]==2);
   if(!damaged)assert(p.panel.valid && p.panel.type==4 && PanelPower::nativePPS0(p,family));
  }
 }
 for(bool ptr:{false,true})for(bool dup:{false,true}) {
  PanelFixture::Input f(ptr,dup);auto p=parseInput(f);
  assert(p.valid() && p.panel.valid && p.panel.type==9 && p.panel.pwmHz==209);
  auto wiring=display13Fixed(p,0x7b,Family::AlderLakeP);assert(wiring.panel==1 && wiring.hdmi==2 && wiring.ddc[1]==2);
  const uint32_t native[5]={};auto plan=PanelPower::select(p.panel,native,0,0,0);
  assert(plan.valid && plan.fields[0]==911 && plan.fields[1]==912 && plan.fields[2]==910 && plan.fields[3]==913 && plan.fields[4]==2);
  // Explicit indices remain authoritative over an unrelated mailbox identity.
  auto explicitProfile=f;explicitProfile.vbt[f.options]=4;assert(parseInput(explicitProfile).panel.type==4);
  // LFP2 uses the second option byte, even with a single internal child.
  explicitProfile.vbt[78]=0x80;if(dup)explicitProfile.vbt[156]=0x80;explicitProfile.vbt[f.options+1]=6;assert(parseInput(explicitProfile).panel.type==6);
  // Duplicates cannot turn a competing AUX/handle/timing field into first-wins.
  if(dup)for(unsigned off:{0U,2U,7U,16U,19U,23U,25U,33U,37U}){
   auto bad=f;bad.vbt[156+off]^=1;assert(!display13Fixed(parseInput(bad),0x7b,Family::AlderLakeP).panel);
  }
  // Prefer an exact date match to an earlier undated product ID.
  auto dates=f;memcpy(dates.vbt.data()+f.data+56,dates.vbt.data()+f.data+66*9+56,8);
  PanelFixture::w16(dates.vbt,f.data+64,0);assert(parseInput(dates).panel.type==9);
  PanelFixture::w16(dates.vbt,f.data+66*9+64,0);assert(!parseInput(dates).panel.valid);
  // Identical identities with different consumed profiles are ambiguous.
  auto ambiguous=f;memcpy(ambiguous.vbt.data()+f.data+56,ambiguous.vbt.data()+f.data+66*9+56,10);
  assert(!parseInput(ambiguous).panel.valid);
  // All-consumer equivalence is safe even when the index cannot be known.
  for(unsigned n=0;n<16;++n){memcpy(ambiguous.vbt.data()+f.edp+10*n,f.vbt.data()+f.edp+90,10);memcpy(ambiguous.vbt.data()+f.backlight+1+6*n,f.vbt.data()+f.backlight+55,6);}
  auto equivalent=parseInput(ambiguous);assert(equivalent.panel.valid && equivalent.panel.type==UINT32_MAX && equivalent.panel.pwmHz==209);
  ambiguous.op[0x1c00]=0;assert(parseInput(ambiguous).panel.valid);
  for(unsigned off:{0U,8U,20U,126U,127U}){auto bad=f;bad.op[0x1c04+off]^=1;assert(!parseInput(bad).panel.valid && parseInput(bad).hdmi(3).hdmi==2);}
  for(unsigned blocks:{0U,2U,3U}){auto bad=f;bad.op[0x1c00]=blocks;assert(!parseInput(bad).panel.valid);}
  auto bad=f;bad.vbt[f.data+36]=0;assert(!parseInput(bad).panel.valid);
  if(ptr)for(unsigned off:{0U,1U,3U,6U,7U,9U,10U,144U}){bad=f;bad.vbt[f.ptr+off]^=1;assert(!parseInput(bad).panel.valid);}
  for(size_t n=0;n<f.vbt.size();++n)assert(!parse(f.vbt.data(),n,Family::AlderLakeP).valid());
 }
 puts("PASS automatic EDID panel identity, explicit/generated pointers, equivalent facts, duplicate records and PPS/wiring consumers");
}
