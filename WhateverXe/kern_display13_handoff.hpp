// Read-only observations used at the donor's first SST modeset.
#ifndef KERN_DISPLAY13_HANDOFF_HPP
#define KERN_DISPLAY13_HANDOFF_HPP
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_tc_clock.hpp"

namespace Display13Handoff {
using Display13FBMemory::field;
// setupPlane prepares the next scanout before hwRegsNeedUpdate. A queued
// native layout can already differ while PLANE_SURFLIVE still shows the GOP.
inline bool samePlane(const Display13FBMemory::Readout &boot,const void *plane) {
    return plane && field<uint32_t>(plane,0x100)==boot.control &&
        field<uint32_t>(plane,0x118)==boot.stride && field<uint32_t>(plane,0x11c)==boot.size &&
        field<uint32_t>(plane,0x120)==boot.surface;
}
struct Stream {
    uint32_t func {}, timings[6] {}, source {}, linkM {}, linkN {}, dataM {}, dataN {};
    uint32_t transport {}, pll {}, mux {}, symbolKHz {}, pipeMisc {};
    unsigned lanes() const { return ((func>>1)&7)+1; }
};

// i915 icl_ddi_combo_pll_get_freq, including ADL-P's 38.4 MHz fraction WA.
inline unsigned comboRate(unsigned ref,uint32_t cfg0,uint32_t cfg1) {
    if((ref!=19200 && ref!=24000 && ref!=38400) || cfg0==UINT32_MAX || cfg1==UINT32_MAX)return 0;
    const unsigned pdiv[]={0,2,3,0,5,0,0,0,7},kdiv[]={0,1,2,0,3};
    const auto p=(cfg1>>2)&15,k=(cfg1>>6)&7,q=(cfg1&(1U<<9))?(cfg1>>10)&255:1;
    if(p>=9 || k>=5 || !pdiv[p] || !kdiv[k] || !q)return 0;
    const uint64_t fraction=((cfg0>>10)&0x7fff)*(ref==38400?2:1);
    const auto crystal=ref==38400?19200:ref;
    return ((cfg0&1023)*crystal+fraction*crystal/32768)/(pdiv[p]*q*kdiv[k]*5);
}
template<class IO> bool stream(IO &io,AlderLakeN::DisplayReadout &scratch,unsigned ddi,bool panel,unsigned ref,Stream &s) {
    AlderLakeN::readDisplay(io,true,scratch);
    const auto tc=Display13TC::Boot::capture(io,ddi>=3,ddi,0);
    if(!AlderLakeN::dpSource(scratch,0,ddi,ddi>=3?&tc:nullptr,panel))return false;
    if(!AlderLakeN::value(scratch,0x164280,s.mux))return false;
    if(ddi<2) {
        const auto select=(s.mux>>(2*ddi))&3;
        if(select>1 || (s.mux&(1U<<(10+ddi))))return false;
        s.mux=select;
        s.pll=io.read(0x46010+4*select);
        if(s.pll==UINT32_MAX || (s.pll&0xcc000000)!=0xcc000000)return false;
        s.symbolKHz=comboRate(ref,io.read(0x164284+8*select),io.read(0x164288+8*select));
    } else {
        if(s.mux&Display13TC::Clock::gateBit(ddi-3))return false;
        s.mux=tc.clock;
        s.pll=tc.pll;
        const unsigned rates[]={162000,270000,540000,810000,216000,243000,324000,432000};
        s.symbolKHz=rates[(tc.buffer>>20)&7];
    }
    if(!s.symbolKHz)return false;
    s.pipeMisc=io.read(0x70030);
    if(s.pipeMisc==UINT32_MAX || (s.pipeMisc&((1U<<27)|(1U<<11))))return false;
    uint32_t conf=0;
    for(unsigned i=0;i<6;++i)
        if(!AlderLakeN::value(scratch,0x60000+4*i,s.timings[i]))return false;
    return AlderLakeN::value(scratch,0x70008,conf) && (conf&0xc0000000)==0xc0000000 &&
        AlderLakeN::value(scratch,0x60400,s.func) && AlderLakeN::value(scratch,0x60540,s.transport) &&
        AlderLakeN::value(scratch,0x6001c,s.source) && AlderLakeN::value(scratch,0x60040,s.linkM) &&
        AlderLakeN::value(scratch,0x60044,s.linkN) && AlderLakeN::value(scratch,0x60030,s.dataM) &&
        AlderLakeN::value(scratch,0x60034,s.dataN);
}

struct Receiver {
    uint32_t symbolKHz {};
    unsigned lanes {};
    bool enhanced {}, alternateReset {};
};
template<class Read> bool receiver(Read read,unsigned lanes,bool panel,Receiver &sink) {
    uint8_t config[3] {},status[3] {},compression=0;
    if(!read(0x100,config,sizeof(config)) || !read(0x202,status,sizeof(status)) ||
       (config[1]&31)!=lanes || (config[2]&0x2f) || !(status[2]&1))return false;
    for(unsigned i=0;i<lanes;++i)
        if(((status[i/2]>>(4*(i%2)))&7)!=7)return false;
    // DSC_ENABLE is optional. Only query the enable register if supported.
    uint8_t revision=0,cap=0;
    if(!read(0,&revision,1) || revision<0x10 || revision==0xff)return false;
    if(revision>=0x14) {
        if(!read(0x60,&cap,1))return false;
        if((cap&1) && (!read(0x160,&compression,1) || (compression&1)))return false;
    }
    uint8_t panelConfig=0;
    if(panel && !read(0x10a,&panelConfig,1))return false;
    uint32_t rate=config[0]*27000U;
    if(!rate && panel) {
        uint8_t index=0,entry[2] {};
        if(!read(0x115,&index,1) || index>7 || !read(0x10+2*index,entry,2))return false;
        // DPCD table units are 200 kbit/s, or 20 kHz at 10 bits/symbol.
        rate=(uint32_t(entry[1])<<8|entry[0])*20;
    } else if(config[0]!=6 && config[0]!=10 && config[0]!=12 && config[0]!=20 && config[0]!=30)return false;
    if(!rate || rate>810000)return false;
    sink={rate,lanes,bool(config[1]&0x80),bool(panelConfig&1)};
    return true;
}

// Convert the donor's candidate into the same stream on the running link.
// hwRegsNeedUpdate still checks forced modeset, timings, pipe parameters,
// scaler and SDP/color policy. Publish normalized values only on partial mode.
inline bool normalize(const Stream &s,const Receiver &sink,const void *timing,uint32_t *params) {
    if(!timing || !params || !sink.symbolKHz || sink.symbolKHz>810000 || sink.lanes!=s.lanes() ||
       s.symbolKHz+1<sink.symbolKHz || sink.symbolKHz+1<s.symbolKHz ||
       (s.lanes()!=1 && s.lanes()!=2 && s.lanes()!=4) || sink.enhanced!=bool(s.transport&(1U<<18)) ||
       (sink.alternateReset!=bool(s.transport&(1U<<6))) || ((s.func^params[1])&~0x10eU) || memcmp(s.timings,params+4,sizeof(s.timings)) || s.source!=params[10])return false;
    const auto hz=field<uint64_t>(timing,0x28), low=field<uint64_t>(timing,0x30),high=field<uint64_t>(timing,0x38);
    const auto depth=(s.func>>20)&7;
    if(!hz || hz>UINT32_MAX || high>UINT32_MAX || depth>3 || !s.linkN || !s.dataN ||
       (low && low>hz) || (high && high<hz) || field<uint16_t>(timing,0x90))return false;
    const unsigned bpc[]={8,10,6,12}, pipeDepth[]={0,1,2,4}, requestedDepth[]={2,4,1,8};
    const auto encoding=field<uint16_t>(timing,0x88), component=field<uint16_t>(timing,0x8a);
    if((encoding && encoding!=1) || (component && !(component&requestedDepth[depth])) ||
       ((s.pipeMisc>>5)&7)!=pipeDepth[depth])return false;
    const uint64_t symbols=uint64_t(sink.symbolKHz)*1000,capacity=symbols*8*s.lanes();
    const uint64_t clock=symbols*s.linkM/s.linkN;
    const uint64_t pixelFromData=capacity*(s.dataM&0xffffff)/s.dataN/(3*bpc[depth]);
    // Both M/N ratios describe the actual stream. Small denominators must
    // never enlarge the requested timing's clock tolerance.
    const auto matches=[&](uint64_t value) {
        return value+1000 >= (low?low:hz) && value <= (high?high:hz)+1000;
    };
    if(hz*(3*bpc[depth])>capacity || !matches(clock) || !matches(pixelFromData))return false;
    params[1]=s.func;
    params[0x38/4]=s.linkM;params[0x3c/4]=s.linkN;
    params[0x40/4]=s.dataM;params[0x44/4]=s.dataN;
    return true;
}
}
#endif
