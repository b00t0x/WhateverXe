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
// Donor depth indices: RGB8888=0, RGB2101010=1. Both occupy four bytes.
// These describe framebuffer storage, independently of the DSI wire format.
constexpr uint32_t MaxFramebufferDepth=1;
inline bool framebufferDepth(int32_t depth) {return depth>=0 && uint32_t(depth)<=MaxFramebufferDepth;}
struct ModeInformation {
    uint32_t width {},height {},refresh {},depth {},flags {};
    uint16_t widthMM {},heightMM {};
    uint32_t reserved[3] {};
};
static_assert(sizeof(ModeInformation)==36,"IODisplayModeInformation ABI");
inline bool annotateMode(const Mode &m,ModeInformation &info,bool rotated=false,uint32_t drawWidth=0,uint32_t drawHeight=0) {
    const auto width=drawWidth?drawWidth:(rotated?m.height:m.width);
    const auto height=drawHeight?drawHeight:(rotated?m.width:m.height);
    if(!m.valid() || info.width!=width || info.height!=height)return false;
    const auto refresh=((uint64_t(m.clockKHz)*1000*65536+m.htotal()*uint64_t(m.vtotal())/2)/
        (m.htotal()*uint64_t(m.vtotal())));
    if(refresh>UINT32_MAX)return false;
    info.refresh=uint32_t(refresh);
    info.depth=MaxFramebufferDepth;
    info.widthMM=static_cast<uint16_t>(rotated?m.heightMM:m.widthMM);info.heightMM=static_cast<uint16_t>(rotated?m.widthMM:m.heightMM);
    // IOGraphics: valid/safe/default + built-in + native. Acceleration is a
    // separate runtime qualification, never inferred from a fixed mode.
    info.flags|=0x403;
    if(width==(rotated?m.height:m.width) && height==(rotated?m.width:m.height))info.flags|=0x02000004;
    else info.flags&=~0x02000004U; // A scaled source is not the native/default mode.
    return true;
}
// A presentation mode reuses the bound DSI transport. CoreDisplay's
// numLinks hint cannot select a different physical link in this path.
inline Timing panelOffer(Timing t,const Timing &fixed) {
    t.numLinks=fixed.numLinks;
    return t;
}
// CoreDisplay can replace the reserved client IDs when rebuilding its mode
// list. Treat reserved IDs as opaque: the complete
// timing, not an assumed generation/index bit layout, qualifies this alias.
inline bool clientTiming(const Timing &fixed,const Timing &offered,unsigned /*index*/) {
    if(offered.reservedA[0]<0x80000000U || offered.reservedA[0]==UINT32_MAX || offered.signalLevels>1)return false;
    const uint64_t rounded=((fixed.pixelClock+5000)/10000)*10000;
    if((offered.pixelClock!=rounded && offered.pixelClock!=fixed.pixelClock) ||
       offered.minPixelClock!=offered.pixelClock || offered.maxPixelClock!=offered.pixelClock ||
       offered.pixelClock<fixed.minPixelClock || offered.pixelClock>fixed.maxPixelClock)return false;
    auto normalized=offered;
    normalized.reservedA[0]=fixed.reservedA[0];normalized.signalLevels=fixed.signalLevels;
    normalized.pixelClock=fixed.pixelClock;normalized.minPixelClock=fixed.minPixelClock;normalized.maxPixelClock=fixed.maxPixelClock;
    return !memcmp(&normalized,&fixed,sizeof(fixed));
}
inline bool acceptsMode(uint32_t mode,uint32_t clientMode) {
    return mode==FixedModeID || mode==clientMode;
}
inline bool presentationTiming(const Timing &fixed,const Timing &offered,unsigned index) {
    // Presentation is supplied by CoreDisplay. Do not whitelist rotation,
    // scaling sizes or mode-ID generations here; qualify only the wire timing.
    auto native=offered;native.scalerFlags=0;native.horizontalScaled=0;native.verticalScaled=0;
    native.horizontalInset=0;native.verticalInset=0;
    return clientTiming(fixed,native,index);
}
// Synthetic Apple enumeration descriptor with timing from the panel profile.
// EDID wire layout reference: Linux v6.12 include/drm/drm_edid.h. This is not
// an EDID read from the panel and does not advertise DP/HDMI, HDR or DPMS.
inline bool panelNeedsDisplayID(const Mode &m) {
    return (uint64_t(m.clockKHz)+5)/10>65535 || m.width>4095 || m.height>4095 || m.hblank>4095 || m.vblank>4095 ||
        m.hfront>1023 || m.hsync>1023 || m.vfront>63 || m.vsync>63;
}
inline bool makePanelEDID(const Mode &m,uint8_t (&edid)[128],unsigned block=1) {
    memset(edid,0,sizeof(edid));
    const uint32_t clock=uint32_t((uint64_t(m.clockKHz)+5)/10);
    if(!m.valid() || !clock || clock>0x1000000 ||
       m.widthMM>2550 || m.heightMM>2550)return false;
    const bool extended=panelNeedsDisplayID(m);
    // DisplayID Type I has 15-bit sync offsets, stored minus one. Preserve
    // the real panel timing instead of truncating the EDID DTD's six-bit VFP.
    if(extended && (!m.hfront || !m.vfront || m.hfront>32768 || m.vfront>32768))return false;
    if(block<1 || block>2 || (block==2 && !extended))return false;
    const uint32_t rounded=clock*10;
    const uint32_t error=rounded>m.clockKHz ? rounded-m.clockKHz : m.clockKHz-rounded;
    if(uint64_t(error)*10000>m.clockKHz)return false; // retained link: at most 100 ppm
    if(block==2) {
        // VESA DisplayID 1.2, Type I timing; Linux drm_displayid_internal.h
        // and Big Sur CoreDisplay's InstallDisplayIDEXT use this wire layout.
        edid[0]=0x70;edid[1]=0x12;edid[2]=23; // one 3+20 byte data block
        edid[3]=0;edid[4]=0; // EDID extension, no further DisplayID sections
        edid[5]=3;edid[6]=0;edid[7]=20;
        auto d=edid+8;const uint32_t encodedClock=clock-1;
        d[0]=encodedClock;d[1]=encodedClock>>8;d[2]=encodedClock>>16;d[3]=0x80;
        const uint16_t fields[]={m.width,m.hblank,m.hfront,m.hsync,m.height,m.vblank,m.vfront,m.vsync};
        for(unsigned i=0;i<8;++i) {
            const auto value=uint16_t(fields[i]-1);d[4+2*i]=value;d[5+2*i]=value>>8;
        }
        uint8_t section=0;for(unsigned i=1;i<28;++i)section+=edid[i];edid[28]=uint8_t(0-section);
        uint8_t sum=0;for(unsigned i=0;i<127;++i)sum+=edid[i];edid[127]=uint8_t(0-sum);
        return true;
    }
    const uint8_t header[]={0,255,255,255,255,255,255,0};memcpy(edid,header,8);
    edid[8]=0x5C;edid[9]=0xB8; // synthetic WEX identity, no panel serial
    edid[10]=0x10;edid[11]=0xD5;
    // EDID 1.4 model year of this synthetic descriptor, not panel manufacture.
    // A zero year selects Apple's pre-2007 non-linear-only backlight path.
    edid[16]=0xFF;edid[17]=2026-1990;
    edid[18]=1;edid[19]=4;edid[20]=0xA0; // digital RGB8, transport unspecified
    edid[21]=static_cast<uint8_t>((m.widthMM+5)/10);
    edid[22]=static_cast<uint8_t>((m.heightMM+5)/10);
    edid[23]=0xFF;edid[24]=extended?0:2; // preferred timing lives in DTD or DisplayID
    for(unsigned i=38;i<54;++i)edid[i]=1; // no standard/established timings
    auto d=edid+54;
    if(extended) {
        d[3]=0x10; // unused base descriptor; exact timing is in extension 1
        edid[126]=1;
    } else {
        d[0]=clock;d[1]=clock>>8;
        d[2]=m.width;d[3]=m.hblank;d[4]=((m.width>>8)<<4)|(m.hblank>>8);
        d[5]=m.height;d[6]=m.vblank;d[7]=((m.height>>8)<<4)|(m.vblank>>8);
        d[8]=m.hfront;d[9]=m.hsync;d[10]=((m.vfront&15)<<4)|(m.vsync&15);
        d[11]=((m.hfront>>8)<<6)|((m.hsync>>8)<<4)|((m.vfront>>4)<<2)|(m.vsync>>4);
        d[12]=m.widthMM;d[13]=m.heightMM;d[14]=((m.widthMM>>8)<<4)|(m.heightMM>>8);
        d[17]=0x18; // separate negative sync, matching the fixed Apple timing
    }
    edid[75]=0xFC;memcpy(edid+77,"MIPI DSI\n    ",13);
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
