// Fixed SST DP on Display13 Dekel. Linux v6.12 intel_dpll_mgr.c,
// intel_ddi.c and ADLP DKL signal tables; no board-specific coefficients.
#ifndef KERN_DISPLAY13_TC_DP_HPP
#define KERN_DISPLAY13_TC_DP_HPP
#include "kern_display13_tc_clock.hpp"
#include "kern_tgl_port_map.hpp"
namespace Display13TC {
struct DPClock : TMDSClock {
    static DPClock calculate(uint32_t linkRate,uint32_t ref) {
        DPClock p;
        if((linkRate!=6 && linkRate!=10 && linkRate!=20 && linkRate!=30) ||
           (ref!=19200 && ref!=24000 && ref!=38400))return p;
        const auto clock=linkRate*27000;
        const unsigned dividers[]={7,5,3,2};
        for(unsigned h:dividers)for(unsigned d=10;d;--d) {
            if(uint64_t(clock)*5*h*d!=8100000)continue;
            const auto integer=8100000/(ref*2),rem=8100000%(ref*2);
            const auto fraction=uint32_t((uint64_t(rem)<<22)/(ref*2));
            const auto target=uint32_t((2000000000ULL/(132*ref)+5)/10);
            const auto ratio=h==7?3:h==5?2:h==3?1:0;
            p.rate=clock;p.reference=ref;p.dco=8100000;p.hs=h;p.ds=d;
            p.words[0]={0x212c,0x700,0x100};
            p.words[1]={0x20d8,0xff00,(d>=2?10U:5U)<<8};
            p.words[2]={0x20d4,0x1ff00,(d>=2?0x4000U:0U)|(ratio<<12)|(d<<8)};
            p.words[3]={0x2200,0x1fffff,0x84200|integer};
            p.words[4]={0x2204,0x1f00ff,((ref==24000?25U:28U)<<16)|target};
            p.words[5]={0x2210,0xe0ff3a00,(ref==38400?0x40000000U:0x20000000U)|0x2000};
            p.words[6]={0x2214,0x7fffff00,(fraction?0x40000000U:0U)|(fraction<<8)};
            p.words[7]={0x2218,0xffff,rem?82U:0U}; // 200000000 / (8100000 * 3/10)
            return p;
        }
        return {};
    }
};
// Retain only a link programmed by this owner, with matching live PLL/FIA.
// AGDC success leaves it enabled before native adoption and LightUp.
struct DPRetainedLink {
    uint32_t rate {},reference {},lanes {};
    template<class IO> bool matches(IO &io,const Port &p,const TMDSClock &plan,unsigned count) const {
        if(!plan.valid() || !rate || rate!=plan.rate || reference!=plan.reference || lanes!=count ||
           (count!=1 && count!=2 && count!=4) || !io.owned(p.ddi) || !io.linkHeld())return false;
        const uint32_t registers[]={p.pll,p.clock,Clock::Gate,Clock::laneRegister(p.tc)};
        for(auto reg:registers)if(!io.readable(reg) || io.read(reg)==UINT32_MAX)return false;
        if((io.read(p.pll)&0xcc000000U)!=0xcc000000U || (io.read(p.clock)&0xf0000000U)!=0x80000000U ||
           (io.read(Clock::Gate)&Clock::gateBit(p.tc)) ||
           (io.read(Clock::laneRegister(p.tc))&Clock::laneMask(p.tc))!=((1U<<count)-1)<<(4*(p.tc&1)))return false;
        for(const auto &word:plan.words) {
            uint32_t value=0;if(!PHY::access(io,p,word.reg,word.mask,value,false) || (value&word.mask)!=word.value)return false;
        }
        return true;
    }
};
struct DPSignal {
    // ADL-P/N DDI_BUF_PHY_LINK_RATE, Linux ddi_buf_phy_link_rate().
    // The TGL donor's bits15:8 do not select this Display13 PHY rate.
    static bool bufferRate(uint32_t rate,uint32_t &buffer) {
        unsigned code;
        switch(rate) {case 6:code=0;break;case 10:code=1;break;case 20:code=2;break;case 30:code=3;break;default:return false;}
        buffer=(buffer&~0x700000U)|(code<<20);return true;
    }
    // ADL-P/N, Linux adlp_tbt_to_dp_alt_switch_wa: release both PCS cores
    // after selecting DP training and before enabling the DDI buffer.
    // This is independent of the current USB-C orientation and pin mask.
    template<class IO> static bool prepareBuffer(IO &io,const Port &p,uint32_t before[2],uint32_t after[2]) {
        if(!io.powerHeld() || !io.linkHeld() || !io.owned(p.ddi) || !io.readable(p.buffer))return false;
        const auto buffer=io.read(p.buffer);if(buffer==UINT32_MAX)return false;
        for(unsigned group=0;group<2;++group) {
            if(!PHY::access(io,p,group*0x1000+0x14,UINT32_MAX,before[group],false))return false;
            after[group]=before[group];
        }
        if(!((before[0]|before[1])&0x800))return true;
        // A retained live link must already have released reset. Do not
        // alter PCS underneath an enabled or not-yet-idle transmitter.
        if((buffer&0x80000080U)!=0x80)return false;
        for(unsigned group=0;group<2;++group)if(before[group]&0x800) {
            uint32_t value=0;
            if(!PHY::access(io,p,group*0x1000+0x14,0x800,value,true))return false;
            after[group]=value;
        }
        return true;
    }
    static bool coefficients(uint32_t rate,uint32_t swing,uint32_t emphasis,uint32_t &value) {
        if((rate!=6 && rate!=10 && rate!=20 && rate!=30) || swing>3 || emphasis>3 || swing+emphasis>3)return false;
        constexpr uint8_t starts[]={0,4,7,9},voltage[]={7,5,2,0,5,2,0,2,0,0};
        constexpr uint8_t low[]={1,6,11,23,0,8,20,0,11,0},high[]={0,4,10,24,0,6,20,0,9,0};
        const auto index=starts[swing]+emphasis;
        value=voltage[index]|(uint32_t(rate<=10?low[index]:high[index])<<8);return true;
    }
    template<class IO> static bool mode(IO &io,const Port &p,unsigned lanes,unsigned pin=0) {
        if(!io.powerHeld() || !io.owned(p.ddi) || !io.quiescent(p.ddi) || (lanes!=1 && lanes!=2 && lanes!=4))return false;
        // The pin-to-X1/X2 calculation is shared with Display12. Only the
        // powered register access and ownership adapter differ on Display13.
        uint32_t modes[2];if(!TGLDklDPMode::select(pin,lanes,modes))return false;
        for(unsigned group=0;group<2;++group) {
            uint32_t value=modes[group];
            if(!PHY::access(io,p,0x1000*group+0xa0,0xc0,value,true))return false;
        }
        return true;
    }
    // The donor negotiates a common swing/emphasis for the active lanes.
    // Training can change the electrical level while the DDI buffer is on.
    template<class IO> static bool program(IO &io,const Port &p,unsigned rate,unsigned swing,unsigned emphasis,unsigned physical=15) {
        uint32_t selected;
        if(!physical || physical>15 || !io.powerHeld() || !io.owned(p.ddi) || !coefficients(rate,swing,emphasis,selected))return false;
        for(unsigned group=0;group<2;++group) {
            if(!(physical&(3U<<(2*group))))continue;
            const auto bank=0x1000*group;uint32_t zero=0;
            if(!PHY::access(io,p,bank+0xd00,UINT32_MAX,zero,true))return false;
            for(unsigned tx=0;tx<2;++tx) {
                if(!(physical&(1U<<(2*group+tx))))continue;
                auto value=selected;
                if(!PHY::access(io,p,bank+0x2c0+4*tx,0x3ff07,value,true))return false;
            }
            const auto loadMask=4U|((physical&(1U<<(2*group)))?0x18U:0U)|((physical&(2U<<(2*group)))?0x60U:0U);
            zero=0;if(!PHY::access(io,p,bank+0x2c8,loadMask,zero,true))return false;
        }
        return true;
    }
    template<class IO> static bool startLink(IO &io,const Port &p,const DPClock &plan,unsigned lanes,Clock::LinkStage *stage=nullptr,unsigned pin=0,unsigned physical=15) {
        auto step=[&](Clock::LinkStage s){if(stage)*stage=s;};
        step(Clock::LinkStage::Preflight);
        unsigned available=0;for(unsigned bit=0;bit<4;++bit)available+=(physical>>bit)&1;
        if(!physical || physical>15 || available<lanes || pin>6 || !plan.valid() || (lanes!=1 && lanes!=2 && lanes!=4) || !Clock::preflight(io,p))return false;
        step(Clock::LinkStage::ReleaseDDI);if(!io.linkPower(false))return false;
        step(Clock::LinkStage::StopPLL);if(!Clock::disable(io,p))return false;
        step(Clock::LinkStage::Lanes);if(!Clock::lanes(io,p,lanes))return false;
        step(Clock::LinkStage::PLL);if(!Clock::enable(io,p,plan))return false;
        step(Clock::LinkStage::DDI);if(!io.linkPower(true))return false;
        step(Clock::LinkStage::Signal);
        if(!mode(io,p,lanes,pin) || !program(io,p,plan.rate/27000,0,0,physical))return false;
        step(Clock::LinkStage::Complete);return true;
    }
};
}
#endif
