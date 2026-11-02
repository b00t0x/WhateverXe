// Fixed TMDS on ADLP Dekel PHY. Equations/register fields checked against
// Intel display definitions and Linux v6.12; independent integer implementation.
#ifndef KERN_DISPLAY13_TC_CLOCK_HPP
#define KERN_DISPLAY13_TC_CLOCK_HPP
#include "kern_display13_tc.hpp"
namespace Display13TC {
struct TMDSClock {
    struct Word {uint32_t reg,mask,value;};
    uint32_t rate {},reference {},dco {},hs {},ds {};
    Word words[8] {};
    bool valid() const {return rate && reference && dco && hs && ds;}
    static TMDSClock calculate(uint32_t rate,uint32_t reference) {
        TMDSClock p;
        if(rate<25000 || rate>600000 || (reference!=19200 && reference!=24000 && reference!=38400))return p;
        const uint32_t high[]={7,5,3,2};
        for(auto h:high) {
            for(unsigned d=10;d;--d) {
                const auto oscillator=uint64_t(rate)*5*h*d;
                if(oscillator<7992000 || oscillator>10000000)continue;
                const auto divider=reference*2,integer=uint32_t(oscillator/divider),remainder=uint32_t(oscillator%divider);
                if(!integer || integer>255)return {};
                const auto fraction=uint32_t((uint64_t(remainder)<<22)/divider);
                const uint32_t integral=oscillator>=9000000?10:8,proportional=integral/2;
                const auto target=uint32_t((2000000000ULL/(132*reference)+5)/10);
                const auto gain=remainder?uint32_t(200000000ULL/(oscillator*3/10)):0;
                if(target>255 || gain>255)return {};
                const uint32_t ratio=h==7?3:h==5?2:h==3?1:0;
                p.rate=rate;p.reference=reference;p.dco=uint32_t(oscillator);p.hs=h;p.ds=d;
                p.words[0]={0x212c,0x700,0x100};
                p.words[1]={0x20d8,0xff00,0x500};
                p.words[2]={0x20d4,0x1ff00,0x10000|(d>=2?0x4000U:0U)|(ratio<<12)|(d<<8)};
                // Preserve firmware AFC startup unless VBT supplies an explicit policy.
                p.words[3]={0x2200,0x1fffff,(integral<<16)|(proportional<<12)|0x200|integer};
                p.words[4]={0x2204,0x1f00ff,((reference==24000?25U:28U)<<16)|target};
                p.words[5]={0x2210,0xe0ff3a00,(reference==38400?0x40000000U:0x20000000U)|0x2000};
                p.words[6]={0x2214,0x7fffff00,(fraction?0x40000000U:0U)|(fraction<<8)};
                p.words[7]={0x2218,0xffff,gain};
                return p;
            }
        }
        return {};
    }
    // Exact donor ClockParams is DCO integer/fraction15, P/Q/K. Its
    // calculatePllDivisors uses 19.2 MHz for both DSSM codes1 and2, and
    // 24 MHz for code0. Recover TMDS rate before encoding the real Dekel PLL.
    static uint32_t nativeRate(const uint32_t *params,uint32_t refCode) {
        if(!params || refCode>2 || !params[0] || params[0]>1023 || params[1]>32767 ||
           !params[3] || params[3]>255 || !params[4] || params[4]>3)return 0;
        const auto p=params[2];if(p!=2 && p!=3 && p!=5 && p!=7)return 0;
        const uint64_t denominator=uint64_t(5)*p*params[3]*params[4]*32768;
        const auto rate=(uint64_t(refCode?19200:24000)*(uint64_t(params[0])*32768+params[1])+denominator/2)/denominator;
        return rate>=25000 && rate<=600000 ? uint32_t(rate) : 0;
    }
    static uint32_t frequency(uint32_t reference,uint32_t div0,uint32_t bias,uint32_t hsctl) {
        if((reference!=19200 && reference!=24000 && reference!=38400) ||
           div0==UINT32_MAX || bias==UINT32_MAX || hsctl==UINT32_MAX)return 0;
        const uint32_t high[]={2,3,5,7};const auto h=high[(hsctl>>12)&3],d=(hsctl>>8)&15;
        const auto pre=(div0>>8)&15,integer=div0&255,fraction=(bias&0x40000000)?((bias>>8)&0x3fffff):0;
        if(!d || !pre || !integer)return 0;
        return uint32_t((uint64_t(reference)*pre*((uint64_t(integer)<<22)+fraction)+(uint64_t(5*h*d)<<21))/(uint64_t(5*h*d)<<22));
    }
};
// One masked bank transaction. The caller holds the native port/lifecycle
// mutex and all leases; HIP has its own IRQ-safe lock shared across ports.
struct PHY {
    template<class IO> static bool access(IO &io,const Port &p,uint32_t internal,uint32_t mask,uint32_t &value,bool write) {
        Port checked;if(!Port::resolve(p.ddi,checked) || p.phy!=checked.phy || p.hip!=checked.hip || p.shift!=checked.shift ||
            internal>0x2fff || (internal&3) || !io.powerHeld() || !io.readable(p.hip) || !io.readable(p.phyRegister(internal)))return false;
        const auto irq=io.lock();const auto prior=io.read(p.hip);bool changed=false,ok=prior!=UINT32_MAX;
        if(ok) {
            const auto bank=p.bank(prior,internal);changed=bank!=prior;
            if(changed)io.write(p.hip,bank);
            ok=io.read(p.hip)==bank;
            if(ok) {
                const auto reg=p.phyRegister(internal),old=io.read(reg);ok=old!=UINT32_MAX;
                if(ok && write) {const auto desired=(old&~mask)|(value&mask);io.write(reg,desired);ok=io.read(reg)==desired;value=desired;}
                else if(ok)value=old;
            }
            if(changed) {io.write(p.hip,prior);if(io.read(p.hip)!=prior){ok=false;io.bankFault();}}
        }
        io.unlock(irq);return ok;
    }
};
struct TMDSSignal {
    // Public Intel HDMI preset coefficients: vswing/preshoot/de-emphasis.
    // ADLP lane0 uses LOADGENSELECT 0/2, lane1 3/3. This is independent of
    // the electrical preset and must not inherit DP's 0/0 settings.
    template<class IO> static bool program(IO &io,const Port &p,uint32_t preset) {
        if(!io.powerHeld() || !io.owned(p.ddi) || !io.quiescent(p.ddi))return false;
        if(preset==UINT32_MAX)preset=9;
        if(preset>=10)return false;
        const uint32_t swing[]={7,6,4,2,0,0,0,0,0,0};
        const uint32_t emphasis[]={0,0,0,0,0,5,6,7,8,10};
        for(unsigned lane=0;lane<2;++lane) {
            const auto bank=0x1000*lane;
            // Fixed legacy HDMI occupies both lane pairs (X2 in each bank).
            uint32_t mode=0x80;
            if(!PHY::access(io,p,bank+0xa0,0xc0,mode,true))return false;
            uint32_t wake=0;
            if(!PHY::access(io,p,bank+0xd00,UINT32_MAX,wake,true))return false;
            for(unsigned tx=0;tx<2;++tx) {
                uint32_t value=swing[preset]|(emphasis[preset]<<8);
                if(!PHY::access(io,p,bank+0x2c0+4*tx,0x3ff07,value,true))return false;
            }
            uint32_t load=lane?0x78:0x40;
            if(!PHY::access(io,p,bank+0x2c8,0x7c,load,true))return false;
        }
        return true;
    }
};

struct Clock {
    static constexpr uint32_t Gate=0x164280,Enable=1U<<31,Locked=1U<<30,Power=1U<<27,Powered=1U<<26;
    static uint32_t gateBit(unsigned tc) {return tc<3?1U<<(12+tc):tc==3?1U<<21:0;}
    // Runtime IO validates the immutable owner/config/path bindings. A ready
    // register alone never authorizes touching an active/foreign clock.
    template<class IO> static bool accessible(IO &io,const Port &p) {
        Port checked;
        if(!Port::resolve(p.ddi,checked) || p.pll!=checked.pll || p.clock!=checked.clock || p.ready!=checked.ready ||
           p.buffer!=checked.buffer || p.tc!=checked.tc || p.phy!=checked.phy || p.hip!=checked.hip || p.shift!=checked.shift ||
           !io.powerHeld() || !io.quiescent(p.ddi))return false;
        const uint32_t registers[]={p.pll,p.clock,p.buffer,Gate};
        for(auto reg:registers)if(!io.readable(reg) || io.read(reg)==UINT32_MAX)return false;
        return true;
    }
    template<class IO> static bool preflight(IO &io,const Port &p) {
        return accessible(io,p) && io.owned(p.ddi) && io.readable(p.ready) && io.read(p.ready)!=UINT32_MAX &&
            (io.read(p.ready)&6)==4 && (io.read(p.buffer)&0x80000040)==64;
    }
    // HPD, pin assignment and TCSS mode may disappear before native teardown.
    // Only stop our retained, powered hardware after buffer/scanout quiescence.
    // Never use this certificate to start a PLL or program the PHY.
    template<class IO> static bool stopPreflight(IO &io,const Port &p) {
        return accessible(io,p) && io.retainedOwned(p.ddi) && (io.read(p.buffer)&0x800000c0U)==0xc0;
    }
    template<class IO> static bool rmw(IO &io,uint32_t reg,uint32_t mask,uint32_t value) {
        const auto irq=io.lock();const auto old=io.read(reg);
        bool ok=old!=UINT32_MAX;
        if(ok) {const auto desired=(old&~mask)|(value&mask);io.write(reg,desired);ok=(io.read(reg)&mask)==(desired&mask);}
        io.unlock(irq);return ok;
    }
    template<class IO> static bool wait(IO &io,uint32_t reg,uint32_t mask,uint32_t expected) {
        for(unsigned elapsed=0;;elapsed+=10) {
            const auto v=io.read(reg);if(v==UINT32_MAX)return false;
            if((v&mask)==expected)return true;
            if(elapsed==1000)return false;io.delayUs(10);
        }
    }
private:
    template<class IO> static bool disableRegisters(IO &io,const Port &p) {
        return rmw(io,Gate,gateBit(p.tc),gateBit(p.tc)) && rmw(io,p.clock,0xf0000000,0) &&
            rmw(io,p.pll,Enable,0) && wait(io,p.pll,Locked,0) &&
            rmw(io,p.pll,Power,0) && wait(io,p.pll,Powered,0);
    }
public:
    template<class IO> static bool disable(IO &io,const Port &p) {
        return preflight(io,p) && disableRegisters(io,p);
    }
    template<class IO> static bool enable(IO &io,const Port &p,const TMDSClock &plan) {
        if(!plan.valid() || !preflight(io,p))return false;
        // No reprogramming of running PLLs. Preserve an enabled GOP until
        // its native path has explicitly quiesced and disabled the clock.
        if((io.read(p.pll)&(Enable|Locked)) || !(io.read(Gate)&gateBit(p.tc)) || (io.read(p.clock)&0xf0000000))return false;
        if(!rmw(io,p.pll,Power,Power) || !wait(io,p.pll,Powered,Powered))return false;
        for(const auto &word:plan.words) {
            auto value=word.value;if(!PHY::access(io,p,word.reg,word.mask,value,true))return false;
        }
        return rmw(io,p.pll,Enable,Enable) && wait(io,p.pll,Locked,Locked) &&
            rmw(io,p.clock,0xf0000000,0x80000000) && rmw(io,Gate,gateBit(p.tc),0);
    }
    // ADLP uses modular FIA, two TC ports per instance (intel_tc.c).
    // Fixed HDMI uses all four main-link lanes. PHY ownership registers are
    // generation-specific, but the FIA lane-enable register is still required.
    static uint32_t laneRegister(unsigned tc) {return tc<2?0x1638c0:0x16e8c0;}
    static uint32_t laneMask(unsigned tc) {return 15U<<(4*(tc&1));}
    template<class IO> static bool lanes(IO &io,const Port &p,unsigned count=4) {
        return (count==1 || count==2 || count==4) && preflight(io,p) && io.readable(laneRegister(p.tc)) &&
            rmw(io,laneRegister(p.tc),laneMask(p.tc),((1U<<count)-1)<<(4*(p.tc&1)));
    }
    enum class LinkStage : uint32_t {Preflight,ReleaseDDI,StopPLL,Lanes,PLL,DDI,Signal,Complete};
    // Both Linux and the exact donor acquire DDI IO after enabling the PLL.
    // Control/AUX power permits PLL access; output power cannot be its prerequisite.
    template<class IO> static bool startLink(IO &io,const Port &p,const TMDSClock &plan,uint32_t preset,LinkStage *stage=nullptr) {
        auto step=[&](LinkStage s){if(stage)*stage=s;};
        step(LinkStage::Preflight);if(!plan.valid() || !preflight(io,p))return false;
        step(LinkStage::ReleaseDDI);if(!io.linkPower(false))return false;
        step(LinkStage::StopPLL);if(!disable(io,p))return false;
        step(LinkStage::Lanes);if(!lanes(io,p))return false;
        step(LinkStage::PLL);if(!enable(io,p,plan))return false;
        step(LinkStage::DDI);if(!io.linkPower(true))return false;
        step(LinkStage::Signal);if(!TMDSSignal::program(io,p,preset))return false;
        step(LinkStage::Complete);return true;
    }
    template<class IO> static bool stopLink(IO &io,const Port &p) {
        return io.unstarted() || (stopPreflight(io,p) && io.linkPower(false) &&
            stopPreflight(io,p) && disableRegisters(io,p));
    }

};
}
#endif
