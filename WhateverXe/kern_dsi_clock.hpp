// Retire an exclusively retained Display13 DSI Combo PLL after host/IRQ stop.
// Sequence: Linux v6.12 icl_pll_disable; register bits from i915_reg.h.
#ifndef KERN_DSI_CLOCK_HPP
#define KERN_DSI_CLOCK_HPP
#include "kern_dsi_quiesce.hpp"
#include "kern_xelp_combo_clock.hpp"
namespace XELP { namespace DSI { namespace Clock {
enum class Status : uint32_t {Input,Ownership,Consumer,Peer,Source,Disable,Power,Complete};
struct Result {
    Status status {Status::Input};uint32_t reg {},before {},after {},writes {},waitUs {};
    bool complete() const {return status==Status::Complete;}
};
// Caller holds shared Combo/FB/owner serialization and has parked the actual
// native IRQ bank under its workloop gate. No waits under the IRQ spinlock.
// IO::ready checks retained ownership, not the original running source image.
// Keep the external allocation on every failure, including a half-disabled PLL.
// Never commandeer a peer PLL or clear a firmware power-well request here.
template<class IO> Result retire(IO &io,const Readout &link,const Quiesce::Result &host,bool irqParked,
                                XELPComboClock::ClockAllocation &allocation,uintptr_t owner) {
    Result r;
    if(!owner || !link.complete() || !link.identity.valid() || link.identity.pipe!=0 || !host.complete() || !irqParked)return r;
    XELPComboClock::PLL pll;if(!XELPComboClock::PLL::resolve(link.identity.pll,pll))return r;
    r.reg=pll.enable;
    const auto value=[&](uint32_t reg,uint32_t &out) {
        if(!io.readable(reg))return false;
        out=io.read(reg);return out!=UINT32_MAX;
    };
    const auto owned=[&]() {
        uint32_t wells=0;
        return io.ready() && allocation.external[0]==owner && allocation.binding[0]==link.identity.pll+1 &&
            value(0x45404,wells) && (wells&0x403)==0x403;
    };
    const auto stopped=[&]() {
        Display13Transcoder::Bindings bindings;bindings.dsi0Pipe=0;
        uint32_t buffer=0,secondary=0,mux=0;
        // host.complete() witnessed ULPS before disabling the function. The
        // request self-clears and host disable may reset its status.
        return Display13FBIRQ::consumersStopped(io,1,bindings) && value(0x64000,buffer) &&
            (buffer&0x80000080U)==0x80 && value(0x7B808,secondary) && !(secondary&0xC0000000U) &&
            value(0x6BC00,secondary) && !(secondary&(1U<<31)) &&
            value(0x164280,mux) && (mux&0x403U)==(0x400U|link.identity.pll);
    };
    const auto exclusive=[&]() {
        XELPComboClock::ClockSnapshot s;s.layout=XELPComboClock::Layout::Display13;
        if(!value(0x164280,s.mux[0]))return false;
        if(s.peers(XELPComboClock::Port(0))&(1U<<link.identity.pll))return false;
        for(unsigned phy=1;phy<3;++phy)if(allocation.binding[phy]==link.identity.pll+1)return false;
        return true;
    };
    const auto source=[&]() {
        uint32_t cfg0=0,cfg1=0;
        return value(pll.cfg0,cfg0) && value(pll.cfg1,cfg1) && cfg0==link.cfg0 && cfg1==link.cfg1;
    };
    const auto held=[&]() {
        if(!owned()){r.status=Status::Ownership;return false;}
        if(!stopped()){r.status=Status::Consumer;return false;}
        if(!exclusive()){r.status=Status::Peer;return false;}
        if(!source()){r.status=Status::Source;return false;}
        return true;
    };
    if(!held() || !value(pll.enable,r.before))return r;
    if((r.before&0xCC000000U)!=0xCC000000U){r.status=Status::Source;return r;}
    const auto wait=[&](uint32_t mask) {
        for(unsigned us=0;;us+=10) {
            if(!held() || !value(pll.enable,r.after))return false;
            if(!(r.after&mask))return true;
            if(us>=1000)return false;
            io.delayUs(10);r.waitUs+=10;
        }
    };
    // Require both request and state bits clear. A lost write must not be
    // accepted merely because its status bit happened to read unlocked.
    r.status=Status::Disable;io.write(pll.enable,r.before&~(1U<<31));++r.writes;
    if(!wait(0xC0000000U))return r;
    r.status=Status::Power;io.write(pll.enable,r.after&~(1U<<27));++r.writes;
    if(!wait(0xCC000000U) || !held())return r;
    if(!allocation.release(0,owner)){r.status=Status::Ownership;return r;}
    r.status=Status::Complete;return r;
}
}}}
#endif
