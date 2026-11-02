// Explicit Combo/TC wiring. Board wiring and native backend
// qualification are separate: describing a port does not attach unsupported resources.
#ifndef KERN_DISPLAY13_FB_TOPOLOGY_HPP
#define KERN_DISPLAY13_FB_TOPOLOGY_HPP
#include "kern_display13_hdmi_wiring.hpp"
#include "kern_xelp_platform.hpp"
#include "kern_xelp_donor.hpp"
namespace Display13FBTopology {
constexpr uint32_t Bytes=0x1C0,Ports=9;
inline bool selectedWiring(const Display13OutputWiring &w) {
    return w.described();
}
// Linux xe_lpd_display used by ADL-N has Combo A/B + TC1..4, no Combo C.
// Common three-Combo helpers above are not physical ADL-N admission.
inline bool adlnWiring(const Display13OutputWiring &w) {return selectedWiring(w) && !(w.selected()&~3U);}
// DP topology is composable, but runtime admission stays closed until its
// complete signal/training/bootstrap operation contract is installed.
inline bool supportedWiring(const Display13OutputWiring &w,bool fixedTC=false,bool dsiAdoption=false) {
    // Fixed HDMI is composable across the physical Combo A/B and TC1..4.
    // TC provider capability, legacy mode and clock hooks are checked separately.
    // DSI requires explicit GOP adoption selection. Native start separately
    // checks its mode, writer, probe, cache and disposal hooks.
    return (!w.dsi || dsiAdoption) && !(w.dp&~0x78U) && w.described() && !(w.selected()&~(fixedTC ? Display13OutputWiring::Physical : 3U));
}
// A TC route uses the same validated handoff regardless of the GT family.
// Keep the established Combo-only ADL-N startup ordering unchanged.
inline bool deferredBootPrepare(const Display13OutputWiring &w,bool mobile) {
    return w.internal() || mobile || (w.selected()&0x78);
}
inline bool multiOutputWiring(const Display13OutputWiring &w) {
    return selectedWiring(w) && (w.selected()&(w.selected()-1));
}
inline bool singleOutputWiring(const Display13OutputWiring &w) {
    return selectedWiring(w) && !multiOutputWiring(w);
}
inline uint32_t singleDDI(const Display13OutputWiring &w) {
    if(!singleOutputWiring(w))return UINT32_MAX;
    for(unsigned d=0;d<Ports;++d)if(w.selected()&(1U<<d))return d;
    return UINT32_MAX;
}
// TGL/XE_LPD TRANS_DDI_FUNC_CTL encodes physical DDI as port+1 at bit27.
// Dual board wiring alone does not identify the boot connector; use the
// active pipe's actual TMDS selection and reject an unselected connector.
inline uint32_t bootDDI(const Display13OutputWiring &w,uint32_t trans,bool dpSource=false) {
    if(!selectedWiring(w) || trans==UINT32_MAX || !(trans&(1U<<31)))return UINT32_MAX;
    const auto selector=(trans>>27)&15;
    if(selector<1 || selector>7)return UINT32_MAX;
    const auto port=selector-1;
    const auto mode=(trans>>24)&7;
    // Selection is not an adoption certificate. The DP caller must also prove
    // source state, physical TC ownership/power, memory and native handoff.
    if(dpSource && mode==2 && (port<2 || port>=3) && (w.links()&(1U<<port)))return port;
    if(mode>1)return UINT32_MAX;
    return (w.hdmi&(1U<<port)) ? port : UINT32_MAX;
}
// Physical ADLP/ADLN topology, independent of runtime backend admission.
inline bool xeLPDWiring(const Display13OutputWiring &w) {
    return selectedWiring(w) && !(w.selected()&~Display13OutputWiring::Physical);
}
// Current handoff adopts one real GOP on pipe A. A dual board policy does not
// authorize ignoring a second firmware scanout or an unknown/unpowered bank.
inline uint32_t bootstrapDDI(const Display13OutputWiring &w,bool complete,uint32_t powered,uint32_t active,uint32_t trans,bool dpSource=false) {
    return complete && active==1 && (powered&1) ? bootDDI(w,trans,dpSource) : UINT32_MAX;
}
// Preserve donor record count, DDI identities, memory sizing and unrelated
// settings. Native objects retain pointers to this stable copy. Preflight
// every identity and the existing HDMI-B record before any write.
inline bool apply(void *destination,uint32_t bytes,const void *source,const Display13OutputWiring &w) {
    if(!destination || !source || destination==source || bytes<Bytes || !selectedWiring(w))return false;
    const auto src=static_cast<const uint8_t *>(source);
    if(src[0x12]!=Ports)return false;
    const uint32_t panel[] {0,0,0,0,2,0x18};
    const uint32_t comboB[] {1,2,1,0,0x400,0x201};
    if(memcmp(src+0x20,panel,sizeof(panel)) || memcmp(src+0x38,comboB,sizeof(comboB)))return false;
    for(unsigned d=0;d<Ports;++d) {
        uint32_t ddi;memcpy(&ddi,src+0x20+24*d,sizeof(ddi));if(ddi!=d)return false;
    }
    alignas(uint64_t) uint8_t staging[Bytes];memcpy(staging,src,Bytes);
    auto dst=staging;
    if(!w.apply(dst,Bytes))return false;
    for(unsigned d=0;d<Ports;++d)if(!(w.selected()&(1U<<d))) {
        const uint32_t excluded[] {1,0}; // Native dummy connector, no panel flags.
        memcpy(dst+0x20+24*d+16,excluded,sizeof(excluded));
    }
    memcpy(destination,staging,Bytes);return true;
}
inline bool bound(const void *platform,const void *config,uint32_t ddi) {
    return platform && config && ddi<Ports &&
        static_cast<const uint8_t *>(config)==static_cast<const uint8_t *>(platform)+0x20+24*ddi;
}
inline bool selected(const void *platform,const void *config,const Display13OutputWiring &w) {
    if(!selectedWiring(w))return false;
    for(unsigned d=0;d<Ports;++d)if((w.selected()&(1U<<d)) && bound(platform,config,d) &&
        w.matches(static_cast<const uint32_t *>(config)))return true;
    return false;
}
// Transport adapter validates records under the native FB mutex. The binding
// core below consumes only validated port identities, independent of transport.
inline XELP::NativePorts boundPorts(const void *platform,const Display13OutputWiring &w) {
    if(!platform || !selectedWiring(w))return {};
    for(unsigned d=0;d<Ports;++d)if((w.selected()&(1U<<d)) &&
        !selected(platform,static_cast<const uint8_t *>(platform)+0x20+24*d,w))return {};
    return {platform,w.selected()};
}
// ICP/TGP SDE bits24..29 are TC1..6 hotplug, not legacy GMBUS bit24.
// Combo A..C hotplug uses bits16..18. Authorize exclusions only after
// every unselected native record is disabled and has no connection flags.
// Other PCH sources (including DDI D) remain outside this policy.
inline uint32_t excludedHotplug(const void *platform,uint32_t bytes,const Display13OutputWiring &w) {
    if(!platform || bytes<Bytes || !selectedWiring(w))return 0;
    const auto p=static_cast<const uint8_t *>(platform);
    if(p[0x12]!=Ports)return 0;
    for(unsigned d=0;d<Ports;++d)if((w.selected()&(1U<<d)) && !selected(platform,p+0x20+24*d,w))return 0;
    for(unsigned d=0;d<Ports;++d) {
        const auto config=reinterpret_cast<const uint32_t *>(p+0x20+24*d);
        if(config[0]!=d || (!(w.selected()&(1U<<d)) && (config[4]!=1 || config[5])))return 0;
    }
    return (0x3F000000 & ~((w.hotplug()>>3)<<24)) |
        ((7U & ~w.hotplug())<<16);
}
}
#endif
