#include "kern_dsi_session.hpp"
#include "kern_xelp_combo_clock.hpp"
#include <cassert>
#include <cstdio>
using namespace XELP::DSI;
int main() {
    Profile p;p.status=ProfileStatus::Valid;p.lanes=4;
    p.mode={135255,1080,46,22,2,1920,82,40,2,86,154};const auto plan=linkPlan(p);
    Readout r;r.status=ReadoutStatus::Complete;r.identity={0,0,0,1};r.afeKHz=811500;
    r.htotal=plan.htotal;r.hsync=plan.hsync;r.vtotal=plan.vtotal;r.vsync=plan.vsync;
    Capabilities caps;caps.installed=Capabilities::Adoption;Session s;
    for(unsigned bit=0;bit<8;++bit){auto missing=caps;missing.installed&=~(1U<<bit);assert(!s.bind(missing,r,p,1,100,200));}
    auto incompatible=caps;incompatible.version=2;assert(!s.bind(incompatible,r,p,1,100,200));
    assert(s.state==SessionState::Unbound&&!s.lease);
    assert(s.bind(caps,r,p,1,100,200)&&s.state==SessionState::FirmwareActive);
    assert(!s.bind(caps,r,p,1,100,200));
    assert(!s.beginSurface(200)&&s.beginSurface(201));auto t=s.ticket();
    SurfaceProof proof {t,r.identity,201,true,true,true};auto old=proof;--old.ticket.serial;
    assert(!s.completeSurface(old)&&s.state==SessionState::Switching);
    assert(!s.takeRetiredBacking()&&!s.takeCleanup().lease);
    assert(s.completeSurface(proof)&&s.state==SessionState::Streaming);
    assert(!s.beginSurface(202)); // must first transfer the old backing's cleanup obligation
    assert(s.takeRetiredBacking()==200&&!s.takeRetiredBacking());
    assert(s.beginSurface(202));t=s.ticket();assert(!s.abort(t,SessionError::Switch));
    assert(s.state==SessionState::Faulted&&s.currentBacking==201&&s.pendingBacking==202);
    assert(!s.takeCleanup().lease&&!s.beginSurface(203));
    StopProof stop {t,r.identity,true,true,true,false};assert(!s.stopped(stop));
    assert(s.firstError==SessionError::Switch&&s.lease==100);
    stop.clockDetached=true;auto stale=stop;++stale.ticket.epoch;assert(!s.stopped(stale));
    assert(s.stopped(stop));auto cleanup=s.takeCleanup();
    assert(cleanup.lease==100&&cleanup.currentBacking==201&&cleanup.pendingBacking==202&&!cleanup.retiredBacking);
    assert(s.state==SessionState::Released&&!s.takeCleanup().lease&&!s.stopped(stop));
    // Each missing stop proof retains ownership, including IRQ callbacks which
    // can still reference a surface after its plane stops scanning it out.
    for(unsigned missing=0;missing<4;++missing) {
        Session held;assert(held.bind(caps,r,p,9,100,200));
        StopProof q {held.ticket(),r.identity,missing!=0,missing!=1,missing!=2,missing!=3};
        assert(!held.stopped(q)&&held.lease==100&&!held.takeCleanup().lease);
    }
    // The existing allocator must see DSI's explicit binding even while the
    // DDI clock is gated. A different output may not reassign that PLL.
    XELPComboClock::ClockSnapshot clocks;clocks.layout=XELPComboClock::Layout::Display13;
    clocks.mux[0]=(1U<<10)|(1U<<11); // both legacy DDI consumers gated
    XELPComboClock::ClockAllocation allocation;allocation.binding[0]=2; // DSI owns PLL1
    auto other=allocation.choose(XELPComboClock::Port(1),clocks,true);
    assert(other.valid&&other.pll==0);
    clocks.enable[0]=0xCC000000;assert(!allocation.choose(XELPComboClock::Port(1),clocks,true).valid);
    puts("DSI adoption journal: capability gate, stale callbacks, retained backing, stop proofs, single cleanup, shared PLL: PASS");
}
