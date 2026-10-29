// Pure calculations, independently implemented from Intel register formats and
// Linux icl_dsi.c v7.0 / intel_dpll_mgr.c v6.12. No allocation or clock writes.
#ifndef KERN_DSI_MATH_HPP
#define KERN_DSI_MATH_HPP
#include "kern_dsi_profile.hpp"
namespace XELP { namespace DSI {
struct LinkPlan {
    bool valid {};uint32_t afeKHz {},portKHz {},escapeDivider {},phyEscapeDivider {};
    uint32_t clockTiming {},dataTiming {},htotal {},hsync {},vtotal {},vsync {};
};
inline uint32_t cappedCeil(uint32_t numerator,uint32_t denominator,uint32_t cap) {
    const auto n=(numerator+denominator-1)/denominator;return n<cap?n:cap;
}
inline LinkPlan linkPlan(const Profile &p) {
    LinkPlan r;
    if(!p.valid() || p.lanes!=4 || p.byteClock!=0 || !p.mode.clockKHz || p.mode.clockKHz>300000 ||
       p.clockPrepareZero<p.clockPrepare || p.hsPrepareZero<p.hsPrepare)return r;
    const auto &m=p.mode;
    r.afeKHz=m.clockKHz*6;r.portKHz=r.afeKHz/5;
    // ADL-P/N escape word clock divider must be odd, >=3, <=20 MHz.
    auto word=(r.afeKHz+159999)/160000;word|=1;if(word<3)word=3;
    r.escapeDivider=word*8;r.phyEscapeDivider=(word-1)/2;
    constexpr uint32_t tlpx=50; // byteClock=0: 20 MHz escape timing reference
    r.clockTiming=(1U<<31)|(cappedCeil(p.clockPrepare*4,tlpx,7)<<28)|(1U<<27)|
        (cappedCeil(p.clockPrepareZero-p.clockPrepare,tlpx,15)<<20)|(1U<<19)|(cappedCeil(p.clockPre,tlpx,3)<<16);
    r.dataTiming=(1U<<31)|(cappedCeil(p.hsPrepare*4,tlpx,7)<<24)|(1U<<23)|
        (cappedCeil(p.hsPrepareZero-p.hsPrepare,tlpx,15)<<16)|(1U<<7)|cappedCeil(p.hsExit,tlpx,7);
    r.htotal=((m.htotal()-1)<<16)|(m.width-1);
    r.vtotal=((m.vtotal()-1)<<16)|(m.height-1);
    r.hsync=((uint32_t(m.width)+m.hfront+m.hsync-1)<<16)|(uint32_t(m.width)+m.hfront-1);
    r.vsync=((uint32_t(m.height)+m.vfront+m.vsync-1)<<16)|(uint32_t(m.height)+m.vfront-1);
    r.valid=true;return r;
}
struct PLLPlan {
    bool valid {};uint32_t referenceKHz {},dcoKHz {},divider {},p {},q {},k {},integer {},fraction {};
};
inline PLLPlan pllPlan(uint32_t portKHz,uint32_t referenceKHz) {
    PLLPlan r;
    if(!portKHz || portKHz>600000 || (referenceKHz!=19200 && referenceKHz!=24000 && referenceKHz!=38400))return r;
    // Hardware divider choices, in the documented tie-breaking order.
    static const uint8_t dividers[]={2,4,6,8,10,12,14,16,18,20,24,28,30,32,36,40,42,44,48,50,52,54,
        56,60,64,66,68,70,72,76,78,80,84,88,90,92,96,98,100,102,3,5,7,9,15,21};
    uint32_t distance=UINT32_MAX;
    for(auto d:dividers) {
        const uint64_t dco=uint64_t(portKHz)*5*d;
        if(dco<7998000 || dco>10000000)continue;
        const auto diff=uint32_t(dco>8999000?dco-8999000:8999000-dco);
        if(diff<distance){distance=diff;r.divider=d;r.dcoKHz=uint32_t(dco);}
    }
    if(!r.divider)return r;
    const auto d=r.divider;
    if(d%2) {r.p=d<=7?d:d/3;r.q=1;r.k=d<=7?1:3;}
    else if(d==2){r.p=2;r.q=1;r.k=1;}
    else if(d%4==0){r.p=2;r.q=d/4;r.k=2;}
    else if(d%6==0){r.p=3;r.q=d/6;r.k=2;}
    else if(d%10==0){r.p=5;r.q=d/10;r.k=2;}
    else if(d%14==0){r.p=7;r.q=d/14;r.k=2;}
    else return {};
    r.referenceKHz=referenceKHz;
    const auto fixed=(uint64_t(r.dcoKHz)<<15)/(referenceKHz==38400?19200:referenceKHz);
    r.integer=uint32_t(fixed>>15);r.fraction=uint32_t(fixed&0x7FFF);
    // Display WA 22010492432: 38.4 MHz uses half the nominal fraction.
    if(referenceKHz==38400)r.fraction=(r.fraction+1)/2;
    r.valid=true;return r;
}
}}
#endif
