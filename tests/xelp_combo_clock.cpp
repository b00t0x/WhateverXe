#include "kern_xelp_combo_clock.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace XELPComboClock;
int main(){
 assert(donorPC(0x100000000,0x10006D77E)==0x6D77E);
 assert(donorPC(0x100000000,0x20006D77E)==UINT32_MAX);
 assert(donorPC(0x100000000,0xFFFF0000)==UINT32_MAX);
 unsigned cases=0;
 for(unsigned pll=0;pll<2;++pll)for(bool gated:{false,true}) {
  ClockSnapshot s;s.layout=Layout::Display13;s.mux[0]=0x800|pll|(gated?0x400U:0U);s.enable[pll]=0xCC000000;
  ClockAllocation a;assert(a.retain(0,pll,0x1234,s));
  assert(a.retain(0,pll,0x1234,s) && !a.retain(0,pll,0x5678,s));
  assert(!a.choose(Port(0),s,true).valid && !a.choose(Port(0),s,false).valid && !a.share(Port(0),s).valid);
  auto peer=a.choose(Port(1),s,true);assert(peer.valid && peer.pll==1-pll);
  assert(!a.reset() && a.binding[0]==pll+1 && !a.release(0,0x5678));
  assert(a.release(0,0x1234) && !a.release(0,0x1234) && a.reset());
  s.enable[pll]=0x88000000;assert(!a.retain(0,pll,0x1234,s));
 }
 for(auto layout:{Layout::TGL,Layout::Display13})for(unsigned target=0;target<2;++target)
 for(unsigned peer=0;peer<2u;++peer)for(unsigned occupied=0;occupied<2;++occupied) {
  if(target==peer)continue;
  ClockSnapshot s;s.layout=layout;s.mux[0]=0x01000C00;
  Port p(target),q(peer);s.mux[0]=(s.mux[0]&~(q.clockOff|(3U<<q.clockShift)))|(occupied<<q.clockShift);
  s.enable[occupied]=0xCC000000;ClockAllocation a;
  auto choice=a.choose(p,s,true);assert(choice.valid && choice.pll==1-occupied);
  a.binding[target]=choice.pll+1;s.enable[choice.pll]=0x0C000000;
  auto off=a.choose(p,s,false);assert(off.valid && !off.keepPLL && off.pll==choice.pll);
  s.mux[0]=(s.mux[0]&~(p.clockOff|(3U<<p.clockShift)))|(occupied<<p.clockShift);
  assert(a.choose(p,s,false).keepPLL);
  // Exhaustion does not borrow TBT or TC PLLs even though they look free.
  assert(!a.choose(p,s,true).valid);
  // Native sharing is admitted only for an active, fully acknowledged source.
  if(peer<2){assert(a.share(q,s).valid);s.enable[occupied]=0x88000000;assert(!a.share(q,s).valid);}
  ++cases;
 }
 for(auto layout:{Layout::TGL,Layout::Display13})for(unsigned reserved:{2u,3u}) {
  ClockSnapshot s;s.layout=layout;s.mux[0]=0x01000800|reserved;
  ClockAllocation a;Port p(0);assert(!a.choose(p,s,true).valid && !a.choose(p,s,false).valid);
 }
 puts("PASS TGL/Display13 live-peer exclusion, partial-owner teardown, exhaustion, source qualification and reserved-PLL rejection");
 assert(cases==8);
}
