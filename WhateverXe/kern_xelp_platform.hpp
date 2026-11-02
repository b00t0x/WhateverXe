// Shared WhateverXe/provider CPU contract. Recognition is not runtime qualification.
// Keep hardware capability, donor ABI and board transport as separate inputs.
#ifndef KERN_XELP_PLATFORM_HPP
#define KERN_XELP_PLATFORM_HPP
#include <stdint.h>
#include <stddef.h>
#include "kern_xelp_devices.hpp"
#include "kern_xelp_display_resources.hpp"
namespace XELP {
struct Profile {
    uint32_t identity {}, revision {}; // Physical config-space identity, never spoofed.
    Family family {Family::Unknown};
    Display display {Display::Unknown};
    GT gt {GT::Unknown};
    Donor donor {Donor::Unknown};
    uint32_t effectiveIdentity {}; // Table selector; may come from device-id.
    // Mobile Display13 selection is distinct from whether Render is requested.
    // The donor selector never changes the physical Display13 identity.
    bool mobileDisplay13() const {
        return mobileDisplay13Device(identity) && revision<=255 &&
            (effectiveIdentity==identity || effectiveIdentity==0x9A498086) &&
            display==Display::XeLPD && gt==GT::ADLPMobile && donor==Donor::C004FC1A &&
            family==deviceEntry(identity)->family;
    }
    bool displayAdmitted() const {return admitted();}
    bool overridden() const {return identity!=effectiveIdentity;}
    // Select the implemented family backend for every PCI revision. Hardware
    // validation is independent; a stepping split needs a concrete behavior delta.
    bool admitted() const {return (identity&0xFFFF)==0x8086 && revision<=255 &&
        donor==Donor::C004FC1A && gt!=GT::Unknown &&
        ((gt!=GT::ADLPMobile && !mobileDisplay13Device(identity)) || mobileDisplay13()) &&
        ((!display12SmallGT(gt) && !display12SmallGTDevice(identity)) ||
         (display12SmallGT(gt) && display12SmallGTDevice(identity) && !overridden()));}
};
inline Profile identify(uint32_t physical,uint32_t revision,uint32_t effective) {
    Profile p;p.identity=physical;p.revision=revision;p.effectiveIdentity=effective;
    if((physical&0xFFFF)!=0x8086 || revision>255)return p;
    const auto entry=deviceEntry(effective);if(!entry)return p;
    const auto physicalEntry=deviceEntry(physical);
    if(physicalEntry && physicalEntry->backend==GT::ADLPMobile) {
        p.family=physicalEntry->family;p.display=physicalEntry->display;p.gt=physicalEntry->backend;
        if(effective==physical || effective==0x9A498086)p.donor=Donor::C004FC1A;
        return p;
    }
    p.family=entry->family;p.display=entry->display;p.gt=entry->backend;
    if(p.gt!=GT::Unknown)p.donor=Donor::C004FC1A;
    return p;
}
inline Profile identify(uint32_t pci,uint32_t revision) {return identify(pci,revision,pci);}
// IOPCIFamily also publishes device-id when no override is injected. Equal
// values are ordinary physical selection. An invalid explicit value never
// silently falls back to a different backend.
inline Profile select(uint32_t pci,uint32_t revision,bool present,const void *data,size_t size) {
    if(!present)return identify(pci,revision);
    if(!data || size!=4)return identify(pci,revision,0);
    const auto b=static_cast<const uint8_t *>(data);
    const uint32_t id=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);
    if(!id || id>=0xFFFF)return identify(pci,revision,0);
    return identify(pci,revision,(id<<16)|(pci&0xFFFF));
}
inline bool same(const Profile &a,const Profile &b) {
    return a.identity==b.identity && a.revision==b.revision && a.family==b.family &&
        a.display==b.display && a.gt==b.gt && a.donor==b.donor && a.effectiveIdentity==b.effectiveIdentity;
}
inline const char *model(const Profile &p) {
    // Prefer the actual known product name even when trying another backend.
    auto entry=deviceEntry(p.identity);if(!entry)entry=deviceEntry(p.effectiveIdentity);
    return entry ? entry->model : nullptr;
}
constexpr uint32_t PortCount=9,PortMask=(1U<<PortCount)-1,PipeCount=3;
// A validated native association. The adapter validates transport-specific
// record contents before construction; the core does not infer HDMI or DP.
struct NativePorts {
    const void *platform {};
    uint32_t selected {};
    bool valid() const {return platform && selected && !(selected&~PortMask);}
    bool bound(const void *config,uint32_t ddi) const {
        return valid() && ddi<PortCount && (selected&(1U<<ddi)) &&
            config==static_cast<const uint8_t *>(platform)+0x20+24*ddi;
    }
};
// ABI3 includes physical/effective identity and binds a profile and immutable probe policy, not mutable connection state.
// legacy permits probing for mode4; it does not force mode4 or classify a
// USB-C-capable connector as physically fixed DP. The provider checks generation-specific live state.
// DP-alt uses the existing typeC mask and an optional capability; ABI3 layout
// stays unchanged. An older provider must not bind the new route.
constexpr uint32_t ProbeABI=3,ProbeCapabilityABI=1,ProbeDisplay13ComboDP=1U,ProbeDisplay13FixedTC=2U,ProbeDisplay13FixedDP=4U,ProbeDisplay13Panel=8U,ProbeDisplay13DPAlt=16U;
using QueryProbeCapabilities=uint32_t (*)(uint32_t,uint32_t);
// Optional export: old ABI3 providers remain valid for their existing routes,
// but their lack of a capability reply cannot authorize a new DP route.
inline bool display13PanelProbeCapable(QueryProbeCapabilities query) {
    return query && (query(ProbeCapabilityABI,ProbeABI)&ProbeDisplay13Panel);
}
inline bool display13ProbeCapable(QueryProbeCapabilities query) {
    return query && (query(ProbeCapabilityABI,ProbeABI)&ProbeDisplay13ComboDP);
}
inline bool display13TCProbeCapable(QueryProbeCapabilities query) {
    return query && (query(ProbeCapabilityABI,ProbeABI)&ProbeDisplay13FixedTC);
}
inline bool display13TCDPProbeCapable(QueryProbeCapabilities query) {
    return query && (query(ProbeCapabilityABI,ProbeABI)&ProbeDisplay13FixedDP);
}
inline bool display13DPAltProbeCapable(QueryProbeCapabilities query) {
    return query && (query(ProbeCapabilityABI,ProbeABI)&ProbeDisplay13DPAlt);
}

