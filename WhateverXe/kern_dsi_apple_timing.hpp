// Exact TGL donor's IODetailedTimingInformationV2 data contract. Apple-specific
// layout stays outside the DSI host; no native object is owned by this value.
#ifndef KERN_DSI_APPLE_TIMING_HPP
#define KERN_DSI_APPLE_TIMING_HPP
#include "kern_dsi_profile.hpp"
namespace XELP { namespace DSI { namespace AppleAdapter {
struct Timing {
    uint32_t reservedA[3] {},horizontalInset {},verticalInset {},scalerFlags {},horizontalScaled {},verticalScaled {};
    uint32_t signalConfig {},signalLevels {};
    uint64_t pixelClock {},minPixelClock {},maxPixelClock {};
    uint32_t horizontalActive {},horizontalBlanking {},horizontalSyncOffset {},horizontalSyncWidth {};
    uint32_t verticalActive {},verticalBlanking {},verticalSyncOffset {},verticalSyncWidth {};
    uint32_t borders[4] {},horizontalSyncConfig {},horizontalSyncLevel {},verticalSyncConfig {},verticalSyncLevel {};
    uint32_t numLinks {},verticalBlankingExtension {};
    uint16_t pixelEncoding {},bitsPerColorComponent {},colorimetry {},dynamicRange {},dsc[3] {},reservedB[5] {};
};
static_assert(sizeof(Timing)==160 && offsetof(Timing,pixelClock)==0x28 &&
              offsetof(Timing,horizontalActive)==0x40 && offsetof(Timing,pixelEncoding)==0x88,"Apple timing ABI");
constexpr uint32_t FixedModeID=0x10000;
struct ModeInformation {
    uint32_t width {},height {},refresh {},depth {},flags {};
    uint16_t widthMM {},heightMM {};
    uint32_t reserved[3] {};
};
static_assert(sizeof(ModeInformation)==36,"IODisplayModeInformation ABI");
inline bool annotateMode(const Mode &m,ModeInformation &info) {
    if(!m.valid() || info.width!=m.width || info.height!=m.height)return false;
    info.refresh=static_cast<uint32_t>((uint64_t(m.clockKHz)*1000*65536+m.htotal()*uint64_t(m.vtotal())/2)/
        (m.htotal()*uint64_t(m.vtotal())));
    info.widthMM=static_cast<uint16_t>(m.widthMM);info.heightMM=static_cast<uint16_t>(m.heightMM);
    // IOGraphics: valid/safe/default + built-in + native. Acceleration is a
    // separate runtime qualification, never inferred from a fixed mode.
    info.flags|=0x02000407;
    return true;
}
inline bool makeTiming(const Profile &p,Timing &result) {
    result={};const auto &m=p.mode;
    if(!p.valid() || !m.valid())return false;
    result.pixelClock=uint64_t(m.clockKHz)*1000;
    const auto error=(result.pixelClock+9999)/10000; // 100 ppm, also used by DSI readout
    result.minPixelClock=result.pixelClock-error;result.maxPixelClock=result.pixelClock+error;
    result.horizontalActive=m.width;result.horizontalBlanking=m.hblank;
    result.horizontalSyncOffset=m.hfront;result.horizontalSyncWidth=m.hsync;
    result.verticalActive=m.height;result.verticalBlanking=m.vblank;
    result.verticalSyncOffset=m.vfront;result.verticalSyncWidth=m.vsync;
    // IOGraphics timing feature fields are bit sets, not literal bit counts.
    result.numLinks=1;result.pixelEncoding=1;result.bitsPerColorComponent=2;
    result.colorimetry=1;result.dynamicRange=1;
    return true;
}
}}}
#endif
