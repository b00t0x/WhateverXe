// Generation-aware implementation behind the donor's probePortMode ABI.
// IO owns the powered mapping; this performs reads only, never TC acquisition.
#ifndef KERN_XELP_PROBE_HPP
#define KERN_XELP_PROBE_HPP
#include "kern_xelp_platform.hpp"
#include "kern_tgl_port_map.hpp"
#include "kern_display13_tc.hpp"
namespace XELP {
struct ProbeBinding {
    uintptr_t controller {},access {},platform {};
    ProbeConfig config;
    bool valid() const {return controller && access && platform && config.valid();}
    bool sameBinding(const ProbeBinding &b) const {
        return controller==b.controller && access==b.access && platform==b.platform && same(config,b.config);
    }
    bool bound(uintptr_t manager,uintptr_t record,const uint32_t *c) const {
        return valid() && manager==access && c && c[0]<PortCount && record==platform+0x20+24*c[0];
    }
};
template<class IO> uint32_t legacyTGLMode(IO &io,uint32_t ddi,uint32_t selected) {
    if(ddi<=2)return 1; // Preserve native Combo/panel mode classification.
    if(ddi>=PortCount || !(selected&(1U<<ddi)))return 0;
    TGLTypeCPort port;TGLFiaPort fia;
    if(!TGLTypeCPort::resolve(ddi,port) || !io.readable(0xC4000) || !port.legacyHPD(io.read(0xC4000)) ||
       !io.readable(0x1638A0))return 0;
    const auto first=io.read(0x1638A0);
    if(!port.resolveFia(first,fia) || !io.readable(fia.live) || !io.readable(fia.ready))return 0;
    const auto live=fia.live==0x1638A0 ? first : io.read(fia.live);
    return fia.readyForLegacy(live,io.read(fia.ready)) ? 4 : 0;
}
template<class IO> uint32_t probeMode(IO &io,const ProbeBinding &b,uintptr_t manager,uintptr_t record,const uint32_t *c) {
    if(!b.bound(manager,record,c))return 0;
    if(displayAdapter(b.config.profile.family)==DisplayAdapter::ADLS) {
        if(!(b.config.combo&b.config.comboBackend&(1U<<c[0])))return 0;
        const bool hdmi=(c[1]==1 || (c[1]>=9 && c[1]<=12)) && c[2]==2 && !c[3] && c[4]==0x800 && (c[5]&0xB)==2;
        const bool dp=c[1]==(c[0]>=3?c[0]+6:c[0]+1) && c[2]==1 && !c[3] && c[4]==0x400 && c[5]==0x201;
        const bool panel=c[0]==0 && !c[1] && !c[2] && !c[3] && c[4]==2 && c[5]==0x18;
        return hdmi || dp || panel ? 1 : 0;
    }
    if(b.config.profile.display==Display::XeD)
        return legacyTGLMode(io,c[0],b.config.legacy&b.config.legacyBackend);
    if(c[0]>=3) {
        Display13TC::Port p;
        const bool hdmi=c[1]>=9 && c[1]<=12 && c[2]==2 && c[3]==1 && c[4]==0x800 && (c[5]&0xB)==2;
        const bool dp=c[1]==c[0]+6 && c[2]==1 && c[3]==1 && c[4]==0x400 && c[5]==0x201;
        if(dp && (b.config.typeC&b.config.legacyBackend&(1U<<c[0])) && Display13TC::Port::resolve(c[0],p)) {
            if(!io.readable(0xc4000) || !io.readable(0x44470))return 0;
            return p.altHPD(io.read(0xc4000),io.read(0x44470))?3:0;
        }
        if(!(b.config.legacy&b.config.legacyBackend&(1U<<c[0])) ||
           !Display13TC::Port::resolve(c[0],p) || (!hdmi && !dp) ||
           !io.readable(0xC4000) || !io.readable(p.ready))return 0;
        const auto ready=io.read(p.ready);
        // Fixed DP classification precedes setPortMode and AUX. A cold PHY
        // can assert ready only after ownership; mode acquisition polls it.
        return ready!=UINT32_MAX && (dp ? !(ready&3) : (ready&7)==4) && p.legacyHPD(io.read(0xC4000)) ? 4 : 0;
    }
    // Fixed Combo uses the same native mode for HDMI and SST DP. This is
    // classification only: startup separately proves AUX/PHY/boot operations.
    if(!(b.config.combo&b.config.comboBackend&(1U<<c[0])))return 0;
    const bool hdmi=c[1]>=1 && c[1]<=3 && c[2]==2 && !c[3] && c[4]==0x800 && (c[5]&0xB)==2;
    const bool dp=c[0]<2 && c[1]==c[0]+1 && c[2]==1 && !c[3] && c[4]==0x400 && c[5]==0x201;
    const bool panel=c[0]<2 && !c[1] && !c[2] && !c[3] && c[4]==2 && c[5]==0x18;
    return hdmi || dp || panel ? 1 : 0;
}
}
#endif
