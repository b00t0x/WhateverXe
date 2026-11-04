// Display 13 DE interrupt banks and the exact native TGL event encoding.
// Caller serializes with the controller's IRQ simple lock (also in hard IRQ).
#ifndef KERN_DISPLAY13_FB_IRQ_HPP
#define KERN_DISPLAY13_FB_IRQ_HPP
#include <stdint.h>
#include "kern_display13_power_bridge.hpp"
#include "kern_display13_transcoder.hpp"
namespace Display13FBIRQ {
// The IRQ simple lock protects this list. Each node lives on its real native
// handler's stack until end(). A later master restore cannot grant DE access
// to a handler which entered during quiescence. No allocation or fabricated
// handler/queue retirement is involved; nested same-thread entries use the
// most recent scope and restore their outer scope when they return.
struct HandlerScope {
    HandlerScope *next {};
    uintptr_t thread {};
    bool display {};
};
struct HandlerScopes {
    HandlerScope *head {};
    uint32_t total {},display {};
    bool begin(HandlerScope &s,uintptr_t thread,bool allowed) {
        if(!thread || s.thread || s.next || total==UINT32_MAX)return false;
        s.next=head;s.thread=thread;s.display=allowed;head=&s;++total;
        if(allowed)++display;
        return true;
    }
    bool permits(uintptr_t thread) const {
        for(auto p=head;p;p=p->next)if(p->thread==thread)return p->display;
        return false;
    }
    bool end(HandlerScope &s) {
        auto link=&head;while(*link && *link!=&s)link=&(*link)->next;
        if(!*link || !total || (s.display && !display))return false;
        *link=s.next;--total;if(s.display)--display;s={};return true;
    }
};
constexpr uint32_t Driver=0x45404, Master=0x44200, GfxMaster=0x190010;
constexpr uint32_t Enable=1U<<31, PipeOwned=0x8060E00D, PCHOwned=3U<<16;
constexpr uint32_t PCHTypeC=0x3F000000, PCHCombo=7U<<16;
constexpr uint32_t TCControl=0xC4034;
constexpr uint32_t DDIControl=0xC4030, DDIStatus=0x33, DDIEnable=0x88;
constexpr uint32_t PipeIIR(unsigned pipe) {return 0x44408+16*pipe;}
constexpr uint32_t DSBInterrupt(unsigned pipe,unsigned dsb) {return 0x70B28+0x1000*pipe+0x100*dsb;}
// Linux intel_dsb_regs.h: HEAD +0, TAIL +4, CTRL +8, INTERRUPT +0x28.
// Only CTRL contains ENABLE/BUSY; a nonzero HEAD is not execution state.
constexpr uint32_t DSBControl(unsigned pipe,unsigned dsb) {return DSBInterrupt(pipe,dsb)-0x20;}
enum class Status : uint32_t {Unavailable,Complete,Power,ForeignIRQ,Topology,Readback,Faulted};
struct State {
    Display13Transcoder::Bindings transcoders;
    bool begun {},enabled {},faulted {};
    Status firstFailure {Status::Unavailable};
    uint32_t failureRegister {},failureBits {};
    uint32_t excludedPCH {},ports {3},typeC {};
    uint32_t active {},configured {},priorIER[3] {},priorIMR[3] {},priorPCHIER {},priorPCHIMR {};
    uint32_t priorTCEnable {},priorDDIEnable {},priorDSB[3][3] {},lastMaster {},raw[3] {},lastPCH {},lastDDI {},lastTC {},foreign {};
    uint64_t interrupts {},vblanks[3] {},flips[3] {},underruns[3] {},dsbErrors[3] {};
};
struct Events {uint32_t pipe {},aux {},hotplug {};Status status {Status::Unavailable};};
struct ActivePipes {Status status {Status::Unavailable};uint32_t active {},allowed {};};
inline bool powered(uint32_t wells,unsigned pipe) {
    if(wells==UINT32_MAX || pipe>=3 || (wells&3)!=3)return false;
    return (wells&(1U<<(10+2*pipe))) && (!pipe || (wells&(1U<<2)));
}
template<class IO> ActivePipes readActivePipes(IO &io,uint32_t allowed,const Display13Transcoder::Bindings &transcoders={}) {
    ActivePipes r;r.allowed=allowed;
    if(!allowed || (allowed&~7U) || !transcoders.valid()) {r.status=Status::Topology;return r;}
    const auto wells=io.read(Driver);if(wells==UINT32_MAX) {r.status=Status::Readback;return r;}
    if(transcoders.dsi0Pipe<3 && !powered(wells,transcoders.dsi0Pipe)) {r.status=Status::Power;return r;}
    for(unsigned p=0;p<3;++p)if(powered(wells,p)) {
        const auto conf=Display13Transcoder::control(io,p,transcoders);
        if(conf==UINT32_MAX) {r.status=Status::Readback;return r;}
        if(transcoders.dsi(p) && (conf&0xC0000000U) && (conf&0xC0000000U)!=0xC0000000U) {r.status=Status::Topology;return r;}
        if(conf&Enable)r.active|=1U<<p;
    }
    r.status=r.active && !(r.active&~allowed) ? Status::Complete : Status::Topology;return r;
}
inline uint32_t nativePipe(uint32_t raw,unsigned pipe) {
    if(pipe>=3)return 0;
    uint32_t result=0;
    if(raw&1)result|=1U<<pipe;
    if(raw&8)result|=1U<<(3+pipe);
    if(raw&0x80600000)result|=1U<<(6+pipe); // ADL hard/soft and FIFO underrun.
    if(raw&4)result|=1U<<(9+pipe);
    for(unsigned dsb=0;dsb<3;++dsb)if(raw&(1U<<(13+dsb)))result|=1U<<(14+3*dsb+pipe);
    return result;
}
inline uint32_t pchOwned(const State &s) {return ((s.ports&7)<<16)|(((s.ports&~s.typeC)>>3)<<24);}
inline uint32_t northOwned(const State &s) {return (s.typeC>>3)<<16;}
constexpr uint32_t NorthControl=0x44038;
inline uint32_t northControl(const State &s,uint32_t value) {
    uint32_t r=0;for(unsigned tc=0;tc<4;++tc)if(s.typeC&(8U<<tc))r|=value<<(4*tc);return r;
}
// Exact c004fc1a processDEInterrupt: DP-alt events occupy the second output
// word, bits2*TC (long) / 2*TC+1 (short), not PCH legacy hotplug bits6+.
inline uint32_t nativeDPAlt(uint32_t pending,uint32_t control) {
    uint32_t r=0;for(unsigned tc=0;tc<4;++tc)if(pending&(1U<<(16+tc))) {
        if(control&(2U<<(4*tc)))r|=1U<<(2*tc);
        if(control&(1U<<(4*tc)))r|=2U<<(2*tc);
    }return r;
}
// Linux GEN11_DE_HPD: TC1..6 at bits16..21, TBT1..6 at bits0..5.
// Apply the same connector exclusion to both PCH and north HPD banks.
// Reference: samranahmad09/WhateverXe-RaptorLakeP, 2195924, this header.
constexpr uint32_t HPDBank=0x44478;
constexpr uint32_t hpdExcluded(uint32_t excludedPCH) {
    const uint32_t ports=(excludedPCH>>24)&0x3FU;return ports|(ports<<16);
}
inline uint32_t ddiMask(const State &s,uint32_t value) {
    uint32_t m=0;for(unsigned p=0;p<3;++p)if(s.ports&(1U<<p))m|=value<<(4*p);return m;
}
inline uint32_t tcMask(const State &s,uint32_t value) {
    uint32_t m=0;for(unsigned tc=0;tc<4;++tc)if((s.ports&~s.typeC)&(8U<<tc))m|=value<<(4*tc);return m;
}
inline uint32_t nativeHotplug(uint32_t sde,uint32_t ddi,uint32_t tcStatus=0) {
    uint32_t result=0;
    for(unsigned port=0;port<3;++port)if(sde&(1U<<(16+port))) {
        if(ddi&(2U<<(4*port)))result|=1U<<(2*port); // long pulse
        else if(ddi&(1U<<(4*port)))result|=1U<<(2*port+1); // short pulse
    }
    for(unsigned tc=0;tc<4;++tc)if(sde&(1U<<(24+tc))) {
        if(tcStatus&(2U<<(4*tc)))result|=1U<<(6+2*tc);
        if(tcStatus&(1U<<(4*tc)))result|=1U<<(7+2*tc);
    }
    return result;
}
template<class IO> bool available(IO &io) {
    return io.readable(GfxMaster) && io.readable(Driver) && io.readable(0xC400C) &&
        io.readable(DDIControl) && io.readable(0x72D28);
}
template<class IO> Status fail(IO &io,State &s,Status status,uint32_t reg=0,uint32_t bits=0) {
    if(!s.faulted) {s.firstFailure=status;s.failureRegister=reg;s.failureBits=bits;}
    s.faulted=true;s.enabled=false;
    if(io.readable(Master))io.write(Master,0); // Stop a repeated unsupported DE IRQ.
    return status;
}
// Only powered banks are touched. Unrelated enabled IRQ sources constitute a
// different owner; do not clear them or pretend to have installed their handler.
constexpr uint32_t NativeGT=(3U)|(1U<<29); // Exact donor non-display callback dispatch.
// A caller may expand this mask only after validating/publishing each native
// pipe's resources. Keep the live boot backend's default at pipe A. Changing
// the set requires the existing masked/quiesced transition, not an idempotent
// enable that silently admits a newly active bank.
template<class IO> Status enable(IO &io,State &s,uint32_t excludedPCH=0,uint32_t sharedGT=0,uint32_t expectedPipes=1,uint32_t ports=3,uint32_t typeC=0) {
    if(s.faulted)return Status::Faulted;
    const bool dsi=s.transcoders.dsi0Pipe!=UINT32_MAX;
    if(!s.transcoders.valid() || (!ports && !dsi) || (dsi && (ports&1)) || (ports&~0x7FU) || (typeC&~(ports&0x78)) || (s.begun && (ports!=s.ports || typeC!=s.typeC)))return fail(io,s,Status::Topology);
    if(dsi)excludedPCH|=1U<<16; // HPD-A is the panel reset GPIO, not hotplug.
    s.ports=ports;s.typeC=typeC;excludedPCH|=(typeC>>3)<<24;
    if((excludedPCH&pchOwned(s)) || (excludedPCH&~(PCHTypeC|PCHCombo)) || (sharedGT && sharedGT!=NativeGT) || !expectedPipes || (expectedPipes&~7U))return fail(io,s,Status::Topology);
    if(!available(io) || (typeC && !io.readable(NorthControl)) || ((ports&0x78) && !io.readable(TCControl)))return Status::Unavailable;
    // Native repeated enable is idempotent. Do not drop a pending flip/DSB
    // completion or undo hwSetInterruptForTransaction's VBL mask.
    if(s.enabled && excludedPCH!=s.excludedPCH)return fail(io,s,Status::Topology);
    if(s.enabled && s.active && s.active!=expectedPipes)return fail(io,s,Status::Topology);
    if(s.enabled && s.active)return (io.read(Master)&Enable) && (io.read(GfxMaster)&Enable) ?
        Status::Complete : fail(io,s,Status::Readback);
    auto wells=io.read(Driver);
    if(dsi && !powered(wells,s.transcoders.dsi0Pipe))return fail(io,s,Status::Power);
    for(unsigned p=0;p<3;++p)if((expectedPipes&(1U<<p)) && !powered(wells,p))return fail(io,s,Status::Power);
    uint32_t active=0,configured=0;
    for(unsigned p=0;p<3;++p)if(powered(wells,p)) {
        const auto conf=Display13Transcoder::control(io,p,s.transcoders);
        if(conf==UINT32_MAX)return fail(io,s,Status::Readback);
        if(s.transcoders.dsi(p) && (conf&0xC0000000U) && (conf&0xC0000000U)!=0xC0000000U)return fail(io,s,Status::Topology);
        if(conf&Enable)active|=1U<<p;
        const auto ier=io.read(PipeIIR(p)+4),imr=io.read(PipeIIR(p)-4);
        if((ier&~imr)&~PipeOwned)return fail(io,s,Status::ForeignIRQ,PipeIIR(p)+4,(ier&~imr)&~PipeOwned);
        if(!(s.configured&(1U<<p))) {s.priorIER[p]=ier;s.priorIMR[p]=imr;}
        configured|=1U<<p;
    }
    if(active!=expectedPipes)return fail(io,s,Status::Topology);
    // Fixed links use PCH HPD; USB-C DP-alt uses north TC HPD. TBT is not
    // implemented. Preserve status for excluded transports while masking them.
    const uint32_t excludedHPD=hpdExcluded(excludedPCH|((ports&0x78)<<21))&~northOwned(s);
    constexpr uint32_t otherBanks[] {0x44448,0x44468,HPDBank,0x44488};
    for(auto iir : otherBanks) {
        auto pending=io.read(iir+4)&~io.read(iir-4);
        if(iir==HPDBank)pending&=~(excludedHPD|northOwned(s));
        if(pending)return fail(io,s,Status::ForeignIRQ,iir+4,pending);
    }
    const auto pchIER=io.read(0xC400C),pchIMR=io.read(0xC4004);
    if((pchIER&~pchIMR)&~(pchOwned(s)|excludedPCH))
        return fail(io,s,Status::ForeignIRQ,0xC400C,(pchIER&~pchIMR)&~(pchOwned(s)|excludedPCH));
    if(!s.begun) {
        s.priorPCHIER=pchIER;s.priorPCHIMR=pchIMR;
        s.priorDDIEnable=io.read(DDIControl)&ddiMask(s,8);
        if(ports&0x78)s.priorTCEnable=io.read(TCControl)&tcMask(s,8);
    }
    const auto foreignMaster=io.read(GfxMaster)&((3U)|(3U<<29))&~sharedGT;
    if(foreignMaster)return fail(io,s,Status::ForeignIRQ,GfxMaster,foreignMaster);
    io.write(Master,0);
    if(excludedPCH) {
        // Exclusive native FB takeover: a connector excluded by the native
        // policy must not retain its GOP-era hotplug enable. Preserve every
        // unrelated mask/enable and do not acknowledge excluded IIR status.
        io.write(0xC4004,pchIMR|excludedPCH);
        io.write(0xC400C,pchIER&~excludedPCH);
        if((io.read(0xC400C)&excludedPCH) || (~io.read(0xC4004)&excludedPCH))
            return fail(io,s,Status::Readback);
    }
    if(excludedHPD) {
        // Keep pending north status and every unrelated source intact.
        io.write(HPDBank-4,io.read(HPDBank-4)|excludedHPD);
        io.write(HPDBank+4,io.read(HPDBank+4)&~excludedHPD);
        if((io.read(HPDBank+4)&excludedHPD) || (~io.read(HPDBank-4)&excludedHPD))
            return fail(io,s,Status::Readback);
    }
    if(typeC) {
        const auto ctl=io.read(NorthControl);
        if(ctl==UINT32_MAX)return fail(io,s,Status::Readback);
        io.write(NorthControl,(ctl&~0x333333U)|northControl(s,8)|(ctl&northControl(s,3)));
        io.write(HPDBank-4,io.read(HPDBank-4)&~northOwned(s));
        io.write(HPDBank+4,io.read(HPDBank+4)|northOwned(s));
        const auto stale=io.read(HPDBank)&northOwned(s);if(stale)io.write(HPDBank,stale);
        if((io.read(NorthControl)&northControl(s,8))!=northControl(s,8) ||
           (io.read(HPDBank+4)&northOwned(s))!=northOwned(s) || (io.read(HPDBank-4)&northOwned(s)))
            return fail(io,s,Status::Readback);
    }
    s.excludedPCH=excludedPCH;
    for(unsigned p=0;p<3;++p)if(configured&(1U<<p)) {
        const auto enabled=(active&(1U<<p)) ? (PipeOwned&~4U) : 0U;
        io.write(PipeIIR(p)-4,(io.read(PipeIIR(p)-4)|PipeOwned)&~enabled);
        io.write(PipeIIR(p)+4,(io.read(PipeIIR(p)+4)&~PipeOwned)|enabled);
        // Clear observed stale owned status twice, as in the native reset.
        for(unsigned repeat=0;repeat<2;++repeat) {
            const auto stale=io.read(PipeIIR(p))&PipeOwned;if(stale)io.write(PipeIIR(p),stale);
        }
        if(active&(1U<<p))for(unsigned dsb=0;dsb<3;++dsb) {
            const auto reg=DSBInterrupt(p,dsb),value=io.read(reg);
            if(value==UINT32_MAX)return fail(io,s,Status::Readback);
            if(!s.begun)s.priorDSB[p][dsb]=value&0xF0000;
            // Enable Gen12/13 program/error statuses, never MTL ATS bit20.
            io.write(reg,(value&~0xF)|0xF0000);
        }
    }
    const auto ddi=io.read(DDIControl);
    if(ddi==UINT32_MAX)return fail(io,s,Status::Readback);
    // DSI's HPD-A is an output GPIO: disable its HPD detector, preserve
    // OUTPUT_DATA bit2 and leave its W1C status untouched under this IRQ lock.
    io.write(DDIControl,(ddi&~(0x3333U|(dsi?8U:0U)))|ddiMask(s,8)|(ddi&ddiMask(s,3)));
    if(dsi && (io.read(DDIControl)&12)!=(ddi&4))return fail(io,s,Status::Readback);
    if(ports&0x78) {
        const auto tc=io.read(TCControl);s.lastTC=tc;if(tc==UINT32_MAX)return fail(io,s,Status::Readback);
        io.write(TCControl,(tc&~0x333333U)|tcMask(s,8)|(tc&tcMask(s,3)));
        if((io.read(TCControl)&tcMask(s,8))!=tcMask(s,8))return fail(io,s,Status::Readback);
    }
    io.write(0xC4004,io.read(0xC4004)&~pchOwned(s));
    io.write(0xC400C,io.read(0xC400C)|pchOwned(s));
    const auto stale=io.read(0xC4008)&pchOwned(s);if(stale)io.write(0xC4008,stale);
    // Verify bank masks before admitting the actual PCI interrupt source.
    for(unsigned p=0;p<3;++p)if(configured&(1U<<p)) {
        const auto expected=(active&(1U<<p)) ? (PipeOwned&~4U) : 0U;
        if((io.read(PipeIIR(p)+4)&PipeOwned)!=expected ||
           (io.read(PipeIIR(p)-4)&PipeOwned)!=(PipeOwned&~expected))return fail(io,s,Status::Readback);
        if(active&(1U<<p))for(unsigned d=0;d<3;++d)
            if((io.read(DSBInterrupt(p,d))&0xF0000)!=0xF0000)return fail(io,s,Status::Readback);
    }
    if((io.read(DDIControl)&ddiMask(s,8))!=ddiMask(s,8) ||
       (io.read(0xC400C)&pchOwned(s))!=pchOwned(s) || (io.read(0xC4004)&pchOwned(s)))return fail(io,s,Status::Readback);
    s.begun=true;s.configured|=configured;s.active=active;
    io.write(GfxMaster,Enable);io.write(Master,Enable);
    if(!(io.read(Master)&Enable) || !(io.read(GfxMaster)&Enable))return fail(io,s,Status::Readback);
    s.enabled=true;return Status::Complete;
}
// Caller has proved total display power loss and restored its power/clock
// dependencies. Cached armed banks no longer describe hardware. Reinstall
// both pipe interrupts and connector detection, not just the live DSI bank.
template<class IO> Status restoreAfterPowerLoss(IO &io,State &s,uint32_t allowed) {
    if(!s.begun || s.faulted)return Status::Unavailable;
    const auto actual=readActivePipes(io,allowed,s.transcoders);
    if(actual.status!=Status::Complete)return actual.status;
    io.write(Master,0);
    if(io.read(Master)&Enable)return fail(io,s,Status::Readback);
    s.enabled=false;s.active=0;
    return enable(io,s,s.excludedPCH,NativeGT,actual.active,s.ports,s.typeC);
}
template<class IO> Status disable(IO &io,State &s) {
    if(!s.begun || !io.readable(Master))return Status::Unavailable;
    io.write(Master,0);s.enabled=false;
    return (io.read(Master)&Enable) ? fail(io,s,Status::Readback) : Status::Complete;
}
// Caller holds the native IRQ lock. Add only newly active qualified banks;
// peer masks/status, PCH, GT and both masters are untouched. No IRQ reset or
// stale-status ACK is part of this native activation boundary.
template<class IO> Status extendActivePipes(IO &io,State &s,uint32_t allowed) {
    if(!s.begun || !s.enabled || s.faulted)return Status::Unavailable;
    const auto actual=readActivePipes(io,allowed,s.transcoders);
    if(actual.status!=Status::Complete)return actual.status;
    // s.active tracks armed IRQ banks, not currently running transcoders.
    // Native display-off may stop a peer while retaining its powered bank.
    // Preserve that bank and pending completions until explicit retirement;
    // it must not prevent another pipe from being rearmed during wake.
    const auto master=io.read(Master),gfx=io.read(GfxMaster);
    if(master==UINT32_MAX || gfx==UINT32_MAX || !(master&Enable) || !(gfx&Enable))return Status::Readback;
    const auto added=actual.active&~s.active;
    uint32_t ier[3] {},imr[3] {},dsb[3][3] {};
    for(unsigned p=0;p<3;++p)if(added&(1U<<p)) {
        ier[p]=io.read(PipeIIR(p)+4);imr[p]=io.read(PipeIIR(p)-4);
        if(ier[p]==UINT32_MAX || ((ier[p]&~imr[p])&~PipeOwned))return Status::ForeignIRQ;
        for(unsigned d=0;d<3;++d) {
            dsb[p][d]=io.read(DSBInterrupt(p,d));if(dsb[p][d]==UINT32_MAX)return Status::Readback;
        }
    }
    for(unsigned p=0;p<3;++p)if(added&(1U<<p)) {
        constexpr uint32_t enabled=PipeOwned&~4U;
        if(!(s.configured&(1U<<p))) {s.priorIER[p]=ier[p];s.priorIMR[p]=imr[p];}
        io.write(PipeIIR(p)-4,(imr[p]|PipeOwned)&~enabled);
        io.write(PipeIIR(p)+4,(ier[p]&~PipeOwned)|enabled);
        for(unsigned d=0;d<3;++d) {
            if(!(s.configured&(1U<<p)))s.priorDSB[p][d]=dsb[p][d]&0xF0000;
            io.write(DSBInterrupt(p,d),(dsb[p][d]&~15U)|0xF0000); // lower pending bits are W1C: write zero.
        }
        if((io.read(PipeIIR(p)+4)&PipeOwned)!=enabled ||
           (io.read(PipeIIR(p)-4)&PipeOwned)!=(PipeOwned&~enabled))return fail(io,s,Status::Readback);
        for(unsigned d=0;d<3;++d)if((io.read(DSBInterrupt(p,d))&0xF0000)!=0xF0000)return fail(io,s,Status::Readback);
        s.configured|=1U<<p;s.active|=1U<<p;
    }
    return Status::Complete;
}
// A last *owned* display request can disappear only after the pipe and
// autonomous DSB execution stopped. Domain bits are the power core's fixed
// PW1/PW2/A/B/C/DDI_B ABI. The caller blocks affected bank access during release.
inline uint32_t affectedPipes(uint32_t domains,uint32_t ddiA=7,uint32_t ddiB=1,uint32_t ddiC=7,
                              uint32_t tc1=7,uint32_t tc2=7,uint32_t tc3=7,uint32_t tc4=7) {
    const uint32_t map[] {ddiA,ddiB,ddiC,tc1,tc2,tc3,tc4};
    for(auto mask:map)if(mask&~7U)return 7;
    uint32_t pipes=(domains>>2)&7;
    if(domains&1)pipes|=7;
    if(domains&2)pipes|=6;
    for(unsigned port=0;port<7;++port) {
        const auto scope=Display13Power::ddiDomain(port)|(port>=3?Display13Power::auxDomain(port):0);
        if(domains&scope)pipes|=map[port];
    }
    return pipes;
}
inline uint32_t portPowerDomains() {
    uint32_t domains=0;
    for(unsigned p=0;p<7;++p)domains|=Display13Power::ddiDomain(p)|(p>=3?Display13Power::auxDomain(p):0);
    return domains;
}
// PLL bypass transitions retain pending interrupt status. Unlike physical
// well release, they only need stopped consumers while the master is masked.
template<class IO> bool consumersStopped(IO &io,uint32_t pipes,const Display13Transcoder::Bindings &transcoders={}) {
    if(!transcoders.valid())return false;
    const auto wells=io.read(Driver);
    if(wells==UINT32_MAX || (wells&3)!=3 || (wells&(3U<<16)))return false;
    if(transcoders.dsi0Pipe<3 && (pipes&(1U<<transcoders.dsi0Pipe)) && !powered(wells,transcoders.dsi0Pipe))return false;
    for(unsigned p=0;p<3;++p)if((pipes&(1U<<p)) && powered(wells,p)) {
        const auto conf=Display13Transcoder::control(io,p,transcoders),cursor=io.read(0x70080+0x1000*p);
        const auto trans=io.read(transcoders.function(p));
        if(conf==UINT32_MAX || cursor==UINT32_MAX || trans==UINT32_MAX ||
           (conf&0xC0000000) || (cursor&63) || (trans&Enable))return false;
        if(transcoders.dsi(p)) {
            const auto normal=io.read(0x60400+0x1000*p);
            if(normal==UINT32_MAX || (normal&Enable))return false;
        }
        for(unsigned n=0;n<5;++n) {
            const auto plane=io.read(0x70180+0x1000*p+0x100*n);
            if(plane==UINT32_MAX || (plane&Enable))return false;
        }
        for(unsigned d=0;d<3;++d) {
            const auto ctrl=io.read(DSBControl(p,d));
            if(ctrl==UINT32_MAX || (ctrl&(Enable|1)))return false;
        }
    }
    return true;
}
// Exact native E08 event bits emitted by nativePipe(). This is independent
// of DDI HPD events in E10, which remain pending for the connected port.
constexpr uint32_t pipeEvents(unsigned pipe) {
    return pipe<3 ? ((1U<<pipe)|(1U<<(3+pipe))|(1U<<(6+pipe))|(1U<<(9+pipe))|
        (1U<<(14+pipe))|(1U<<(17+pipe))|(1U<<(20+pipe))) : 0;
}
// Caller holds the IRQ lock after masking DE master and draining real HW/SW
// handlers. queued is the actual native E08 word, never a fabricated zero.
// Hardware STOP_ACK/idle DSB qualify retirement; this function stops only the
// target bank's notifications. PCH HDMI, GT and every other pipe are preserved.
template<class IO> Status parkStoppedPipe(IO &io,State &s,unsigned pipe,unsigned handlers,unsigned softwareHandlers,uint32_t queued) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || pipe>=3 || !(s.configured&(1U<<pipe)))return Status::Unavailable;
    const auto master=io.read(Master);
    if(master==UINT32_MAX)return Status::Readback;
    if((master&Enable) || handlers || softwareHandlers || (queued&pipeEvents(pipe)))return Status::Unavailable;
    if(!powered(io.read(Driver),pipe))return Status::Power;
    if(!consumersStopped(io,1U<<pipe,s.transcoders))return Status::Topology;
    const auto iir=PipeIIR(pipe),ier=io.read(iir+4),imr=io.read(iir-4);
    // IMR=all ones is the valid fully masked state, including a re-park.
    if(ier==UINT32_MAX)return Status::Readback;
    if((ier&~imr)&~PipeOwned)return Status::ForeignIRQ;
    uint32_t dsb[3];
    for(unsigned d=0;d<3;++d) {
        dsb[d]=io.read(DSBInterrupt(pipe,d));
        if(dsb[d]==UINT32_MAX)return Status::Readback;
    }
    io.write(iir-4,imr|PipeOwned);io.write(iir+4,ier&~PipeOwned);
    for(unsigned d=0;d<3;++d)io.write(DSBInterrupt(pipe,d),dsb[d]&~0xF000FU);
    const auto stale=io.read(iir)&PipeOwned;if(stale)io.write(iir,stale);
    if((io.read(iir+4)&PipeOwned) || (~io.read(iir-4)&PipeOwned))return fail(io,s,Status::Readback);
    for(unsigned d=0;d<3;++d)if(io.read(DSBInterrupt(pipe,d))&0xF0000)return fail(io,s,Status::Readback);
    if(!consumersStopped(io,1U<<pipe,s.transcoders))return fail(io,s,Status::Topology);
    s.active&=~(1U<<pipe);return Status::Complete;
}
// Caller has masked the master, drained native handlers and stopped all
// consumers. PW1 remains powered for the PCH HDMI hotplug bank. Keep pending
// HPD status: it may already contain the reconnect edge.
template<class IO> Status hotplugOnly(IO &io,State &s,bool preservePipeStatus=false) {
    if(!s.begun || s.faulted)return Status::Unavailable;
    const auto master=io.read(Master);if(master==UINT32_MAX)return Status::Readback;
    const bool already=master&Enable;
    if(already && (!preservePipeStatus || !s.enabled || s.active))return Status::Unavailable;
    const auto wells=io.read(Driver);
    if(wells==UINT32_MAX || (wells&3)!=3)return Status::Power;
    if(!consumersStopped(io,7,s.transcoders))return Status::Topology;
    if((io.read(DDIControl)&ddiMask(s,8))!=ddiMask(s,8) ||
       (io.read(0xC400C)&pchOwned(s))!=pchOwned(s) || (io.read(0xC4004)&pchOwned(s)))return Status::Readback;
    uint32_t ier[3] {},imr[3] {};
    // Inspect every powered bank before any mutation; an unknown peer must
    // not leave earlier banks masked or acknowledged as a side effect.
    for(unsigned p=0;p<3;++p)if(powered(wells,p)) {
        ier[p]=io.read(PipeIIR(p)+4);imr[p]=io.read(PipeIIR(p)-4);
        if(ier[p]==UINT32_MAX)return Status::Readback;
        if((ier[p]&~imr[p])&~PipeOwned)return Status::ForeignIRQ;
        if(already && ((ier[p]&PipeOwned) || (~imr[p]&PipeOwned)))return Status::Unavailable;
    }
    if(already)return Status::Complete; // Verified HPD-only; no pending ACK or writes.
    for(unsigned p=0;p<3;++p)if(powered(wells,p)) {
        io.write(PipeIIR(p)-4,imr[p]|PipeOwned);io.write(PipeIIR(p)+4,ier[p]&~PipeOwned);
        if(!preservePipeStatus) {const auto stale=io.read(PipeIIR(p))&PipeOwned;if(stale)io.write(PipeIIR(p),stale);}
        if((io.read(PipeIIR(p)+4)&PipeOwned) || (~io.read(PipeIIR(p)-4)&PipeOwned))return Status::Readback;
    }
    s.active=0;io.write(Master,Enable);
    if(!(io.read(Master)&Enable))return fail(io,s,Status::Readback);
    s.enabled=true;return Status::Complete;
}
// The runtime supplies one mask to WM checks, this hardware proof and its
// access block. Never silently derive a different DDI mapping inside the proof.
template<class IO> bool quiescentPipes(IO &io,const State &s,uint32_t domains,unsigned handlers,uint32_t pipes) {
    const auto required=affectedPipes(domains,0,0,0,0,0,0,0);
    if((pipes&~7U) || (pipes&required)!=required || ((domains&portPowerDomains()) && !pipes))return false;
    if(!domains)return true;
    const auto master=io.read(Master);
    if(master==UINT32_MAX)return false;
    // An inactive B/C well can be released while A keeps displaying. The
    // donor's temporary PG3 scope must not force the active HDMI pipe off.
    const bool active=(domains&1) || (domains&portPowerDomains()) || (pipes&s.active);
    if(active && (handlers || s.enabled || (master&Enable)))return false;
    if(master&(pipes<<16))return false;
    const auto wells=io.read(Driver);if(wells==UINT32_MAX)return false;
    for(unsigned port=0;port<7;++port)if(domains&(Display13Power::ddiDomain(port)|(port>=3?Display13Power::auxDomain(port):0))) {
        const auto ddi=io.read(0x64000+port*0x100);
        if(ddi==UINT32_MAX || (ddi&Enable))return false;
    }
    if(!consumersStopped(io,pipes,s.transcoders))return false;
    for(unsigned p=0;p<3;++p)if((pipes&(1U<<p)) && powered(wells,p)) {
        // No bank notification is unmasked for a domain about to disappear.
        if(!active && (io.read(PipeIIR(p)+4)&~io.read(PipeIIR(p)-4)))return false;
    }
    return true;
}
template<class IO> bool quiescent(IO &io,const State &s,uint32_t domains,unsigned handlers) {
    return quiescentPipes(io,s,domains,handlers,affectedPipes(domains));
}
// Caller owns the native workloop gate and IRQ lock. It supplies the real
// DE-permitted handler count, not total GT-only handlers, and real native
// queues. Entry scopes keep new GT-only handlers from gaining DE permission
// after this scope restores the master. No GT register/source is changed.
struct PowerReleaseScope {
    bool held {},enabled {};
    uint32_t master {},affected {},parked {};
};
template<class IO> Status beginPowerRelease(IO &io,State &s,PowerReleaseScope &g,uint32_t domains,uint32_t pipes,
                                           unsigned displayHandlers,unsigned softwareHandlers,
                                           uint32_t queued,uint32_t auxiliary,uint32_t hotplug) {
    if(g.held || !s.begun || s.faulted || !domains || !pipes || (pipes&~7U) ||
       (pipes&affectedPipes(domains,0,0,0,0,0,0,0))!=affectedPipes(domains,0,0,0,0,0,0,0))return Status::Unavailable;
    uint32_t events=0;for(unsigned p=0;p<3;++p)if(pipes&(1U<<p))events|=pipeEvents(p);
    uint32_t hpd=(domains&1)?0x3FFFFU:0;
    for(unsigned port=0;port<7;++port)if(domains&(Display13Power::ddiDomain(port)|(port>=3?Display13Power::auxDomain(port):0)))hpd|=3U<<(2*port);
    if(displayHandlers || softwareHandlers || (queued&events) || auxiliary || (hotplug&hpd))return Status::Unavailable;
    const auto master=io.read(Master),wells=io.read(Driver);
    if(master==UINT32_MAX || wells==UINT32_MAX || bool(master&Enable)!=s.enabled)return Status::Readback;
    if(!consumersStopped(io,pipes,s.transcoders))return Status::Topology;
    // Preflight every bank before parking any. A never-configured bank must
    // already have all notifications masked; it is not ours to clear.
    for(unsigned p=0;p<3;++p)if(pipes&(1U<<p)) {
        if(!powered(wells,p)) {if(s.active&(1U<<p))return Status::Power;continue;}
        const auto ier=io.read(PipeIIR(p)+4),imr=io.read(PipeIIR(p)-4);
        if(ier==UINT32_MAX)return Status::Readback; // Fully masked IMR is valid.
        if((ier&~imr)&~PipeOwned)return Status::ForeignIRQ;
        if(!(s.configured&(1U<<p)) && (ier&~imr))return Status::ForeignIRQ;
        for(unsigned d=0;d<3;++d) {
            const auto dsb=io.read(DSBInterrupt(p,d));
            if(dsb==UINT32_MAX)return Status::Readback;
            if(dsb&15U)return Status::Unavailable; // Native completion/error has not been consumed.
            if(!(s.configured&(1U<<p)) && (dsb&0xF0000))return Status::ForeignIRQ;
        }
    }
    g.held=true;g.enabled=s.enabled;g.master=master;g.affected=pipes;
    io.write(Master,0);s.enabled=false;
    if(io.read(Master)&Enable)return fail(io,s,Status::Readback);
    for(unsigned p=0;p<3;++p)if((pipes&s.configured&(1U<<p)) && powered(wells,p)) {
        const auto status=parkStoppedPipe(io,s,p,displayHandlers,softwareHandlers,queued);
        if(status!=Status::Complete)return status;
        g.parked|=1U<<p;
    }
    return quiescentPipes(io,s,domains,displayHandlers,pipes) ? Status::Complete : Status::Unavailable;
}
template<class IO> Status endPowerRelease(IO &io,State &s,PowerReleaseScope &g) {
    if(!g.held)return Status::Unavailable;
    g.held=false;
    if(s.faulted)return Status::Faulted; // Do not re-enable after a failed write.
    const auto wells=io.read(Driver);
    if(wells==UINT32_MAX)return fail(io,s,Status::Power);
    if((wells&3)!=3) {s.enabled=false;return Status::Complete;} // PW1 off: no DE MMIO until real power-up.
    io.write(Master,g.master&Enable);
    const auto master=io.read(Master);
    if(master==UINT32_MAX || bool(master&Enable)!=bool(g.master&Enable))return fail(io,s,Status::Readback);
    s.enabled=g.enabled;return Status::Complete;
}
template<class IO> Status vblank(IO &io,State &s,unsigned pipe,bool enabled) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || pipe>=3 || !(s.active&(1U<<pipe)))return Status::Topology;
    if(!powered(io.read(Driver),pipe))return fail(io,s,Status::Power);
    const auto iir=PipeIIR(pipe),ier=io.read(iir+4),imr=io.read(iir-4);
    io.write(iir-4,enabled ? imr&~1U : imr|1U);
    io.write(iir+4,enabled ? ier|1U : ier&~1U);
    const auto pending=io.read(iir)&1;if(pending)io.write(iir,pending);
    if(bool(io.read(iir+4)&1)!=enabled || bool(io.read(iir-4)&1)==enabled)return fail(io,s,Status::Readback);
    return Status::Complete;
}
// Invoked under native IRQ lock by ProcessInterrupt. No sleeping, allocation,
// registry serialization, PCI config access, or unpowered pipe reads here.
template<class IO> Events collect(IO &io,State &s) {
    Events e;
    if(!s.begun || !s.enabled || s.faulted)return e;
    const auto master=io.read(Master),wells=io.read(Driver);s.lastMaster=master;
    if(master==UINT32_MAX) {e.status=fail(io,s,Status::Readback);return e;}
    io.write(Master,0);++s.interrupts;
    for(unsigned p=0;p<3;++p)if(master&(1U<<(16+p))) {
        if(!powered(wells,p)) {e.status=fail(io,s,Status::Power);return e;}
        if(!(s.active&(1U<<p))) {e.status=fail(io,s,Status::Topology);return e;}
        const auto status=io.read(PipeIIR(p));s.raw[p]=status;
        const auto pending=status&io.read(PipeIIR(p)+4)&~io.read(PipeIIR(p)-4);
        if(status==UINT32_MAX || (pending&~PipeOwned)) {s.foreign|=pending;e.status=fail(io,s,Status::ForeignIRQ);return e;}
        const auto raw=pending&PipeOwned,owned=status&PipeOwned;
        if(owned)io.write(PipeIIR(p),owned);
        e.pipe|=nativePipe(raw,p);
        if(raw&1)++s.vblanks[p];if(raw&8)++s.flips[p];if(raw&0x80600000)++s.underruns[p];
        for(unsigned dsb=0;dsb<3;++dsb)if(raw&(1U<<(13+dsb))) {
            const auto reg=DSBInterrupt(p,dsb),value=io.read(reg),errors=value&0xE;
            if(value==UINT32_MAX) {e.status=fail(io,s,Status::Readback);return e;}
            if(errors) {++s.dsbErrors[p];e.pipe|=1U<<(6+p);io.write(reg,(value&~0xF)|errors);}
            // Program status bit0 stays for the native completion handler,
            // which clears the real path's DSB busy flag and then W1C status.
        }
    }
    if(master&(1U<<23)) {
        const auto sde=io.read(0xC4008);s.lastPCH=sde;
        const auto pending=sde&io.read(0xC400C)&~io.read(0xC4004);
        if(sde==UINT32_MAX || (pending&~pchOwned(s))) {s.foreign|=pending;e.status=fail(io,s,Status::ForeignIRQ);return e;}
        const auto ddi=io.read(DDIControl);s.lastDDI=ddi;
        if(ddi==UINT32_MAX) {e.status=fail(io,s,Status::Readback);return e;}
        uint32_t tc=0;
        if(s.ports&0x78) {
            tc=io.read(TCControl);s.lastTC=tc;if(tc==UINT32_MAX){e.status=fail(io,s,Status::Readback);return e;}
            uint32_t status=0;for(unsigned p=0;p<4;++p)if(pending&(1U<<(24+p)))status|=tc&(3U<<(4*p));
            if(status)io.write(TCControl,(tc&~0x333333U)|status);
        }
        e.hotplug=nativeHotplug(pending,ddi,tc);
        uint32_t status=0;for(unsigned p=0;p<3;++p)if(pending&(1U<<(16+p)))status|=ddi&(3U<<(4*p));
        if(status)io.write(DDIControl,(ddi&~0x3333U)|status);
        const auto owned=sde&pchOwned(s);if(owned)io.write(0xC4008,owned);
    }
    if(master&(1U<<21)) {
        const auto status=io.read(HPDBank);
        const auto pending=status&io.read(HPDBank+4)&~io.read(HPDBank-4);
        if(status==UINT32_MAX || (pending&~northOwned(s))) {e.status=fail(io,s,Status::ForeignIRQ,HPDBank,pending);return e;}
        const auto ctl=io.read(NorthControl);
        if(ctl==UINT32_MAX){e.status=fail(io,s,Status::Readback);return e;}
        e.aux=nativeDPAlt(pending,ctl);
        uint32_t ack=0;for(unsigned tc=0;tc<4;++tc)if(pending&(1U<<(16+tc)))ack|=ctl&(3U<<(4*tc));
        if(ack)io.write(NorthControl,(ctl&~0x333333U)|ack);
        const auto owned=status&northOwned(s);if(owned)io.write(HPDBank,owned);
    }
    const uint32_t supported=(7U<<16)|(1U<<23)|(s.typeC?(1U<<21):0U)|Enable;
    if(master&~supported) {s.foreign|=master&~supported;e.status=fail(io,s,Status::ForeignIRQ);return e;}
    io.write(Master,Enable);e.status=Status::Complete;return e;
}
}
#endif
