// Combo clocks are shared resources, not a PHY-numbered array of owners.
// Linux v6.12 intel_ddi.c and intel_dpll_mgr.c (icl_get_combo_phy_dpll).
#ifndef KERN_XELP_COMBO_CLOCK_HPP
#define KERN_XELP_COMBO_CLOCK_HPP
#include <stdint.h>

namespace XELPComboClock {
// Hardware inventory, independent of connector wiring or native port mode.
// TGL/ADL-P/N allocate only DPLL0/1: PLL2 is not a third Combo resource.
// ADL-S preserves the existing three-slot donor backend and fourth live peer.
enum class Layout {ADLS,TGL,Display13};
inline uint32_t donorPC(uintptr_t image,uintptr_t caller) {
    return image && caller>=image && caller-image<0x6DE58 ? uint32_t(caller-image):UINT32_MAX;
}
struct Port {
    uint32_t phy {},clockShift {},clockOff {};
    explicit Port(uint32_t p):phy(p),clockShift(2*(p%3)),clockOff(1U<<(p==0?10:p==1?11:p==2?24:p+1)){}
};
struct PLL {
    uint32_t enable {},cfg0 {},cfg1 {};
    static bool resolve(uint32_t id,PLL &out) {
        out={};if(id>3)return false;
        const uint32_t enables[]={0x46010,0x46014,0x46018,0x46030};
        const uint32_t configs[]={0x164284,0x16428C,0x164294,0x1642C0};
        out={enables[id],configs[id],configs[id]+4};return true;
    }
};
struct ClockSnapshot {
    uint32_t mux[2] {},enable[4] {};
    Layout layout=Layout::ADLS;
    unsigned pllCount() const {return layout==Layout::ADLS?4:2;}
    unsigned slots() const {return layout==Layout::ADLS?3:2;}
    bool valid() const {
        for(auto v:mux)if(v==UINT32_MAX)return false;
        for(auto v:enable)if(v==UINT32_MAX)return false;
        return true;
    }
    template<class P> uint32_t value(const P &p) const {return mux[p.phy<3?0:1];}
    template<class P> bool active(const P &p) const {return !(value(p)&p.clockOff);}
    template<class P> uint32_t selected(const P &p) const {return (value(p)>>p.clockShift)&3;}
    template<class P> uint32_t peers(const P &target) const {
        uint32_t mask=0;
        for(unsigned phy=0;phy<(layout==Layout::ADLS?5U:2U);++phy) {
            Port p(phy);
            if(p.phy!=target.phy && active(p))mask|=1U<<selected(p);
        }
        return mask;
    }
};
struct ClockChoice {
    uint32_t pll {UINT32_MAX};
    bool valid {},keepPLL {};
};
struct ClockAllocation {
    template<class P> ClockChoice share(const P &source,const ClockSnapshot &s) const {
        if(source.phy>=s.slots() || external[source.phy] || !s.valid() || !s.active(source))return {};
        const auto pll=s.selected(source);
        return pll<s.pllCount() && (s.enable[pll]&0xCC000000U)==0xCC000000U ?
            ClockChoice{pll,true,true}:ClockChoice{};
    }
    // PLL+1, including an incomplete enable whose output is still gated.
    // The runtime serializes this state; startup clears only unowned bindings.
    uint8_t binding[3] {};
    // Explicit owner of a firmware link outside the donor clock transaction
    // (DSI may keep using its PLL with the DDI mux gated). Native enable/off
    // and sharing must not reprogram this owner or treat it as a DP clock.
    uintptr_t external[3] {};
    bool reset() {
        for(auto owner:external)if(owner)return false;
        for(auto &slot:binding)slot=0;
        return true;
    }
    bool retain(unsigned phy,unsigned pll,uintptr_t owner,const ClockSnapshot &s) {
        if(!owner || phy>=s.slots() || pll>=s.pllCount() || !s.valid() ||
           s.selected(Port(phy))!=pll || (s.enable[pll]&0xCC000000U)!=0xCC000000U)return false;
        if(external[phy])return external[phy]==owner && binding[phy]==pll+1;
        if(binding[phy] || (s.peers(Port(phy))&(1U<<pll)))return false;
        for(unsigned p=0;p<3;++p)if(p!=phy && binding[p]==pll+1)return false;
        external[phy]=owner;binding[phy]=static_cast<uint8_t>(pll+1);return true;
    }
    // Caller must first stop/detach this link and drain its consumers under
    // the same serialized ownership scope. A closed DDI gate is insufficient.
    bool release(unsigned phy,uintptr_t owner) {
        if(phy>=3 || !owner || external[phy]!=owner)return false;
        external[phy]=0;binding[phy]=0;return true;
    }
    template<class P> ClockChoice choose(const P &p,const ClockSnapshot &s,bool on) const {
        if(p.phy>=s.slots() || external[p.phy] || !s.valid())return {};
        auto peers=s.peers(p);
        for(unsigned phy=0;phy<3;++phy)
            if(phy!=p.phy && binding[phy])peers|=1U<<(binding[phy]-1);
        const auto previous=s.active(p)?s.selected(p):binding[p.phy]?binding[p.phy]-1:UINT32_MAX;
        if(previous!=UINT32_MAX && previous>=s.pllCount())return {};
        if(!on)return {previous,true,previous==UINT32_MAX || bool(peers&(1U<<previous))};
        if(previous<s.pllCount() && !(peers&(1U<<previous)))return {previous,true,false};
        // Do not commandeer an enabled but unaccounted-for firmware PLL.
        auto busy=peers;
        for(unsigned pll=0;pll<s.pllCount();++pll)if(s.enable[pll]&0xCC000000U)busy|=1U<<pll;
        if(!(busy&(1U<<p.phy)))return {p.phy,true,false};
        for(unsigned pll=0;pll<s.slots();++pll)if(!(busy&(1U<<pll)))return {pll,true,false};
        return {};
    }
};
// Only the qualified donor clock body's own accesses are translated. In
// particular calculateVoltageLevel must continue to inspect physical PLLs.
struct ClockTranslation {
    uint32_t phy {},pll {};
    bool enabling {};
    bool caller(uint32_t pc) const {
        return enabling?(pc>=0x6CE10 && pc<0x6DA26):(pc>=0x6DAE6 && pc<0x6DE58);
    }
    unsigned long reg(unsigned long r,uint32_t pc) const {
        if(!caller(pc))return r;
        PLL from,to;
        if(!PLL::resolve(phy,from) || !PLL::resolve(pll,to))return r;
        if(r==from.enable)return to.enable;
        if(r==from.cfg0)return to.cfg0;
        if(r==from.cfg1)return to.cfg1;
        return r;
    }
    uint32_t mux(uint32_t value,uint32_t pc) const {
        if(!enabling || pc!=0x6D77E || phy>=3 || pll>=4)return value;
        return (value&~(3U<<(2*phy)))|(pll<<(2*phy));
    }
    template<class Read,class Write> void prepareMux(Read read,Write write,uint32_t value) const {
        const auto old=read(0x164280);
        Port route(phy);
        if((old^value)&(3U<<route.clockShift)) {
            if(!(old&route.clockOff))write(0x164280,old|route.clockOff);
            write(0x164280,value|route.clockOff);
        }
    }
};
}
#endif
