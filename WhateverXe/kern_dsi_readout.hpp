// Decode a caller-owned, cached Display13 DSI0 snapshot. This does not acquire
// power, read MMIO, prove snapshot stability, or grant resource ownership.
// Registers: Intel i915_reg.h v6.12 and icl_dsi_regs.h/icl_dsi.c v7.0.
#ifndef KERN_DSI_READOUT_HPP
#define KERN_DSI_READOUT_HPP
#include "kern_dsi_math.hpp"
namespace XELP { namespace DSI {
struct Identity {
    uint32_t pipe {UINT32_MAX},transcoder {UINT32_MAX},phy {UINT32_MAX},pll {UINT32_MAX};
    bool valid() const {return pipe<3 && transcoder==0 && phy==0 && pll<2;}
    bool same(const Identity &b) const {return pipe==b.pipe && transcoder==b.transcoder && phy==b.phy && pll==b.pll;}
};
enum class ReadoutStatus : uint32_t {Missing, Disabled, Transition, Routing, Power, Transport, Clock, Timing, Complete};
struct Readout {
    uint32_t version {1};ReadoutStatus status {ReadoutStatus::Missing};
    Identity identity;
    uint32_t conf {},function {},transport {},link {},mux {},pllEnable {},cfg0 {},cfg1 {},referenceKHz {},afeKHz {};
    uint32_t normalConf {},wells {},buffer {},htotal {},hsync {},vtotal {},vsync {};
    bool complete() const {return status==ReadoutStatus::Complete;}
    uint32_t activePipes() const {return complete()?1U<<identity.pipe:0;}
    bool ddiClockGated() const {return mux&(1U<<10);}
    bool sameSource(const Readout &b) const {
        // Peer PHY muxes and power requests may change independently. Only
        // compare this DSI link's routing/PLL/timing; both decodes prove power.
        return complete() && b.complete() && identity.same(b.identity) &&
            conf==b.conf && function==b.function && transport==b.transport && link==b.link &&
            ((mux^b.mux)&0x403U)==0 && cfg0==b.cfg0 && cfg1==b.cfg1 &&
            referenceKHz==b.referenceKHz && buffer==b.buffer &&
            htotal==b.htotal && hsync==b.hsync && vtotal==b.vtotal && vsync==b.vsync;
    }
    bool matches(const Profile &profile,uint32_t tolerancePpm=100) const {
        const auto p=linkPlan(profile);
        if(!complete() || !p.valid || htotal!=p.htotal || hsync!=p.hsync || vtotal!=p.vtotal || vsync!=p.vsync)return false;
        const auto difference=afeKHz>p.afeKHz?afeKHz-p.afeKHz:p.afeKHz-afeKHz;
        return uint64_t(difference)*1000000<=uint64_t(p.afeKHz)*tolerancePpm;
    }
};
// get(reg,value) must inspect immutable cached words only. DSI timing belongs
// to transcoder0; frame/IRQ/planes belong to the separately decoded pipe.
template<class Get> Readout decodeReadout(Get get,uint32_t referenceKHz) {
    Readout r;r.referenceKHz=referenceKHz;
    const auto value=[&](uint32_t reg,uint32_t &out){return get(reg,out) && out!=UINT32_MAX;};
    if(!value(0x7B008,r.conf))return r;
    if(!(r.conf&0xC0000000U)){r.status=ReadoutStatus::Disabled;return r;}
    if((r.conf&0xC0000000U)!=0xC0000000U){r.status=ReadoutStatus::Transition;return r;}
    if(!value(0x6B400,r.function))return r;
    r.status=ReadoutStatus::Routing;
    const auto input=(r.function>>12)&7;
    const auto pipe=input==0?0U:input==5?1U:input==6?2U:UINT32_MAX;
    if(pipe==UINT32_MAX || !(r.function&0x80000000U))return r;
    r.identity.pipe=pipe;r.identity.transcoder=0;r.identity.phy=0;
    r.status=ReadoutStatus::Missing;
    if(!value(0x45404,r.wells) || !value(0x70008+0x1000*pipe,r.normalConf))return r;
    r.status=ReadoutStatus::Power;
    if(!(r.wells&1) || !(r.wells&(1U<<(10+2*pipe))) || (pipe && !(r.wells&4)))return r;
    r.status=ReadoutStatus::Routing;
    if(r.normalConf&0xC0000000U)return r; // two timing consumers for one pipe
    r.status=ReadoutStatus::Missing;uint32_t secondary=0,command=0;
    if(!value(0x6B030,r.transport) || !value(0x6B094,r.link) ||
       !value(0x6B404,secondary) || !value(0x6B034,command) || !value(0x64000,r.buffer))return r;
    r.status=ReadoutStatus::Transport;
    if((r.function&0x70000E)!=6 || secondary || command ||
       (r.transport&0x303F0000U)!=0x20130000U || !(r.link&1) || (r.buffer&0x80000080U)!=0x80000000U)return r;
    r.status=ReadoutStatus::Missing;
    if(!value(0x164280,r.mux))return r;
    r.identity.pll=r.mux&3;r.status=ReadoutStatus::Clock;
    // DSI deliberately gates the DDI clock after enabling its PHY/buffer
    // (gen11_dsi_enable_port_and_phy). This bit does NOT make its PLL free.
    if(r.identity.pll>=2)return r;
    r.status=ReadoutStatus::Missing;
    const auto pll=r.identity.pll;
    if(!value(0x46010+4*pll,r.pllEnable) || !value(0x164284+8*pll,r.cfg0) ||
       !value(0x164288+8*pll,r.cfg1))return r;
    r.status=ReadoutStatus::Clock;
    if((r.pllEnable&0xCC000000U)!=0xCC000000U)return r;
    r.afeKHz=pllAFE(r.cfg0,r.cfg1,referenceKHz);if(!r.afeKHz)return r;
    r.status=ReadoutStatus::Missing;
    if(!value(0x6B000,r.htotal) || !value(0x6B008,r.hsync) ||
       !value(0x6B00C,r.vtotal) || !value(0x6B014,r.vsync))return r;
    r.status=ReadoutStatus::Timing;
    const auto ordered=[](uint32_t total,uint32_t sync) {
        return (total&65535)<(sync&65535) && (sync&65535)<(sync>>16) && (sync>>16)<(total>>16);
    };
    if(!ordered(r.htotal,r.hsync) || !ordered(r.vtotal,r.vsync))return r;
    r.status=ReadoutStatus::Complete;return r;
}
// Revalidate an already owned source before/after a slow backing proof. The
// caller holds the DSI/pipe power leases and serializes mode changes. Checking
// the wells first avoids a DSI read after a visible power loss; this does not
// itself acquire leases or make unsynchronized firmware MMIO atomic.
template<class IO> bool sourceUnchanged(IO &io,const Readout &expected) {
    if(!expected.complete() || !expected.identity.valid() || !io.readable(0x45404) || !io.readable(0x51004))return false;
    const auto powered=[&]() {
        const auto wells=io.read(0x45404);const auto pipe=expected.identity.pipe;
        return wells!=UINT32_MAX && (wells&1) && (wells&(1U<<(10+2*pipe))) && (!pipe || (wells&4));
    };
    if(!powered())return false;
    const auto dssm=io.read(0x51004);const auto index=(dssm>>29)&7;
    const uint32_t reference[]={24000,19200,38400};
    if(index>=3 || reference[index]!=expected.referenceKHz)return false;
    const auto current=decodeReadout([&](uint32_t reg,uint32_t &v) {
        if(!io.readable(reg))return false;v=io.read(reg);return true;
    },reference[index]);
    return expected.sameSource(current) && powered();
}
}}
#endif
