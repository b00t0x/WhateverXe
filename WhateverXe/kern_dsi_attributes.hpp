// Attribute routing for the exact TGL framebuffer donor. Keep transport-only
// operations away from the adopted DSI path; leave OS bookkeeping and native
// Unsupported results to their original implementations.
#ifndef KERN_DSI_ATTRIBUTES_HPP
#define KERN_DSI_ATTRIBUTES_HPP
#include <stdint.h>
namespace XELP { namespace DSI { namespace AppleAdapter {
inline bool transportAttribute(uint32_t attribute) {
    switch(attribute) {
        // AGPM selectors belong to the shared GPU power implementation and
        // are not DSI transport requests. Do not filter them here.
        case 0x68646370U: // hdcp: DisplayPath::initHDCP (DDI/AUX)
        case 0x73707772U: // spwr: never run the donor DDI shutdown on the DSI panel
            return true;
        default:return false;
    }
}
inline bool transportConnectionAttribute(uint32_t attribute) {
    switch(attribute) {
        case 0x00696772U: // igr: direct native hwSetBacklight
        case 0x706E6C74U: // pnlt: native panel power configuration
        case 0x7073723FU: // psr?: eDP panel self-refresh
        case 0x70726F62U: // prob: native physical connection probe
        case 0x64707461U: // dpta: Port::setLinkConfig (DP)
        case 0x7073636EU: // pscn: hardware panel-fitter update, not GPU scaling
        // Native setAttributeForUpstream reaches PAVP/HDCP link operations.
        case 0x00617572U: case 0x00617573U: case 0x00617577U:
        case 0x61757063U: case 0x61757068U: case 0x61757070U: case 0x61757072U:
        case 0x61756D73U: case 0x61756363U:
            return true;
        default:return false;
    }
}
}}}
#endif
