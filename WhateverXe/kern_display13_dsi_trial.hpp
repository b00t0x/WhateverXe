// Opt-in bring-up experiment, not a supported DSI backend or release policy.
// Transfer a qualified DSI-A firmware scanout to Combo-B TMDS, then hand the
// real hardware state to the existing Display13 donor admission. The fixed
// CEA-16 mode is a test fixture; remove this path when native DSI/cold startup
// owns the lifecycle. Never turn a failed source certificate into success.
// Register sequencing: Intel Linux v6.12 icl_dsi.c, intel_ddi.c,
// intel_dpll_mgr.c and intel_ddi_buf_trans.c (MIT).
#ifndef KERN_DISPLAY13_DSI_TRIAL_HPP
#define KERN_DISPLAY13_DSI_TRIAL_HPP
#include "kern_alderlake_n.hpp"
#include "kern_display13_clock.hpp"
#include "kern_display13_wm.hpp"
#include "kern_xelp_combo_clock.hpp"
namespace Display13DSITrial {
enum class Status:uint32_t {NotRequested,Source,Memory,Clock,Latency,PHY,Timeout,Readback,Complete};
struct Result {
    uint32_t version=1;Status status=Status::NotRequested;
    uint32_t stage=0,writes=0,reg=0,value=0,pll=UINT32_MAX,sourcePLL=UINT32_MAX,frames=0;
};
template<class IO> Result transfer(IO &io,bool requested,const AlderLakeN::DisplayReadout &boot,
                                   const XELPMemory::Layout &memory,unsigned hdmiMask,
                                   Display13Clock::Variant variant=Display13Clock::Variant::ADLP) {
    Result r;
    if(!requested)return r;
    r.status=Status::Source;
    if(hdmiMask!=2 || boot.status!=AlderLakeN::ReadoutStatus::Complete || boot.activePipes ||
       !(boot.poweredPipes&1) || !memory.valid)return r;
    const auto read=[&](uint32_t reg){return io.readable(reg)?io.read(reg):UINT32_MAX;};
    const auto get=[&](uint32_t reg){uint32_t v=UINT32_MAX;AlderLakeN::value(boot,reg,v);return v;};
    const auto conf=read(0x7B008),func=read(0x6B400),plane=get(0x70180),surface=get(0x7019C);
    const auto geometry=XELPGeometry::linear(plane,get(0x70188),get(0x70190),get(0x7018C),get(0x701A4));
    const auto power=get(0x45404),ddiPower=get(0x45454);
    if(conf!=0xC0000000 || !(func&0x80000000U) || (func&0x7000) || read(0x6B404) ||
       read(0x7B808)&0xC0000000U || read(0x6B094)!=1 || read(0x6B894) ||
       (read(0x6B030)&0x303F0000U)!=0x20130000U || read(0x6B034) ||
       get(0x64000)!=0x80000000U || (get(0x64100)&0x80000080U)!=0x80 ||
       (power&0x405)!=0x405 || (ddiPower&3)!=3 ||
       !geometry.valid || geometry.width!=1080 || geometry.height!=1920 ||
       surface!=get(0x701AC) || (surface&4095) || !memory.range(surface,geometry.bytes,true))return r;
    for(unsigned p=0;p<3;++p)if(boot.poweredPipes&(1U<<p)) {
        const auto bank=p*0x1000;
        if(get(0x70008+bank)&0xC0000000U || get(0x70080+bank)&63)return r;
        for(unsigned n=p?0:1;n<7;++n)if(get(0x70180+bank+n*0x100)&0x80000000U)return r;
        for(unsigned d=0;d<3;++d)if(read(Display13FBIRQ::DSBControl(p,d))&0x80000001U)return r;
    }
    for(unsigned s=0;s<2;++s)if(get(0x68180+s*0x100)&0x80000000U)return r;
    r.status=Status::Memory;
    if(!memory.range(surface,1920ULL*1080*4,true) || read(0x7027C)!=0x07FF0000U)return r;
    for(unsigned i=0;i<memory.bytes/4096;++i)
        if(io.readPTE(0x800000+8*i)!=memory.pte(uint64_t(i)*4096))return r;
    r.status=Status::Clock;
    const auto cdclk=Display13Clock::read(io,variant);
    if(cdclk.status!=Display13Clock::Status::Complete || cdclk.refKHz!=38400 || cdclk.cdclkKHz<148500)return r;
    const auto mux=read(0x164280);r.sourcePLL=mux&3;
    if(r.sourcePLL>=2 || (mux&(1U<<10)))return r;
    r.pll=r.sourcePLL^1;
    XELPComboClock::PLL target,source;
    if(!XELPComboClock::PLL::resolve(r.pll,target) || !XELPComboClock::PLL::resolve(r.sourcePLL,source) ||
       read(target.enable)&0xCC000000U || (read(source.enable)&0xCC000000U)!=0xCC000000U)return r;
    r.status=Status::PHY;
    if(!(read(0x6C100)&0x80000000U))return r; // firmware initialized Combo-B compensation
    const uint32_t phyRegs[]={0x6C804,0x6C014,0x6C894,0x6C028};
    for(auto reg:phyRegs)if(read(reg)==UINT32_MAX)return r;
    const uint32_t laneWords[]={0x888,0x890,0x89C};
    for(unsigned n=0;n<4;++n)for(auto off:laneWords)if(read(0x6C000+0x100*n+off)==UINT32_MAX)return r;
    // Early bootstrap is before WEX's/native PCODE clients exist. Refuse an
    // already-busy mailbox; use the shared actual-latency calculation.
    r.status=Status::Latency;
    const auto latency=Display13WM::queryLatency(io);
    const auto wm=Display13WM::plan(1920,2200,148500000,2048,latency,false,false,1080);
    if(wm.status!=Display13WM::Status::Complete || !wm.sagv.enable)return r;
    if(read(0x7B008)!=conf || read(0x6B400)!=func || read(0x70180)!=plane ||
       read(0x7019C)!=surface || read(0x45404)!=power || read(0x164280)!=mux)return r;
    const auto write=[&](uint32_t reg,uint32_t value){io.write(reg,value);++r.writes;r.reg=reg;r.value=read(reg);};
    const auto wait=[&](uint32_t reg,uint32_t mask,uint32_t expected,unsigned us) {
        for(unsigned elapsed=0;elapsed<=us;elapsed+=10) {
            r.reg=reg;r.value=read(reg);
            if(r.value!=UINT32_MAX && (r.value&mask)==expected)return true;
            io.delayUs(10);
        }
        r.status=Status::Timeout;return false;
    };
    const auto frames=[&]() {
        const auto first=read(0x70040);
        for(unsigned elapsed=0;elapsed<100000;elapsed+=100) {
            r.frames=read(0x70040)-first;
            if(r.frames>=2 && r.frames<0x10000000)return true;
            io.delayUs(100);
        }
        r.status=Status::Timeout;r.reg=0x70040;return false;
    };
    // 148.5 MHz * 5 * 12 = 8.91 GHz. P=2,Q=3,K=2. 38.4MHz
    // reference uses 19.2MHz arithmetic and WA_22010492432 halves fraction.
    // Firmware may leave B's mux ungated even though its buffer is idle and
    // no ordinary transcoder is active. Gate that qualified unused consumer
    // before powering/programming the free PLL; A's DSI clock stays intact.
    r.stage=1;write(0x164280,mux|(1U<<11));
    if(read(0x164280)!=(mux|(1U<<11))){r.status=Status::Readback;return r;}
    write(0x45454,ddiPower|8);
    if(!wait(0x45454,12,12,1000))return r;
    write(target.enable,0x08000000);
    if(!wait(target.enable,0x0C000000,0x0C000000,1000))return r;
    write(target.cfg0,0x001001D0);write(target.cfg1,0x00000E84);
    write(target.enable,read(target.enable)|0x80000000U);
    if(!wait(target.enable,0xCC000000,0xCC000000,1000))return r;
    r.stage=2;
    write(0x164280,(mux&~(3U<<2))|(r.pll<<2)); // B stays gated while changing source
    write(0x164280,read(0x164280)&~(1U<<11));
    write(0x6C604,read(0x6C804)&~(1U<<26)); // HDMI common keeper off
    for(unsigned n=0;n<4;++n)write(0x6C890+0x100*n,(read(0x6C890+0x100*n)&~0x80000000U)|(n?0x80000000U:0));
    write(0x6C014,read(0x6C014)|3);
    auto tx=read(0x6C894)&~0x80000000U;write(0x6C694,tx);
    tx=(tx&~0x601C0038U)|0x20080030U;write(0x6C694,tx);
    // Linux default HDMI table entry: swing6, scalar127, cursor53, post1=0, post2=10.
    for(unsigned n=0;n<4;++n) {
        const auto b=0x6C880+0x100*n;
        write(b+8,(read(b+8)&~0xB8FFU)|0x3098);
        write(b+16,(read(b+16)&~0x3FFFFU)|(10U<<6)|53U);
        write(b+28,(read(b+28)&~0x7F000000U)|0x7F000000U);
    }
    write(0x6C694,tx|0x80000000U);write(0x6C028,read(0x6C028)&~0xF0U);
    // Last non-destructive check before retiring the firmware surface.
    if(read(0x7B008)!=conf || read(0x6B400)!=func || !frames())return r;
    r.stage=3;
    write(0xC8250,read(0xC8250)&~0x80000000U);
    write(0x70180,plane&~0x80000000U);write(0x7019C,surface);
    if(!frames())return r;
    write(0x7B008,conf&~0x80000000U);
    if(!wait(0x7B008,0xC0000000,0,50000))return r;
    write(0x6B0D8,(read(0x6B0D8)&~(1U<<8))|1);
    if(!wait(0x6B0D8,1U<<16,1U<<16,1000))return r;
    write(0x6B400,func&~0x80000000U);
    write(0x64000,0);
    if(!wait(0x64000,0x80000080,0x80,1000))return r;
    write(0x164280,read(0x164280)|(1U<<10));
    write(source.enable,read(source.enable)&~0x80000000U);
    if(!wait(source.enable,1U<<30,0,1000))return r;
    write(source.enable,read(source.enable)&~(1U<<27));
    if(!wait(source.enable,1U<<26,0,1000))return r;
    r.stage=4;
    write(0x46140,0x20000000); // pipe A clock from actual Combo B
    const uint32_t timing[][2]={{0x60000,0x0897077F},{0x60004,0x0897077F},{0x60008,0x080307D7},
        {0x6000C,0x04640437},{0x60010,0x04640437},{0x60014,0x0440043B},{0x60028,0},
        {0x6001C,0x077F0437}};
    for(auto &v:timing)write(v[0],v[1]);
    write(0x42084,read(0x42084)|(1U<<30));write(0x70028,read(0x70028)|(1U<<13));
    write(0x70188,120);write(0x7018C,0);write(0x70190,0x0437077F);write(0x701A4,0);
    write(0x45270,Display13WM::pipeLineTime(2200,148500000));
    const auto programmed=Display13WM::program(io,wm,false,false,0);
    r.writes+=programmed.writes;
    if(programmed.status!=Display13WM::Status::Complete){r.status=Status::Readback;return r;}
    r.stage=5;
    write(0x60404,0);write(0x60400,0x90030000); // B HDMI, 8bpc, +H/+V
    write(0x70008,0x80000000);
    if(!wait(0x70008,0xC0000000,0xC0000000,50000))return r;
    write(0x64100,0x80000000);
    if(!wait(0x64100,0x80000080,0x80000000,3000))return r;
    write(0x70180,(plane&~(7U<<28))|(1U<<28));write(0x7019C,surface);
    if(!frames())return r;
    r.stage=6;r.status=Status::Complete;return r;
}
}
#endif
