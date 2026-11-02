// Display13 CDCLK. Linux v6.12 intel_cdclk.c defines adlp_cdclk_table,
// rplu_cdclk_table and the corresponding PCODE voltage levels.
// Keep the donor Hz ABI separate from the Linux CDCLK frequency. Transitions
// below require caller-owned clocks, serialized PCODE and qualified consumers.
#ifndef kern_display13_clock_hpp
#define kern_display13_clock_hpp
#include <stdint.h>

namespace Display13Clock {
constexpr uint32_t DSSM=0x51004, PLL=0x46070, Control=0x46000, Squash=0x46008;
enum class Variant {ADLP, RPLU};
constexpr Variant variantForPCI(uint32_t identity) {
    switch(identity) {
        case 0xA7218086: case 0xA7A18086: case 0xA7A98086: case 0xA7AC8086: case 0xA7AD8086:
            return Variant::RPLU;
        default:return Variant::ADLP;
    }
}
struct Entry {
    uint32_t ref,frequency,ratio;
    bool rpluOnly {};
    bool available(uint32_t reference,Variant variant) const {
        return ref==reference && (!rpluOnly || variant==Variant::RPLU);
    }
};
static constexpr Entry table[] {
    {19200,172800,27},{19200,192000,20},{19200,307200,32},{19200,480000,50,true},{19200,556800,58},{19200,652800,68},
    {24000,176000,22},{24000,192000,16},{24000,312000,26},{24000,480000,40,true},{24000,552000,46},{24000,648000,54},
    {38400,179200,14},{38400,192000,10},{38400,307200,16},{38400,480000,25,true},{38400,556800,29},{38400,652800,34}
};
enum class Status : uint32_t {
    NotReady, Unavailable, UnsupportedReference, Disabled, Unlocked,
    UnsupportedSquash, UnsupportedFrequency, DecimalMismatch, Changed, Complete
};
struct Readout {
    uint32_t version {1};
    Status status {Status::NotReady};
    uint32_t dssm {}, pll {}, control {}, squash {};
    uint32_t refIndex {}, refKHz {}, ratio {}, divider {}, cdclkKHz {}, maxKHz {};
    // The exact TGL donor's fPllRatioCdClkPair / controller E98/EA0 ABI uses
    // twice the Linux CDCLK frequency, in Hz. Never copy raw kHz into it.
    uint64_t nativeHz() const { return status==Status::Complete ? uint64_t(cdclkKHz)*2000 : 0; }
    uint64_t nativeMaxHz() const { return status==Status::Complete || status==Status::Disabled ? uint64_t(maxKHz)*2000 : 0; }
};
static_assert(sizeof(Readout)==12*4,"Display13 CDCLK readout layout");

inline Readout decode(uint32_t dssm,uint32_t pll,uint32_t control,uint32_t squash,Variant variant=Variant::ADLP) {
    Readout r; r.dssm=dssm;r.pll=pll;r.control=control;r.squash=squash;
    if(dssm==UINT32_MAX || pll==UINT32_MAX || control==UINT32_MAX || squash==UINT32_MAX) {
        r.status=Status::Unavailable;return r;
    }
    r.refIndex=(dssm>>29)&7;
    const uint32_t references[] {24000,19200,38400};
    if(r.refIndex>=3) {r.status=Status::UnsupportedReference;return r;}
    r.refKHz=references[r.refIndex];r.ratio=pll&0xFF;
    for(const auto &e:table)if(e.available(r.refKHz,variant) && e.frequency>r.maxKHz)r.maxKHz=e.frequency;
    if(!(pll&(1U<<31))) {r.status=Status::Disabled;return r;}
    if(!(pll&(1U<<30))) {r.status=Status::Unlocked;return r;}
    // ADLP's supported table has no squash waveforms. Do not interpret a
    // squashed clock with the unsquashed ADLP table or silently ignore it.
    if(squash&(1U<<31)) {r.status=Status::UnsupportedSquash;return r;}
    const uint32_t divisors[] {2,3,4,8};
    r.divider=divisors[(control>>22)&3];
    const uint32_t vco=r.refKHz*r.ratio;
    r.cdclkKHz=(vco+r.divider/2)/r.divider;
    bool found=false;
    for(const auto &e:table) if(e.available(r.refKHz,variant)) {
        if(e.frequency>r.maxKHz)r.maxKHz=e.frequency;
        if(e.ratio==r.ratio && e.frequency==r.cdclkKHz)found=true;
    }
    if(!found) {r.status=Status::UnsupportedFrequency;return r;}
    // CDCLK_FREQ_DECIMAL is rounded 2*MHz minus two, independent of the
    // donor's Hz ABI. Validate it against PLL/divider, not the other way round.
    const auto decimal=(2*r.cdclkKHz+500)/1000-2;
    if((control&0x7FF)!=decimal) {r.status=Status::DecimalMismatch;return r;}
    r.status=Status::Complete;return r;
}

template<class IO> Readout read(IO &io,Variant variant=Variant::ADLP) {
    Readout r;
    const uint32_t registers[] {DSSM,PLL,Control,Squash};
    for(auto reg:registers) if(!io.readable(reg))return r;
    const auto dssm=io.read(DSSM),pll=io.read(PLL),control=io.read(Control),squash=io.read(Squash);
    r=decode(dssm,pll,control,squash,variant);
    // A concurrent clock transition must not publish a mixed native value.
    if(io.read(DSSM)!=dssm || io.read(PLL)!=pll || io.read(Control)!=control || io.read(Squash)!=squash)
        r.status=Status::Changed;
    return r;
}

// Display13 PLL/PCODE transition. Caller serializes *all* mailbox clients,
// holds PW1, owns display clocks, and proves programmed watermarks fit the new
// clock before changing it. Changed divider / cold start requires every pipe off.
constexpr uint32_t Mailbox=0x138124, Data=0x138128, Data1=0x13812C;
enum class ChangeStatus : uint32_t {
    NotReady, InvalidFrequency, Unavailable, OwnershipRequired, WatermarksRequired,
    ActiveDividerChange, MailboxBusy, MailboxTimeout, MailboxError, PrepareTimeout,
    PllTimeout, ReadbackMismatch, Complete
};
struct Change {
    uint32_t version {1};
    ChangeStatus status {ChangeStatus::NotReady};
    uint32_t writes {}, delayUs {}, mailboxReply {}, mailboxStatus {};
    uint64_t requestedHz {};
    Readout before,after;
};
struct Conditions {
    bool owned {}, watermarksReady {};
    uint32_t activePipes {};
};
inline bool plan(uint32_t reference,uint64_t nativeHz,uint32_t &ratio,uint32_t &divisor,uint32_t &control,Variant variant=Variant::ADLP) {
    for(const auto &e:table) if(e.available(reference,variant) && uint64_t(e.frequency)*2000==nativeHz) {
        ratio=e.ratio;divisor=e.ref*e.ratio/e.frequency;
        control=(divisor==3 ? 1U<<22:0U)|0x380000|((2*e.frequency+500)/1000-2);
        return true;
    }
    return false;
}
inline uint32_t voltage(uint32_t cdclkKHz,Variant variant=Variant::ADLP) {
    const uint32_t level1Max=variant==Variant::RPLU ? 480000 : 326400;
    return cdclkKHz<=312000 ? 0:cdclkKHz<=level1Max ? 1:cdclkKHz<=556800 ? 2:3;
}
// calculateCDClockFrequency's HDMI branch requests the maximum table entry.
// The other branch takes a pixel-clock lower bound, with a strict comparison
// at table thresholds. Return an actual Display D0 entry in the donor's Hz ABI.
inline uint64_t select(uint32_t reference,uint64_t minimumHz,bool external,Variant variant=Variant::ADLP) {
    uint64_t selected=0;
    for(const auto &e:table)if(e.available(reference,variant)) {
        const auto hz=uint64_t(e.frequency)*2000;
        if(external)selected=hz;
        else if(hz>minimumHz)return hz;
    }
    return selected;
}
template<class IO> bool value(IO &io,uint32_t reg,uint32_t &v) {
    if(!io.readable(reg))return false;
    v=io.read(reg);return v!=UINT32_MAX;
}
template<class IO> void write(IO &io,Change &r,uint32_t reg,uint32_t v) {
    io.write(reg,v);++r.writes;
}
template<class IO> void delay(IO &io,Change &r,unsigned us) {io.delayUs(us);r.delayUs+=us;}
template<class IO> bool wait(IO &io,Change &r,uint32_t reg,uint32_t mask,uint32_t expected,unsigned timeoutUs) {
    for(unsigned elapsed=0;;elapsed+=10) {
        uint32_t v;
        if(!value(io,reg,v)) {r.status=ChangeStatus::Unavailable;return false;}
        if((v&mask)==expected)return true;
        if(elapsed>=timeoutUs)return false;
        delay(io,r,10);
    }
}
template<class IO> bool mailbox(IO &io,Change &r,uint32_t data,unsigned budgetUs=1000) {
    const auto start=r.delayUs;
    uint32_t v;
    if(!value(io,Mailbox,v)) {r.status=ChangeStatus::Unavailable;return false;}
    if((v&(1U<<31)) && !wait(io,r,Mailbox,1U<<31,0,budgetUs<500 ? budgetUs:500)) {
        if(r.status!=ChangeStatus::Unavailable)r.status=ChangeStatus::MailboxBusy;
        return false;
    }
    write(io,r,Data,data);write(io,r,Data1,0);write(io,r,Mailbox,0x80000007);
    const auto remaining=budgetUs-(r.delayUs-start);
    if(!wait(io,r,Mailbox,1U<<31,0,remaining<500 ? remaining:500)) {
        if(r.status!=ChangeStatus::Unavailable)r.status=ChangeStatus::MailboxTimeout;
        return false;
    }
    if(!value(io,Mailbox,r.mailboxStatus) || !value(io,Data,r.mailboxReply)) {
        r.status=ChangeStatus::Unavailable;return false;
    }
    if(r.mailboxStatus&0xFF) {r.status=ChangeStatus::MailboxError;return false;}
    return true;
}
template<class IO> Change set(IO &io,uint64_t nativeHz,Conditions conditions,Variant variant=Variant::ADLP) {
    Change r;r.requestedHz=nativeHz;r.before=read(io,variant);
    if(r.before.status!=Status::Complete && r.before.status!=Status::Disabled)return r;
    if(r.before.squash&(1U<<31))return r;
    uint32_t ratio=0,divisor=0,control=0;
    if(!plan(r.before.refKHz,nativeHz,ratio,divisor,control,variant)) {r.status=ChangeStatus::InvalidFrequency;return r;}
    if(r.before.status==Status::Complete && r.before.nativeHz()==nativeHz) {
        r.after=read(io,variant);
        r.status=r.after.status==Status::Complete && r.after.nativeHz()==nativeHz ? ChangeStatus::Complete:ChangeStatus::ReadbackMismatch;
        return r; // Actual unchanged clock, no invented fixed-frequency success.
    }
    if(!conditions.owned) {r.status=ChangeStatus::OwnershipRequired;return r;}
    if(!conditions.watermarksReady) {r.status=ChangeStatus::WatermarksRequired;return r;}
    if(conditions.activePipes && (r.before.status!=Status::Complete || r.before.divider!=divisor)) {
        r.status=ChangeStatus::ActiveDividerChange;return r;
    }
    const uint32_t registers[] {Mailbox,Data,Data1,PLL,Control};
    for(auto reg:registers) {uint32_t v;if(!value(io,reg,v)) {r.status=ChangeStatus::Unavailable;return r;}}
    // Linux uses a 3ms base plus a 50ms bounded retry. Include mailbox waits
    // in this budget; never disable preemption for a long firmware poll here.
    const auto start=r.delayUs;
    for(;;) {
        const auto remaining=53000-(r.delayUs-start);
        if(!mailbox(io,r,3,remaining))return r;
        if(r.mailboxReply&1)break;
        if(r.delayUs-start>=53000) {r.status=ChangeStatus::PrepareTimeout;return r;}
        delay(io,r,10);
    }
    const auto prepared=read(io,variant);
    if(prepared.status!=r.before.status || prepared.dssm!=r.before.dssm || prepared.pll!=r.before.pll ||
       prepared.control!=r.before.control || prepared.squash!=r.before.squash) {
        r.after=prepared;r.status=ChangeStatus::ReadbackMismatch;return r;
    }
    if(r.before.status==Status::Complete) {
        if(r.before.ratio!=ratio) {
            write(io,r,PLL,0x80000000|ratio);
            write(io,r,PLL,0x80800000|ratio);
            if(!wait(io,r,PLL,0x40400000,0x40400000,1000)) {
                if(r.status!=ChangeStatus::Unavailable)r.status=ChangeStatus::PllTimeout;
                return r; // Keep request/maximum voltage after uncertain change.
            }
            write(io,r,PLL,0x80000000|ratio);
            if(!wait(io,r,PLL,0xC0C00000,0xC0000000,1000)) {
                if(r.status!=ChangeStatus::Unavailable)r.status=ChangeStatus::PllTimeout;
                return r;
            }
        }
    } else {
        write(io,r,PLL,ratio);write(io,r,PLL,0x80000000|ratio);
        if(!wait(io,r,PLL,1U<<30,1U<<30,1000)) {
            if(r.status!=ChangeStatus::Unavailable)r.status=ChangeStatus::PllTimeout;
            return r;
        }
    }
    write(io,r,Control,control);
    // Read back before lowering the prepare voltage. Failed PLL/decimal
    // proof must not lower voltage or attempt an unproven rollback.
    r.after=read(io,variant);
    if(r.after.status!=Status::Complete || r.after.nativeHz()!=nativeHz) {r.status=ChangeStatus::ReadbackMismatch;return r;}
    if(!mailbox(io,r,voltage(r.after.cdclkKHz,variant)))return r;
    r.after=read(io,variant);
    r.status=r.after.status==Status::Complete && r.after.nativeHz()==nativeHz ? ChangeStatus::Complete:ChangeStatus::ReadbackMismatch;
    return r;
}

// Caller proves every pipe/plane/cursor/DSB stopped and holds PW1 throughout.
// Follow icl_cdclk_pll_disable and the Display >=12 bypass control, including
// PCODE prepare/final voltage. Never report a disabled PLL before LOCK clears.
template<class IO> Change stop(IO &io,Conditions conditions,Variant variant=Variant::ADLP) {
    Change r;r.before=read(io,variant);
    if(r.before.status!=Status::Complete && r.before.status!=Status::Disabled)return r;
    if((r.before.squash&(1U<<31)) || (r.before.pll&0xC00000))return r;
    if(!conditions.owned) {r.status=ChangeStatus::OwnershipRequired;return r;}
    if(conditions.activePipes) {r.status=ChangeStatus::ActiveDividerChange;return r;}
    if(r.before.status==Status::Disabled && !(r.before.pll&(1U<<30))) {
        r.after=r.before;r.status=ChangeStatus::Complete;return r;
    }
    const uint32_t registers[] {Mailbox,Data,Data1,PLL,Control};
    for(auto reg:registers) {uint32_t v;if(!value(io,reg,v)) {r.status=ChangeStatus::Unavailable;return r;}}
    const auto start=r.delayUs;
    for(;;) {
        const auto remaining=53000-(r.delayUs-start);
        if(!mailbox(io,r,3,remaining))return r;
        if(r.mailboxReply&1)break;
        if(r.delayUs-start>=53000) {r.status=ChangeStatus::PrepareTimeout;return r;}
        delay(io,r,10);
    }
    const auto prepared=read(io,variant);
    if(prepared.status!=r.before.status || prepared.dssm!=r.before.dssm || prepared.pll!=r.before.pll ||
       prepared.control!=r.before.control || prepared.squash!=r.before.squash) {
        r.after=prepared;r.status=ChangeStatus::ReadbackMismatch;return r;
    }
    write(io,r,PLL,r.before.pll&~(1U<<31));
    if(!wait(io,r,PLL,1U<<30,0,1000)) {
        if(r.status!=ChangeStatus::Unavailable)r.status=ChangeStatus::PllTimeout;
        return r;
    }
    const auto bypass=r.before.refKHz/2;
    write(io,r,Control,0x380000|((2*bypass+500)/1000-2));
    r.after=read(io,variant);
    if(r.after.status!=Status::Disabled || (r.after.pll&0xC0000000) ||
       r.after.control!=(0x380000|((2*bypass+500)/1000-2)) || r.after.dssm!=r.before.dssm) {
        r.status=ChangeStatus::ReadbackMismatch;return r;
    }
    if(!mailbox(io,r,voltage(bypass)))return r;
    r.after=read(io,variant);
    r.status=r.after.status==Status::Disabled && !(r.after.pll&0xC0000000) &&
        r.after.control==(0x380000|((2*bypass+500)/1000-2)) && r.after.dssm==r.before.dssm ?
        ChangeStatus::Complete:ChangeStatus::ReadbackMismatch;
    return r;
}
}
#endif
