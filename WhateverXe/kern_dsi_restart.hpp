// Display13 DSI0/Combo-A host restart. Register ordering: Intel icl_dsi.c
// (Linux v7.0), intel_combo_phy.c / intel_dpll_mgr.c (v6.12). Panel sequences,
// framebuffer lifetime and OS power references belong to the calling adapter.
#ifndef KERN_DSI_RESTART_HPP
#define KERN_DSI_RESTART_HPP
#include "kern_dsi_readout.hpp"
#include "kern_dsi_quiesce.hpp"
#include "kern_xelp_combo_clock.hpp"
namespace XELP { namespace DSI { namespace Restart {
enum class Stage : uint32_t {
    Input,ULPS,FunctionOff,BufferOff,ModeOff,PLLDisable,PLLPowerOff,Stopped,
    ModeOn,Escape,PLLPowerOn,PLLConfig,PLLEnable,Map,PHY,BufferOn,Timing,Protocol,Link,Ready
};
enum class Error : uint32_t {None,Input,Ownership,Consumer,Peer,Readback,Timeout};
struct Plan {
    bool valid {};LinkPlan link;PLLPlan pll;
    uint32_t init {},hsTimeout {},lpTimeout {},taTimeout {},function {},transport {};
};
inline Plan plan(const Profile &p,uint32_t referenceKHz) {
    Plan r;r.link=linkPlan(p);if(!r.link.valid || p.initTimer>65535)return r;
    r.pll=pllPlan(r.link.portKHz,referenceKHz);if(!r.pll.valid)return r;
    r.init=p.initTimer;
    // Linux's byte-count -> escape-count expression, evaluated in 64 bits.
    // Its u32 intermediates overflow for this profile. Do not silently rely
    // on that overflow (or copy previously programmed timeout registers).
    const uint64_t divisor=uint64_t(50)*r.link.afeKHz*1000;
    const auto timeout=[&](uint32_t bytes){return (uint64_t(bytes)*8000000+divisor-1)/divisor;};
    const auto hs=timeout(p.hsTxTimeout),lp=timeout(p.lpRxTimeout),ta=timeout(p.turnaroundTimeout);
    if(hs>65535 || lp>65535 || ta>65535)return r;
    r.hsTimeout=uint32_t(hs);r.lpTimeout=uint32_t(lp);r.taTimeout=uint32_t(ta);
    r.function=0x80000006U|(p.mode.hsyncPositive?1U<<16:0)|(p.mode.vsyncPositive?1U<<17:0);
    // RGB888, VC0, sync events, continuous HS clock, blanking packets, EOTP.
    r.transport=0x20030304;r.valid=true;return r;
}
struct State {
    Stage stage {Stage::Input};Error error {Error::None};Plan config;
    uint32_t pll {UINT32_MAX},reg {},value {},writes {},waitUs {};
    bool attempted {},stopped {},started {};
    bool fail(Error e){if(error==Error::None)error=e;return false;}
};
// Registers writable by this backend; used by the owning MMIO adapter.
inline bool writable(uint32_t reg,unsigned pllId) {
    XELPComboClock::PLL pll;if(pllId>=2 || !XELPComboClock::PLL::resolve(pllId,pll))return false;
    if(reg==pll.enable || reg==pll.cfg0 || reg==pll.cfg1)return true;
    switch(reg) {
        case 0x6B0D8:case 0x6B400:case 0x164280:case 0x64000:case 0x6B094:
        case 0x6B090:case 0x162190:case 0x1621A0:case 0x162028:case 0x162390:
        case 0x162388:case 0x162688:case 0x162304:case 0x162604:case 0x162014:
        case 0x162694:case 0x162394:case 0x162180:case 0x162184:case 0x6B088:
        case 0x6B080:case 0x6B084:case 0x6B000:case 0x6B008:case 0x6B00C:
        case 0x6B014:case 0x6B010:case 0x6B028:case 0x6B044:case 0x6B048:
        case 0x6B04C:case 0x6B030:case 0x6B034:case 0x6B404:case 0x6B0C0:return true;
    }
    for(unsigned lane=0;lane<4;++lane)if(reg==0x162890+0x100*lane)return true;
    return false;
}
inline Readout expected(const Readout &previous,const State &s) {
    if(!s.started || s.stage!=Stage::Ready || s.error!=Error::None || !s.config.valid ||
       !previous.complete() || previous.identity.pll!=s.pll)return {};
    auto r=previous;const auto &p=s.config;
    r.cfg0=p.pll.cfg0();r.cfg1=p.pll.cfg1();r.afeKHz=pllAFE(r.cfg0,r.cfg1,r.referenceKHz);
    r.function=p.function;r.transport=p.transport|(1U<<20);r.link|=1;r.mux|=1U<<10;
    r.buffer=(r.buffer&~0x80U)|(1U<<31);
    r.htotal=p.link.htotal;r.hsync=p.link.hsync;r.vtotal=p.link.vtotal;r.vsync=p.link.vsync;
    return r;
}
// Caller keeps Combo-clock -> display-owner serialization and IRQ producers
// parked for the entire operation. io.ready() verifies its real leases and
// object identity, including DDI IO power; this does not acquire any lease.
// Retain the explicit external clock owner through off/on, including failure.
// Do not clear shared power-well requests or release allocator state here.
template<class IO> struct Access {
    IO &io;XELPComboClock::ClockAllocation &allocation;uintptr_t owner;State &s;
    bool raw(uint32_t reg,uint32_t &v) {
        s.reg=reg;
        if(!io.readable(reg))return s.fail(Error::Readback);
        v=io.read(reg);s.value=v;return v!=UINT32_MAX || s.fail(Error::Readback);
    }
    bool bits(uint32_t reg,uint32_t mask,uint32_t expected) {
        uint32_t v=0;return raw(reg,v) && (v&mask)==expected;
    }
    bool held() {
        if(s.error!=Error::None)return false;
        if(!io.ready() || !owner || s.pll>=2 || allocation.external[0]!=owner ||
           allocation.binding[0]!=s.pll+1 || !bits(0x45404,0x403,0x403))return s.fail(Error::Ownership);
        // Pixel/DSB consumers must remain stopped while clocks/PHY change.
        if(!bits(0x7B008,0xC0000000U,0) || !bits(0x70008,0xC0000000U,0) ||
           !bits(0x60400,1U<<31,0) || !bits(0x70080,63,0) ||
           !bits(0x7B808,0xC0000000U,0) || !bits(0x6BC00,1U<<31,0))return s.fail(Error::Consumer);
        if(!bits(0x78000,0xF0300000U,0) || !bits(0x78004,0x80008000U,0) ||
           !bits(0x68180,1U<<31,0) || !bits(0x68280,1U<<31,0))return s.fail(Error::Consumer);
        for(unsigned n=0;n<7;++n)if(!bits(0x70180+0x100*n,1U<<31,0))return s.fail(Error::Consumer);
        for(unsigned n=0;n<3;++n)if(!bits(0x70B08+0x100*n,0x80000001U,0))return s.fail(Error::Consumer);
        XELPComboClock::ClockSnapshot snapshot;snapshot.layout=XELPComboClock::Layout::Display13;
        if(!raw(0x164280,snapshot.mux[0]))return false;
        if((snapshot.mux[0]&3)!=s.pll || (snapshot.peers(XELPComboClock::Port(0))&(1U<<s.pll)))return s.fail(Error::Peer);
        for(unsigned phy=1;phy<3;++phy)if(allocation.binding[phy]==s.pll+1)return s.fail(Error::Peer);
        return true;
    }
    bool read(uint32_t reg,uint32_t &v) {s.reg=reg;if(!held())return false;const bool ok=raw(reg,v);s.value=v;return ok;}
    bool write(uint32_t reg,uint32_t v) {
        s.reg=reg;if(!held() || !io.readable(reg))return s.fail(Error::Readback);
        s.reg=reg;io.write(reg,v);++s.writes;return true;
    }
    bool wait(uint32_t reg,uint32_t mask,uint32_t value,uint32_t budget,unsigned step=1) {
        for(unsigned us=0;;us+=step) {
            uint32_t v=0;if(!read(reg,v))return false;if((v&mask)==value)return true;
            if(us>=budget)return s.fail(Error::Timeout);
            io.delayUs(step);s.waitUs+=step;
        }
    }
    bool set(uint32_t reg,uint32_t value,uint32_t mask=UINT32_MAX) {
        return write(reg,value) && wait(reg,mask,value&mask,0);
    }
    bool rmw(uint32_t reg,uint32_t clear,uint32_t value) {
        uint32_t v=0;return read(reg,v) && set(reg,(v&~clear)|value,clear|value);
    }
    bool group(uint32_t lane0,uint32_t broadcast,uint32_t clear,uint32_t value) {
        uint32_t v=0;if(!read(lane0,v) || !write(broadcast,(v&~clear)|value))return false;
        // Broadcast registers are write aliases; inspect the four real lanes.
        for(unsigned lane=0;lane<4;++lane)if(!wait(lane0+0x100*lane,clear|value,value,0))return false;
        return true;
    }
};
// Shared tail for bootstrap stop and a runtime Quiesce proof. The latter
// retains backing, DDI power and external PLL ownership for display wake.
template<class IO> bool powerOff(Access<IO> &a,const XELPComboClock::PLL &pll) {
    auto &s=a.s;
    s.stage=Stage::ModeOff;if(!a.rmw(0x6B094,1,0))return false;
    s.stage=Stage::PLLDisable;
    if(!a.rmw(pll.enable,1U<<31,0) || !a.wait(pll.enable,0xC0000000U,0,1000,10))return false;
    s.stage=Stage::PLLPowerOff;
    if(!a.rmw(pll.enable,1U<<27,0) || !a.wait(pll.enable,0xCC000000U,0,1000,10))return false;
    s.stage=Stage::Stopped;s.stopped=true;return true;
}
template<class IO> bool stopQuiesced(IO &io,const Readout &source,const Profile &profile,
                                    const Quiesce::Result &host,XELPComboClock::ClockAllocation &allocation,
                                    uintptr_t owner,State &s) {
    if(s.attempted)return false;s.attempted=true;
    if(!host.complete() || !source.matches(profile) || source.identity.pipe!=0)return s.fail(Error::Input);
    s.config=plan(profile,source.referenceKHz);s.pll=source.identity.pll;
    if(!s.config.valid)return s.fail(Error::Input);
    Access<IO> a{io,allocation,owner,s};XELPComboClock::PLL pll;
    if(!XELPComboClock::PLL::resolve(s.pll,pll) || !a.held())return false;
    // Re-read consumers and ownership: a completed software receipt alone
    // cannot authorize stopping a currently active or reassigned PLL.
    if(!a.wait(pll.enable,0xCC000000U,0xCC000000U,0) ||
       !a.wait(pll.cfg0,UINT32_MAX,source.cfg0,0) || !a.wait(pll.cfg1,UINT32_MAX,source.cfg1,0) ||
       !a.wait(0x6B400,1U<<31,0,0) || !a.wait(0x64000,0x80000080U,0x80,0) ||
       !a.wait(0x164280,0x403,0x400|s.pll,0))return false;
    return powerOff(a,pll);
}
// Invoke only after plane/cursor/transcoder stop and DISPLAY_OFF/drain.
// Hardware readbacks recheck those preconditions; a caller assertion alone
// must not permit clock writes. This stop deliberately retains the owner.
template<class IO> bool stop(IO &io,const Readout &source,const Profile &profile,
                            XELPComboClock::ClockAllocation &allocation,uintptr_t owner,State &s) {
    if(s.attempted)return false;s.attempted=true;
    if(!source.matches(profile) || source.identity.pipe!=0)return s.fail(Error::Input);
    s.config=plan(profile,source.referenceKHz);s.pll=source.identity.pll;
    if(!s.config.valid)return s.fail(Error::Input);
    Access<IO> a{io,allocation,owner,s};XELPComboClock::PLL pll;
    if(!XELPComboClock::PLL::resolve(s.pll,pll) || !a.held())return false;
    if(!a.wait(pll.enable,0xCC000000U,0xCC000000U,0) ||
       !a.wait(pll.cfg0,UINT32_MAX,source.cfg0,0) || !a.wait(pll.cfg1,UINT32_MAX,source.cfg1,0) ||
       !a.wait(0x6B400,UINT32_MAX,source.function,0) ||
       !a.wait(0x6B0D0,0x1FFF,0x1040,0) || !a.wait(0x6B0D8,0x30001,0,0))return false;
    s.stage=Stage::ULPS;
    // LINK_ENTER_ULPS is a self-clearing request on the live Display13 host.
    // Completion is LINK_IN_ULPS, not an echoed request bit. Do not reject a
    // transition which already completed before the first posting read.
    uint32_t lp=0;
    if(!a.read(0x6B0D8,lp) || !a.write(0x6B0D8,(lp&~(1U<<8))|1U) ||
       !a.wait(0x6B0D8,1U<<16,1U<<16,10))return false;
    s.stage=Stage::FunctionOff;if(!a.rmw(0x6B400,1U<<31,0))return false;
    s.stage=Stage::BufferOff;
    if(!a.rmw(0x164280,1U<<10,0) || !a.rmw(0x64000,1U<<31,0) ||
       !a.wait(0x64000,0x80000080U,0x80,8) || !a.rmw(0x164280,0,1U<<10))return false;
    return powerOff(a,pll);
}
// A peer clock transaction may run while this host is parked. Revalidate the
// retained allocation and stopped hardware, without demanding the old live
// source or reacquiring an allocation whose PLL is intentionally powered off.
// Keep the stop receipt immutable; failed observation cannot advance it.
template<class IO> bool retainedStopped(IO &io,const Readout &source,
    XELPComboClock::ClockAllocation &allocation,uintptr_t owner,const State &state) {
    if(!source.complete() || source.identity.pipe!=0 || source.identity.phy!=0 ||
       source.identity.pll!=state.pll || !state.stopped || state.started ||
       state.stage!=Stage::Stopped || state.error!=Error::None || !state.config.valid)return false;
    State observed=state;Access<IO> a{io,allocation,owner,observed};XELPComboClock::PLL pll;
    return XELPComboClock::PLL::resolve(state.pll,pll) && a.held() &&
        a.bits(pll.enable,0xCC000000U,0) && a.bits(0x6B400,1U<<31,0) &&
        a.bits(0x64000,0x80000080U,0x80) && a.bits(0x6B094,1,0) &&
        a.bits(0x164280,0x403,0x400|state.pll);
}
// Invoke after panel POWER_ON/PPS/DEASSERT_RESET, before INIT_OTP. Timing and
// transport derive from the profile; no firmware PLL/PHY register dump replay.
// Leaves pixel transfer off, ready for command sequence initialization. The
// caller must enable video/plane, validate and publish the new source identity.
template<class IO> bool start(IO &io,XELPComboClock::ClockAllocation &allocation,uintptr_t owner,State &s) {
    if(!s.stopped || s.started || s.stage!=Stage::Stopped || s.error!=Error::None || !s.config.valid)return false;
    Access<IO> a{io,allocation,owner,s};XELPComboClock::PLL pll;
    if(!XELPComboClock::PLL::resolve(s.pll,pll) || !a.held())return false;
    if(!a.wait(pll.enable,0xCC000000U,0,0) || !a.wait(0x6B400,1U<<31,0,0) ||
       !a.wait(0x64000,0x80000080U,0x80,0) || !a.wait(0x164280,0x403,0x400|s.pll,0))return false;
    const auto &p=s.config;
    s.stage=Stage::ModeOn;if(!a.rmw(0x6B094,0,1))return false;
    s.stage=Stage::Escape;
    if(!a.set(0x6B090,p.link.escapeDivider,0x1FF) || !a.set(0x162190,p.link.escapeDivider,0x1FF) ||
       // Linux ADL_MIPIO_DW8 writes zero (unshifted divider & shifted mask),
       // matching the saved Linux state. Do not enable an unverified override.
       !a.set(0x1621A0,0,0xFF0000))return false;
    s.stage=Stage::PLLPowerOn;
    if(!a.rmw(pll.enable,0,1U<<27) || !a.wait(pll.enable,1U<<26,1U<<26,1000,10))return false;
    s.stage=Stage::PLLConfig;if(!a.set(pll.cfg0,p.pll.cfg0()) || !a.set(pll.cfg1,p.pll.cfg1()))return false;
    s.stage=Stage::PLLEnable;
    if(!a.rmw(pll.enable,0,1U<<31) || !a.wait(pll.enable,0xCC000000U,0xCC000000U,1000,10))return false;
    s.stage=Stage::Map;if(!a.rmw(0x164280,0x403,s.pll))return false;
    s.stage=Stage::PHY;
    if(!a.rmw(0x162028,0xF0,0) || !a.rmw(0x162390,1U<<31,0))return false;
    for(unsigned lane=0;lane<4;++lane)if(!a.rmw(0x162890+0x100*lane,1U<<31,lane==2?0:1U<<31))return false;
    if(!a.rmw(0x162388,0x700,0x500) || !a.group(0x162888,0x162688,0x700,0x500) ||
       !a.rmw(0x162304,12,0) || !a.group(0x162804,0x162604,12,4) ||
       !a.group(0x162804,0x162604,1U<<26,0) || !a.rmw(0x162304,1U<<26,0) ||
       !a.rmw(0x162014,0,3) ||
       !a.group(0x162894,0x162694,1U<<31,0) || !a.rmw(0x162394,1U<<31,0) ||
       !a.group(0x162894,0x162694,0x1C0038,0x60080030) || !a.rmw(0x162394,0x1C0038,0x60080030) ||
       !a.group(0x162888,0x162688,0xB8FF,0x1098) || !a.rmw(0x162388,0xB8FF,0x1098) ||
       !a.rmw(0x162390,0x3FFFF,0x3F))return false;
    for(unsigned lane=0;lane<4;++lane)if(!a.rmw(0x162890+0x100*lane,0x3FFFF,0x3F))return false;
    if(!a.group(0x162894,0x162694,0,1U<<31) || !a.rmw(0x162394,0,1U<<31) ||
       !a.set(0x162180,p.link.clockTiming) || !a.set(0x162184,p.link.dataTiming))return false;
    s.stage=Stage::BufferOn;
    if(!a.rmw(0x64000,0,1U<<31) || !a.wait(0x64000,0x80000080U,0x80000000U,500) ||
       !a.rmw(0x164280,0,1U<<10))return false;
    s.stage=Stage::Timing;
    if(!a.rmw(0x6B088,65535,p.init) || !a.set(0x6B080,p.link.clockTiming) || !a.set(0x6B084,p.link.dataTiming) ||
       !a.set(0x6B000,p.link.htotal) || !a.set(0x6B008,p.link.hsync) ||
       !a.set(0x6B00C,p.link.vtotal) || !a.set(0x6B014,p.link.vsync) ||
       !a.set(0x6B010,p.link.vtotal) || !a.set(0x6B028,0))return false;
    // VSYNCSHIFT is relevant to interlace; this supported profile is progressive.
    // Linux's unconditional interlace calculation is explicitly marked FIXME.
    s.stage=Stage::Protocol;
    // Timeout status bits are W1C; write only the new timeout field.
    if(!a.set(0x6B044,p.hsTimeout<<16,0xFFFF0000U) || !a.set(0x6B048,p.lpTimeout,65535) ||
       !a.set(0x6B04C,p.taTimeout,65535) || !a.set(0x6B034,0) || !a.set(0x6B404,0) ||
       !a.rmw(0x6B0C0,0xF000,0x4000) || !a.set(0x6B030,p.transport,~(1U<<20)) ||
       !a.set(0x6B400,p.function))return false;
    s.stage=Stage::Link;
    if(!a.wait(0x6B030,1U<<20,1U<<20,2500,10) || !a.wait(0x6B0D8,0x30001,0,2500,10) ||
       !a.wait(0x6B0D0,0x1FFF,0x1040,100,10))return false;
    s.stage=Stage::Ready;s.started=true;return true;
}
}}}
#endif
