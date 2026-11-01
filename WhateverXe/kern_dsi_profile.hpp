// Independently implemented VBT wire decoder. Layout reference: Intel's
// intel_vbt_defs.h/intel_bios.c (Linux v6.12), intel_dsi_vbt.c (Linux v7.0).
// This is a description, not permission to program a panel. Borrowed spans
// require immutable backing storage; the Apple adapter owns that storage.
#ifndef KERN_DSI_PROFILE_HPP
#define KERN_DSI_PROFILE_HPP
#include "kern_dsi_sequence.hpp"
#include <string.h>
namespace XELP { namespace DSI {
inline uint16_t profileU16(const uint8_t *p) {return uint16_t(uint16_t(p[0]) | uint16_t(p[1])<<8);}
enum class ProfileStatus : uint32_t {Bounds, Version, Duplicate, Missing, Child, Config, Timing, Backlight, Sequence, Valid};
struct Mode {
    uint32_t clockKHz {};
    uint16_t width {},hblank {},hfront {},hsync {},height {},vblank {},vfront {},vsync {},widthMM {},heightMM {};
    bool hsyncPositive {},vsyncPositive {};
    uint32_t htotal() const {return uint32_t(width)+hblank;}
    uint32_t vtotal() const {return uint32_t(height)+vblank;}
    bool valid() const {
        return clockKHz && width && height && hsync && vsync &&
            uint32_t(hfront)+hsync<hblank && uint32_t(vfront)+vsync<vblank &&
            htotal()<=65536 && vtotal()<=65536;
    }
};
struct Profile {
    ProfileStatus status {ProfileStatus::Bounds};
    uint16_t version {}; uint8_t checksum {},panel {};
    bool bdbTailQuirk {}; // explicit, caller-approved one-byte final END quirk
    Mode mode;
    uint8_t orientationQuarterTurns {},lanes {},byteClock {},clockPrepare {},clockPre {},hsPrepare {},hsExit {};
    uint16_t clockPrepareZero {},hsPrepareZero {};
    bool btaDisabled {};
    uint32_t hsTxTimeout {},lpRxTimeout {},turnaroundTimeout {},initTimer {};
    uint32_t ppsUs[5] {},pwmHz {};
    uint16_t brightness {},minimum {};
    uint8_t precision {},controller {}; bool activeLowPWM {};
    Sequences sequences;
    bool valid() const {return status==ProfileStatus::Valid;}
};
// Strict initial transport subset: single DSI A, RGB888, video sync events,
// continuous clock, LP VC0 commands and native reset/AVDD/backlight GPIOs.
// Other panels require a reviewed profile policy, never guessed coercion.
inline Profile parseProfile(Span input,bool allowFinalEndQuirk=false) {
    Profile failure,r;
    if(!input.data || input.size<48)return failure;
    auto p=input.data;
    if(memcmp(p,"$VBT",4))return failure;
    const size_t vh=profileU16(p+22),vs=profileU16(p+24),bo=sequenceU32(p+28);
    if(vh<48 || vs<vh || vs>input.size || bo<vh || bo>vs || vs-bo<22)return failure;
    const auto b=p+bo;const size_t bh=profileU16(b+18),bs=profileU16(b+20);
    if(memcmp(b,"BIOS_DATA_BLOCK ",16) || bh<22 || bs<bh || bs>vs-bo)return failure;
    r.version=profileU16(b+16);
    // The consumed generic DTD, MIPI and 8-bit backlight fields share
    // this layout from BDB 236 onward. Validate known prefixes and declared
    // record strides rather than rejecting later BIOS versions by number.
    if(r.version<236){failure.status=ProfileStatus::Version;return failure;}
    for(size_t i=0;i<vs;++i)r.checksum+=p[i]; // diagnostic residue, like existing VBT parser
    Span children,options,config,seq,dtd,backlight;
    for(size_t at=bh;at<bs;) {
        if(bs-at<3)return failure;
        const auto id=b[at];size_t n=profileU16(b+at+1);
        if(id==53) {
            if(bs-at<8)return failure;
            if(b[at+3]!=3){failure.status=ProfileStatus::Version;return failure;}
            n=size_t(sequenceU32(b+at+4))+5;
        }
        const size_t available=bs-at-3;
        if(n>available) {
            // Observed BDB256 tail excludes exactly the final panel END.
            // Require the caller's policy, final block 53, and that byte to
            // be inside the *declared VBT*, not merely the mapped page.
            if(!allowFinalEndQuirk || id!=53 || n!=available+1 ||
               vs-bo!=bs+1 || b[bs]!=0)return failure;
            r.bdbTailQuirk=true;
        }
        Span *slot=nullptr;
        switch(id) {
            case 2:slot=&children;break;case 40:slot=&options;break;
            case 43:slot=&backlight;break;case 52:slot=&config;break;
            case 53:slot=&seq;break;case 58:slot=&dtd;break;
        }
        if(slot) {
            if(slot->data){failure.status=ProfileStatus::Duplicate;return failure;}
            *slot={b+at+3,n};
        }
        at+=3+n;
    }
    failure.status=ProfileStatus::Missing;
    if(!children.data || !options.data || !config.data || !dtd.data || !backlight.data)return failure;
    failure.status=ProfileStatus::Child;
    // Intel's legacy child prefix is 33 bytes; later tails are optional here.
    if(children.size<5 || children.data[4]<33 ||
       (children.size-5)%children.data[4])return failure;
    unsigned mipi=0;
    for(size_t at=5;at<children.size;at+=children.data[4]) {
        const auto c=children.data+at;const auto type=profileU16(c+2);
        if(!(type&0x400))continue;
        if(++mipi!=1 || type!=0x1400 || c[16]!=21)return failure;
    }
    if(mipi!=1)return failure;
    failure.status=ProfileStatus::Config;
    if(options.size<1 || options.data[0]>=6 || config.size<792)return failure;
    r.panel=options.data[0];const auto c=config.data+122*r.panel;
    const auto general=sequenceU32(c+2);const auto port=profileU16(c+6);
    // Bridge, command mode, dual link, reversal and alternate DCS
    // destinations are deliberately outside the first implementation.
    if((general&4) || ((general>>3)&3)!=2 || (general&32) ||
       ((general>>6)&3)!=2 || ((general>>10)&15)!=4 ||
       (port&0xFFF3) || (profileU16(c+24)&7) || c[23]!=0)return failure;
    // VBT rotation describes panel mounting, not the DSI wire protocol.
    // Keep the metadata without synthesizing or restricting OS transforms.
    r.orientationQuarterTurns=(general>>14)&3;
    r.lanes=((port>>2)&3)+1;if(r.lanes!=4)return failure;
    r.byteClock=c[23];r.btaDisabled=(general>>16)&1;
    r.hsTxTimeout=sequenceU32(c+26);r.lpRxTimeout=sequenceU32(c+30);
    r.turnaroundTimeout=sequenceU32(c+34);r.initTimer=sequenceU32(c+42);
    r.clockPre=c[93];r.clockPrepare=c[94];r.clockPrepareZero=profileU16(c+98);
    r.hsExit=c[103];r.hsPrepare=c[104];r.hsPrepareZero=profileU16(c+105);
    if(r.clockPrepareZero<r.clockPrepare || r.hsPrepareZero<r.hsPrepare)return failure;
    for(unsigned i=0;i<5;++i)r.ppsUs[i]=uint32_t(profileU16(config.data+732+10*r.panel+2*i))*100;
    failure.status=ProfileStatus::Timing;
    if(dtd.size<2)return failure;
    const size_t stride=profileU16(dtd.data);
    if(stride<28 || (dtd.size-2)%stride || (dtd.size-2)/stride<=r.panel)return failure;
    const auto t=dtd.data+2+stride*r.panel;auto &m=r.mode;
    m.clockKHz=sequenceU32(t);m.width=profileU16(t+4);m.hblank=profileU16(t+6);
    m.hfront=profileU16(t+8);m.hsync=profileU16(t+10);m.height=profileU16(t+12);
    m.vblank=profileU16(t+14);m.vfront=profileU16(t+16);m.vsync=profileU16(t+18);
    m.widthMM=profileU16(t+20);m.heightMM=profileU16(t+22);
    m.hsyncPositive=t[24]&0x80;m.vsyncPositive=t[24]&0x40;
    if(!m.valid())return failure;
    failure.status=ProfileStatus::Backlight;
    if(backlight.size<273 || backlight.data[0]!=6)return failure;
    const auto bl=backlight.data+1+6*r.panel;
    if((bl[0]&3)!=2 || backlight.data[113+r.panel]!=2)return failure;
    r.activeLowPWM=bl[0]&4;r.pwmHz=profileU16(bl+1);r.controller=0;
    r.brightness=profileU16(backlight.data+129+4*r.panel);
    r.minimum=profileU16(backlight.data+193+4*r.panel);r.precision=backlight.data[257+r.panel];
    if(!r.pwmHz || r.precision!=8 || r.brightness>255 || r.minimum>r.brightness)return failure;
    failure.status=ProfileStatus::Sequence;
    // An absent block specifies no panel commands; a present malformed block
    // must not be treated as absence. Host/PLL/PPS sequencing still executes.
    if(seq.data)r.sequences=parseSequences(seq,r.panel);
    else r.sequences.status=SequenceStatus::Valid;
    if(!r.sequences.valid())return failure;
    r.status=ProfileStatus::Valid;return r;
}
}}
#endif
