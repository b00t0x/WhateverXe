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
inline bool annotateMode(const Mode &m,ModeInformation &info,bool rotated=false) {
    if(!m.valid() || info.width!=(rotated?m.height:m.width) || info.height!=(rotated?m.width:m.height))return false;
    info.refresh=static_cast<uint32_t>((uint64_t(m.clockKHz)*1000*65536+m.htotal()*uint64_t(m.vtotal())/2)/
        (m.htotal()*uint64_t(m.vtotal())));
    info.depth=0; // the adopted DSI surface is RGB8 only
    info.widthMM=static_cast<uint16_t>(rotated?m.heightMM:m.widthMM);info.heightMM=static_cast<uint16_t>(rotated?m.widthMM:m.heightMM);
    // IOGraphics: valid/safe/default + built-in + native. Acceleration is a
    // separate runtime qualification, never inferred from a fixed mode.
    info.flags|=0x02000407;
    return true;
}
// CoreDisplay can replace the reserved client IDs when rebuilding its mode
// list. Treat reserved IDs as opaque: the complete
// timing, not an assumed generation/index bit layout, qualifies this alias.
inline bool clientTiming(const Timing &fixed,const Timing &offered,unsigned index) {
    if(index>=1024 || offered.reservedA[0]<0x80000000U || offered.reservedA[0]==UINT32_MAX || offered.signalLevels>1)return false;
    const uint64_t rounded=((fixed.pixelClock+5000)/10000)*10000;
    if(offered.pixelClock!=rounded || offered.minPixelClock!=rounded || offered.maxPixelClock!=rounded ||
       rounded<fixed.minPixelClock || rounded>fixed.maxPixelClock)return false;
    auto normalized=offered;
    normalized.reservedA[0]=fixed.reservedA[0];normalized.signalLevels=fixed.signalLevels;
    normalized.pixelClock=fixed.pixelClock;normalized.minPixelClock=fixed.minPixelClock;normalized.maxPixelClock=fixed.maxPixelClock;
    return !memcmp(&normalized,&fixed,sizeof(fixed));
}
inline bool acceptsMode(uint32_t mode,uint32_t clientMode) {
    return mode==FixedModeID || mode==clientMode;
}
// Presentation changes never alter the qualified DSI wire timing. Initially
// admit only native-size quarter-turns; scaled sources are a separate stage.
inline bool presentationTiming(const Timing &fixed,const Timing &offered,unsigned index) {
    if(offered.scalerFlags!=0x30 && offered.scalerFlags!=0x50)return false;
    if(offered.horizontalScaled!=fixed.verticalActive || offered.verticalScaled!=fixed.horizontalActive)return false;
    auto native=offered;native.scalerFlags=0;native.horizontalScaled=0;native.verticalScaled=0;
    return clientTiming(fixed,native,index);
}
// Synthetic Apple enumeration descriptor with timing from the panel profile.
// EDID wire layout reference: Linux v6.12 include/drm/drm_edid.h. This is not
// an EDID read from the panel and does not advertise DP/HDMI, HDR or DPMS.
inline bool makePanelEDID(const Mode &m,uint8_t (&edid)[128]) {
    memset(edid,0,sizeof(edid));
    const uint32_t clock=(m.clockKHz+5)/10;
    if(!m.valid() || !clock || clock>65535 || m.width>4095 || m.height>4095 ||
       m.hblank>4095 || m.vblank>4095 || m.hfront>1023 || m.hsync>1023 ||
       m.vfront>63 || m.vsync>63 || m.widthMM>4095 || m.heightMM>4095 ||
       m.widthMM>2550 || m.heightMM>2550)return false;
    const uint32_t rounded=clock*10;
    const uint32_t error=rounded>m.clockKHz ? rounded-m.clockKHz : m.clockKHz-rounded;
    if(uint64_t(error)*10000>m.clockKHz)return false; // retained link: at most 100 ppm
    const uint8_t header[]={0,255,255,255,255,255,255,0};memcpy(edid,header,8);
    edid[8]=0x5C;edid[9]=0xB8; // synthetic WEX identity, no panel serial
    edid[10]=0x10;edid[11]=0xD5;
    // EDID 1.4 model year of this synthetic descriptor, not panel manufacture.
    // A zero year selects Apple's pre-2007 non-linear-only backlight path.
    edid[16]=0xFF;edid[17]=2026-1990;
    edid[18]=1;edid[19]=4;edid[20]=0xA0; // digital RGB8, transport unspecified
    edid[21]=static_cast<uint8_t>((m.widthMM+5)/10);
    edid[22]=static_cast<uint8_t>((m.heightMM+5)/10);
    edid[23]=0xFF;edid[24]=2; // gamma unspecified, preferred DTD, fixed frequency
    for(unsigned i=38;i<54;++i)edid[i]=1; // no standard/established timings
    auto d=edid+54;d[0]=clock;d[1]=clock>>8;
    d[2]=m.width;d[3]=m.hblank;d[4]=((m.width>>8)<<4)|(m.hblank>>8);
    d[5]=m.height;d[6]=m.vblank;d[7]=((m.height>>8)<<4)|(m.vblank>>8);
    d[8]=m.hfront;d[9]=m.hsync;d[10]=((m.vfront&15)<<4)|(m.vsync&15);
    d[11]=((m.hfront>>8)<<6)|((m.hsync>>8)<<4)|((m.vfront>>4)<<2)|(m.vsync>>4);
    d[12]=m.widthMM;d[13]=m.heightMM;d[14]=((m.widthMM>>8)<<4)|(m.heightMM>>8);
    d[17]=0x18; // separate negative sync, matching the fixed Apple timing
    edid[75]=0xFC;memcpy(edid+77,"WEX DSI VBT\n  ",13);
    edid[93]=0x10;edid[111]=0x10; // unused descriptor slots
    uint8_t sum=0;for(unsigned i=0;i<127;++i)sum+=edid[i];edid[127]=static_cast<uint8_t>(0-sum);
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