struct ProbeConfig {
    uint32_t abi {ProbeABI},bytes {sizeof(ProbeConfig)};
    Profile profile;
    uint32_t combo {},legacy {},typeC {},excluded {},comboBackend {},legacyBackend {};
    bool valid() const {
        if(abi!=ProbeABI || bytes!=sizeof(ProbeConfig) || !profile.displayAdmitted() ||
           !same(profile,identify(profile.identity,profile.revision,profile.effectiveIdentity)))return false;
        // ADL-S firmware routes use the donor's three Combo PHY slots.
        // Its TC label grants neither Type-C nor Dekel register access.
        if(displayAdapter(profile.family)==DisplayAdapter::ADLS) {
            const auto capacity=ADLSComboBackend::mask(profile.donor);
            return profile.display==Display::XeD && combo && !(combo&~capacity) && comboBackend==capacity &&
                !(excluded&~PortMask) && !(excluded&combo) && !legacy && !typeC && !legacyBackend;
        }
        if((combo&~7U) || ((legacy|typeC|excluded|legacyBackend)&~0x1F8U) ||
           (comboBackend&~7U) || (legacy&typeC) || (legacy&excluded) || (typeC&excluded))return false;
        if(profile.display==Display::XeD)return comboBackend==7;
        return profile.display==Display::XeLPD && (combo|legacy|typeC) && !(combo&~3U) &&
            comboBackend && !(comboBackend&~3U) && !(typeC&~0x78U) &&
            !(legacy&~0x78U) && !(legacyBackend&~0x78U) && !((legacy|typeC)&~legacyBackend);
    }
};
inline bool same(const ProbeConfig &a,const ProbeConfig &b) {
    return a.abi==b.abi && a.bytes==b.bytes && same(a.profile,b.profile) && a.combo==b.combo &&
        a.legacy==b.legacy && a.typeC==b.typeC && a.excluded==b.excluded &&
        a.comboBackend==b.comboBackend && a.legacyBackend==b.legacyBackend;
}
using ConfigureProbe = bool (*)(void *,const ProbeConfig *);
using ProbePort = uint32_t (*)(void *);
}
#endif
