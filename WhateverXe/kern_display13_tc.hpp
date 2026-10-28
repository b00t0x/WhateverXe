// ADL-P/N TC HDMI/DP coordinates and PHY ownership.
// Linux v6.12 intel_tc.c/i915_reg.h; T8 Pro TC1 Linux values are an
// independent live fixture. These are not TGL FIA ownership registers.
#ifndef KERN_DISPLAY13_TC_HPP
#define KERN_DISPLAY13_TC_HPP
#include <stdint.h>

namespace Display13TC {
struct Port {
    uint32_t ddi {}, tc {}, ready {}, buffer {}, clock {}, pll {}, phy {}, hip {}, shift {};
    static bool resolve(unsigned ddi,Port &p) {
        p={};if(ddi<3 || ddi>6)return false;
        const auto tc=ddi-3;
        // ADLP_PORTTC_PLL_ENABLE stride is 8, not TGL's 4-byte stride.
        p={ddi,tc,0x161500+4*tc,0x64000+0x100*ddi,0x46100+4*ddi,
            0x46038+8*tc,0x168000+0x1000*tc,0x1010A0,8*tc};
        return true;
    }
    bool legacyHPD(uint32_t sde) const {return sde!=UINT32_MAX && (sde&(1U<<(24+tc)));}
    bool altHPD(uint32_t sde,uint32_t north) const {
        return sde!=UINT32_MAX && north!=UINT32_MAX && !legacyHPD(sde) &&
            !(north&(1U<<tc)) && (north&(1U<<(16+tc)));
    }
    uint32_t laneAssignment() const {return (tc<2?0x163000:0x16e000)+0x8a0;}
    uint32_t pinAssignment() const {return (tc<2?0x163000:0x16e000)+0x880;}
    static bool modeMatches(uint32_t ready,unsigned mode) {
        return ready!=UINT32_MAX && (mode==4 ? !(ready&3) : mode==3 && (ready&3)==1);
    }
    uint32_t bank(uint32_t previous,uint32_t internal) const {
        return (previous&~(15U<<shift))|(((internal>>12)&15U)<<shift);
    }
    uint32_t phyRegister(uint32_t internal) const {return phy+(internal&0xFFF);}
};
enum class Status : uint32_t {Invalid,NoPower,NeedsAdoption,InUse,Unavailable,UnsupportedMode,NotReady,Readback,Complete};
struct Result {Status status {Status::Invalid};uint32_t before {},after {},writes {},elapsedUs {};};
// Read only after ownership and AUX cold blocking. Physical assignment is
// distinct from logical main-link lane enable and may change on reconnect.
struct Lanes {
    uint32_t mask {},pin {};
    unsigned count() const {unsigned n=0;for(unsigned b=0;b<4;++b)n+=(mask>>b)&1;return n;}
    bool valid() const {
        return (mask==1 || mask==2 || mask==4 || mask==8 || mask==3 || mask==12 || mask==15) && pin>=1 && pin<=6;
    }
    template<class IO> static Lanes capture(IO &io,const Port &p) {
        if(!io.readable(p.laneAssignment()) || !io.readable(p.pinAssignment()) ||
           !io.readable(0xc4000) || !io.readable(0x44470) || !io.readable(p.ready))return {};
        const auto lanes=io.read(p.laneAssignment()),pins=io.read(p.pinAssignment());
        if(lanes==UINT32_MAX || pins==UINT32_MAX || !Port::modeMatches(io.read(p.ready),3) ||
           !p.altHPD(io.read(0xc4000),io.read(0x44470)))return {};
        Lanes r{(lanes>>(8*(p.tc&1)))&15,(pins>>(4*(p.tc&1)))&15};
        return r.valid()?r:Lanes{};
    }
    template<class IO> bool unchanged(IO &io,const Port &p) const {
        const auto now=capture(io,p);return valid() && now.mask==mask && now.pin==pin;
    }
};
// Read-only physical certificate for an inherited fixed link. This never takes
// ownership or programs the PLL. Callers must select the physical Display13
// backend before reading TCSS; TGL uses a different ownership mechanism.
// Protocol/source, sink and PLL frequency are separate certificates. An
// enabled/locked PLL alone does not prove a valid DP link or authorize adoption.
struct Boot {
    Port port;unsigned pipe {3};
    uint32_t power {},ddiPower {},ready {},buffer {},clock {},pll {},transClock {};
    // Linux intel_tc_port_init_mode reconnects an enabled fixed legacy port
    // left unowned by firmware. Only explicit DP source validation may request
    // this witness; the later Mode acquisition must still take ownership.
    bool ownershipOptional {};
    bool valid() const {
        const auto powered=pipe<3 ? 5U|(1U<<(10+2*pipe)) : 0U;
        return port.ddi>=3 && port.ddi<=6 && pipe<3 &&
            power!=UINT32_MAX && (power&powered)==powered && ddiPower!=UINT32_MAX &&
            (ddiPower&(1U<<(2*port.ddi))) && ready!=UINT32_MAX && (ready&7)==4 &&
            buffer!=UINT32_MAX && (buffer&0x80000080U)==0x80000000U && (ownershipOptional || (buffer&64)) &&
            clock!=UINT32_MAX && (clock>>28)==8 && pll!=UINT32_MAX &&
            (pll&0xCC000000U)==0xCC000000U && transClock!=UINT32_MAX && (transClock>>28)==port.ddi+3;
    }
    template<class IO> static Boot capture(IO &io,bool admitted,unsigned ddi,unsigned pipe,bool ownershipOptional=false) {
        Boot b;
        if(!admitted || pipe>=3 || !Port::resolve(ddi,b.port))return b;
        b.pipe=pipe;b.ownershipOptional=ownershipOptional;
        if(!io.readable(0x45404) || !io.readable(0x45454))return {};
        b.power=io.read(0x45404);b.ddiPower=io.read(0x45454);
        const auto powered=5U|(1U<<(10+2*pipe));
        if(b.power==UINT32_MAX || (b.power&powered)!=powered || b.ddiPower==UINT32_MAX ||
           !(b.ddiPower&(1U<<(2*ddi))))return {};
        const uint32_t regs[]={b.port.ready,b.port.buffer,b.port.clock,b.port.pll,0x46140+4*pipe};
        uint32_t *values[]={&b.ready,&b.buffer,&b.clock,&b.pll,&b.transClock};
        for(unsigned i=0;i<5;++i) {
            if(!io.readable(regs[i]))return {};
            *values[i]=io.read(regs[i]);
        }
        if(io.read(0x45404)!=b.power || io.read(0x45454)!=b.ddiPower || !b.valid())return {};
        return b;
    }
    template<class IO> bool unchanged(IO &io) const {
        if(!valid())return false;
        const auto now=capture(io,true,port.ddi,pipe,ownershipOptional);
        return now.valid() && now.power==power && now.ddiPower==ddiPower && now.ready==ready &&
            now.buffer==buffer && now.clock==clock && now.pll==pll && now.transClock==transClock;
    }
};
// Caller keeps lifecycle/port exclusion, live mapping and lane/PW2 access.
// ADLP ownership precedes AUX cold blocking and DDI IO acquisition. Requiring
// those later scopes here creates an inverted dependency on a cold port.
// A matching owner identity is insufficient without that power witness.
struct Ownership {
    uintptr_t owner {},identity {};Port port;bool active {};
    Ownership()=default;Ownership(const Ownership&)=delete;Ownership& operator=(const Ownership&)=delete;
    template<class IO> Result acquire(IO &io,uintptr_t who,uintptr_t object,unsigned ddi,bool adopt=false,unsigned mode=4) {
        Result r;Port p;if(!who || !object || !Port::resolve(ddi,p))return r;
        if(owner && (owner!=who || identity!=object || port.ddi!=ddi)){r.status=Status::InUse;return r;}
        if(!io.powerHeld(who,object,ddi)){r.status=Status::NoPower;return r;}
        if(!io.readable(p.ready) || !io.readable(p.buffer)){r.status=Status::Unavailable;return r;}
        const auto initialReady=io.read(p.ready);
        if(initialReady==UINT32_MAX){r.status=Status::Unavailable;return r;}
        if(!Port::modeMatches(initialReady,mode)){r.status=Status::UnsupportedMode;return r;}
        r.before=io.read(p.buffer);if(r.before==UINT32_MAX){r.status=Status::Unavailable;return r;}
        if(owner && active && !(r.before&64)) {r.status=Status::Readback;return r;}
        if(!owner && ((r.before&0x80000040U)!=0) && (!adopt || !io.inheritedVerified(who,object,ddi))) {
            r.status=Status::NeedsAdoption;return r;
        }
        // Record identity before a write so failed readback retains the exact
        // cleanup obligation and cannot be reused for a different port.
        owner=who;identity=object;port=p;
        if(!(r.before&64)){io.write(p.buffer,r.before|64);++r.writes;}
        r.after=io.read(p.buffer);
        if(r.after==UINT32_MAX){r.status=Status::Unavailable;return r;}
        if(r.after!=(r.before|64)){r.status=Status::Readback;return r;}
        // Linux adlp_tc_phy_connect takes ownership before testing ready.
        // Retain the identity on timeout so cleanup can release only our bit.
        for(;;) {
            const auto ready=io.read(p.ready);
            if(ready==UINT32_MAX){r.status=Status::Unavailable;return r;}
            if(!Port::modeMatches(ready,mode)){r.status=Status::UnsupportedMode;return r;}
            if(ready&4)break;
            if(r.elapsedUs==500000){r.status=Status::NotReady;return r;}
            io.delayUs(100);r.elapsedUs+=100;
        }
        active=true;r.status=Status::Complete;return r;
    }
    template<class IO> Result release(IO &io,uintptr_t who,uintptr_t object,unsigned ddi) {
        Result r;
        if(!owner || owner!=who || identity!=object || port.ddi!=ddi)return r;
        if(!io.powerHeld(who,object,ddi)){r.status=Status::NoPower;return r;}
        if(!io.quiescent(who,object,ddi)){r.status=Status::InUse;return r;}
        if(!io.readable(port.buffer)){r.status=Status::Unavailable;return r;}
        r.before=io.read(port.buffer);if(r.before==UINT32_MAX){r.status=Status::Unavailable;return r;}
        // Never hand the PHY away while the DDI is physically enabled.
        if(r.before&0x80000000U){r.status=Status::InUse;return r;}
        if(r.before&64){io.write(port.buffer,r.before&~64U);++r.writes;}
        r.after=io.read(port.buffer);
        if(r.after==UINT32_MAX){r.status=Status::Unavailable;return r;}
        if(r.after!=(r.before&~64U)){r.status=Status::Readback;return r;}
        owner=identity=0;port={};active=false;r.status=Status::Complete;return r;
    }
};
// HAL's exact table retains PHY1 placeholders and a TGL PLL base. Resolve
// them at the access boundary; do not rewrite unrelated donor object fields.
struct Dekel {
    static bool resolve(const Port &p,unsigned index,uint32_t raw,uint32_t &offset,uint32_t &internal,bool &direct) {
        constexpr uint32_t table[] {0x20D4,0x20D8,0x2130,0x212C,0x46030,0x2200,0x2204,0x2208,
            0x168210,0x168214,0x168218,0x220C,0x2244,0x224C,0x2C0,0x2C4,0x2C8,0x12C0,0x12C4,0x12C8,0xA0,0x10A0};
        Port checked;if(!Port::resolve(p.ddi,checked) || p.pll!=checked.pll || p.phy!=checked.phy ||
            p.hip!=checked.hip || p.shift!=checked.shift || index<0x34 || index>0x49 || raw!=table[index-0x34])return false;
        direct=index==0x38;internal=index>=0x3C && index<=0x3E ? 0x2210+4*(index-0x3C) : raw;
        offset=direct?p.pll:p.phyRegister(internal);return true;
    }
    static uint32_t writeMask(unsigned index) {
        switch(index){
            case 0x39:return 0x001FFFFF;case 0x3A:return 0x001F00FF;
            case 0x3C:return 0xE0FF3A00;case 0x3D:return 0x7FFFFF00;case 0x3E:return 0x0000FFFF;
            default:return UINT32_MAX;
        }
    }
    // IO lock/unlock serialize the shared HIP word and preserve IRQ state.
    // A reference to power is a host proof, not an MMIO ready-bit shortcut.
    template<class IO> static Result access(IO &io,const Port &p,unsigned index,uint32_t raw,bool write,uint32_t &value) {
        Result r;uint32_t offset=0,internal=0;bool direct=false;
        if(!resolve(p,index,raw,offset,internal,direct))return r;
        if(!io.powerHeld()){r.status=Status::NoPower;return r;}
        if(!io.readable(offset) || (!direct && !io.readable(p.hip))){r.status=Status::Unavailable;return r;}
        const auto irq=io.lock();
        uint32_t hip=0;bool changed=false;
        if(!direct) {
            hip=io.read(p.hip);
            if(hip==UINT32_MAX){io.unlock(irq);r.status=Status::Unavailable;return r;}
            const auto selected=p.bank(hip,internal);
            if(selected!=hip){io.write(p.hip,selected);++r.writes;changed=true;}
            if(io.read(p.hip)!=selected){r.status=Status::Readback;}
        }
        if(r.status==Status::Invalid) {
            r.before=io.read(offset);
            if(r.before==UINT32_MAX)r.status=Status::Unavailable;
            else {
                if(write) {
                    const auto mask=writeMask(index),desired=(r.before&~mask)|(value&mask);
                    io.write(offset,desired);++r.writes;r.after=io.read(offset);
                    if(r.after!=desired)r.status=Status::Readback;
                    else {value=desired;r.status=Status::Complete;}
                } else {value=r.before;r.after=r.before;r.status=Status::Complete;}
            }
        }
        if(changed) {
            io.write(p.hip,hip);++r.writes;
            if(io.read(p.hip)!=hip)r.status=Status::Readback;
        }
        io.unlock(irq);return r;
    }
};
}
#endif
