// Board wiring is independent of native path/power/clock qualification.
#ifndef KERN_DISPLAY13_OUTPUT_WIRING_HPP
#define KERN_DISPLAY13_OUTPUT_WIRING_HPP
#include "kern_tgl_hdmi_wiring.hpp"
#include "kern_tgl_combo_dp.hpp"
struct Display13OutputWiring {
    static constexpr unsigned Ports=9;
    static constexpr uint32_t Physical=0x7B; // A/B + TC1..4, no Combo C.
    // panel is eDP. DSI uses the donor's panel object shape, never its AUX,
    // training or HPD protocol. Initially only DSI0 on physical PHY A exists.
    uint32_t hdmi {},ddc[Ports] {},dp {},panel {},typeC {},dsi {};bool valid {};
    Display13OutputWiring()=default;
    Display13OutputWiring(const TGLHDMIWiring &w):hdmi(w.hdmi),valid(w.valid) {
        for(unsigned d=0;d<3;++d)ddc[d]=w.ddc[d];
    }
    // Keep the existing Combo-only explicit property byte-compatible.
    static Display13OutputWiring fromProperty(const void *data,uint32_t bytes) {
        return TGLHDMIWiring::fromProperty(data,bytes);
    }
    uint32_t selected() const {return hdmi|dp|panel|dsi;}
    uint32_t links() const {return dp|panel;}
    uint32_t internal() const {return panel|dsi;}
    uint32_t hotplug() const {return selected()&~dsi;}
    enum class Transport {None,TMDS,DP,EDP,DSI};
    Transport transport(unsigned ddi) const {
        if(!described() || ddi>=Ports)return Transport::None;
        const auto bit=1U<<ddi;
        return dsi&bit ? Transport::DSI : panel&bit ? Transport::EDP :
            dp&bit ? Transport::DP : hdmi&bit ? Transport::TMDS : Transport::None;
    }
    bool described() const {
        if(!valid || !selected() || (selected()&~0x7FU) || (dp&~Physical) || (typeC&~(dp&0x78)) || (hdmi&dp) || (panel&~3U) || (panel&(panel-1)) || (panel&(hdmi|dp)) ||
           (dsi&~1U) || (dsi&(hdmi|dp|panel)))return false;
        uint32_t used=0;
        for(unsigned d=0;d<Ports;++d) {
            const auto pin=ddc[d];
            if(hdmi&(1U<<d)) {
                if(d<3 ? (pin<1 || pin>3) : (pin<9 || pin>12))return false;
                if(used&(1U<<pin))return false;
                used|=1U<<pin;
            } else if(pin)return false;
        }
        return true;
    }
    bool matchesHDMI(const uint32_t *c) const {
        return described() && c && c[0]<Ports && (hdmi&(1U<<c[0])) &&
            c[1]==ddc[c[0]] && c[2]==2 && c[3]==(c[0]>=3?1U:0U) &&
            c[4]==0x800 && (c[5]&0xB)==2;
    }
    bool usesDDC(uint32_t bus) const {
        if(!described())return false;
        for(unsigned d=0;d<Ports;++d)if((hdmi&(1U<<d)) && ddc[d]==bus)return true;
        return false;
    }
    bool matches(const uint32_t *c) const {
        if(!described() || !c || c[0]>=Ports)return false;
        if(hdmi&(1U<<c[0]))return matchesHDMI(c);
        if(internal()&(1U<<c[0])) {
            const uint32_t expected[]={c[0],0,0,0,2,0x18};
            return !memcmp(c,expected,sizeof(expected));
        }
        if(!(dp&(1U<<c[0])))return false;
        const uint32_t expected[]={c[0],c[0]<3?c[0]+1:c[0]+6,1,c[0]<3?0U:1U,0x400,0x201};
        return !memcmp(c,expected,sizeof(expected));
    }
    // Validate all native identities before any mutation. Preserve TC's
    // donor field3 and every unselected record for the topology adapter.
    bool apply(void *platform,uint32_t bytes) const {
        if(!described() || !platform || bytes<0x1C0 || !TGLComboDP::accepts(dp&3,platform,bytes))return false;
        auto p=static_cast<uint8_t *>(platform);if(p[0x12]!=Ports)return false;
        for(unsigned d=0;d<Ports;++d)if((hdmi|internal()|(dp&0x78))&(1U<<d)) {
            const auto c=reinterpret_cast<const uint32_t *>(p+0x20+24*d);
            const uint32_t panel[]={0,0,0,0,2,0x18};
            if(!d && !memcmp(c,panel,24))continue;
            if(c[0]!=d || c[1]!=(d<3?d+1:d+6) || c[2]!=1 || c[3]!=(d<3?0U:1U) ||
                c[4]!=0x400 || c[5]!=0x201)return false;
        }
        for(unsigned d=0;d<Ports;++d)if(hdmi&(1U<<d)) {
            auto c=reinterpret_cast<uint32_t *>(p+0x20+24*d);
            const bool panel=!d && !c[2];
            c[1]=ddc[d];c[2]=2;c[4]=0x800;c[5]=panel?0x202:(c[5]&~9U)|2U;
        }
        if(!TGLComboDP::apply(dp&3,platform,bytes))return false;
        for(unsigned d=0;d<2;++d)if(internal()&(1U<<d)) {
            const uint32_t c[]={d,0,0,0,2,0x18};memcpy(p+0x20+24*d,c,sizeof(c));
        }
        return true;
    }
};
#endif
