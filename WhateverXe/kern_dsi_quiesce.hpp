// Host shutdown, following Linux v7.0 gen11_dsi_post_disable:
// stop plane/cursor, transcoder, ULPS, function, then ungate/stop/gate buffer.
// Optional panel lifecycle supplies backlight, DISPLAY_OFF and reset/power-off.
#ifndef KERN_DSI_QUIESCE_HPP
#define KERN_DSI_QUIESCE_HPP
#include "kern_dsi_surface.hpp"
#include "kern_display13_cursor.hpp"
namespace XELP { namespace DSI { namespace Quiesce {
enum class Stage : uint32_t {Source,Cursor,Plane,Transcoder,Commands,ULPS,Function,PortClock,Buffer,Gate,Complete,Panel};
enum class Error : uint32_t {None,Input,Ownership,Readback,Timeout,Commands,Cursor,Panel};
struct Result {
    Stage stage {Stage::Source};Error error {Error::Input};uint32_t writes {},waitUs {},reg {},value {};
    uint32_t frameBefore {},frameAfter {};Display13Cursor::Result cursor;
    bool complete() const {return stage==Stage::Complete && error==Error::None;}
};
// Adapter holds FB/mode and shared clock serialization, keeps power/PLL/backing
// pinned and prevents new IRQ/flip producers. Never hold an IRQ spinlock here.
// IO::ready is an ownership check that remains meaningful after TRANSCONF is
// stopped, not sourceUnchanged(). No IRQ queue or allocator handle is invented
// by this core. Completion permits real IRQ drain/clock detach; it is not a
// Session StopProof and does not release anything.
// P2 has no command producer. Require empty hardware command FIFOs and LP idle
// both before shutdown and before ULPS; a pending panel sequence needs the
// later Host/panel drain backend. Never silently discard such a command.
struct RetainedPanel {
    bool preflight(const Profile &){return true;}
    template<class IO> bool step(IO &,const Profile &,unsigned){return true;}
};
template<class IO,class Panel> Result run(IO &io,const Readout &link,const Profile &profile,Panel &panel) {
    Result r;
    if(!link.matches(profile) || link.identity.pipe!=0 || !profile.mode.valid() || !panel.preflight(profile))return r;
    const auto period=(uint64_t(profile.mode.htotal())*profile.mode.vtotal()*1000+profile.mode.clockKHz-1)/profile.mode.clockKHz;
    if(period<1000 || period>100000)return r;
    const auto held=[&]() {
        if(!io.ready() || !io.readable(0x45404))return false;
        const auto wells=io.read(0x45404);
        return wells!=UINT32_MAX && (wells&0x403)==0x403;
    };
    const auto read=[&](uint32_t reg,uint32_t &v) {
        r.reg=reg;if(!held()){r.error=Error::Ownership;return false;}
        if(!io.readable(reg)){r.error=Error::Readback;return false;}
        v=io.read(reg);r.value=v;if(v==UINT32_MAX){r.error=Error::Readback;return false;}return true;
    };
    const auto write=[&](uint32_t reg,uint32_t v) {
        if(!held() || !io.readable(reg)){r.error=Error::Ownership;return false;}
        io.write(reg,v);++r.writes;return true;
    };
    const auto wait=[&](uint32_t reg,uint32_t mask,uint32_t value,uint32_t budget,uint32_t step) {
        for(uint32_t us=0;;us+=step) {
            uint32_t observed=0;if(!read(reg,observed))return false;
            if((observed&mask)==value)return true;
            if(us>=budget){r.error=Error::Timeout;return false;}
            io.delayUs(step);r.waitUs+=step;
        }
    };
    const auto empty=[&]() {
        uint32_t credits=0,lp=0;
        if(!read(0x6B0D0,credits) || !read(0x6B0D8,lp))return false;
        if((credits&0x1FFF)!=0x1040 || (lp&((1U<<17)|(1U<<16)|1U))) {r.error=Error::Commands;return false;}
        return true;
    };
    if(!held()){r.error=Error::Ownership;return r;}
    if(!Surface::source(io,link,true) || !empty())return r;
    const uint32_t registers[]={0x70080,0x70084,0x70088,0x700AC,0x70040,0x70180,0x7019C,
        0x7B008,0x6B0D8,0x6B400,0x164280,0x64000};
    for(auto reg:registers)if(!io.readable(reg))return r;
    r.stage=Stage::Panel;if(!panel.step(io,profile,1)){r.error=Error::Panel;return r;}
    r.stage=Stage::Cursor;
    Display13Transcoder::Bindings bindings;bindings.dsi0Pipe=0;
    r.cursor=Display13Cursor::stop(io,0,bindings);r.writes+=r.cursor.writes;r.waitUs+=r.cursor.waitUs;
    if(r.cursor.status!=Display13Cursor::Status::Complete){r.error=Error::Cursor;return r;}
    r.stage=Stage::Plane;
    uint32_t control=0,surface=0;
    if(!read(0x70180,control) || !read(0x7019C,surface) || (surface&4095))return r;
    if(!write(0x70180,control&~(1U<<31)) || !write(0x7019C,surface) ||
       !wait(0x70180,1U<<31,0,0,1) || !read(0x70040,r.frameBefore))return r;
    // Arm even an already-disabled shadow image and prove a subsequent VBlank
    // while DSI timing is still running. SURF/live equality alone cannot prove
    // that the plane stopped fetching its old allocation.
    for(uint32_t us=0;;us+=1000) {
        if(!read(0x70040,r.frameAfter))return r;
        const auto frames=uint32_t(r.frameAfter-r.frameBefore);
        if(frames && frames<0x80000000U)break;
        if(frames || us>=3*period+1000){r.error=Error::Timeout;return r;}
        if(!held()){r.error=Error::Ownership;return r;}
        io.delayUs(1000);r.waitUs+=1000;
    }
    if(!held() || !Surface::source(io,link) || !wait(0x70180,1U<<31,0,0,1))return r;
    r.stage=Stage::Transcoder;
    if(!write(0x7B008,link.conf&~(1U<<31)) || !wait(0x7B008,0xC0000000U,0,50000,1000))return r;
    r.stage=Stage::Panel;if(!panel.step(io,profile,2)){r.error=Error::Panel;return r;}
    r.stage=Stage::Commands;if(!empty())return r;
    r.stage=Stage::ULPS;uint32_t lp=0;
    if(!read(0x6B0D8,lp) || !write(0x6B0D8,(lp&~(1U<<8))|1) || !wait(0x6B0D8,1U<<16,1U<<16,10,1))return r;
    r.stage=Stage::Function;
    if(!write(0x6B400,link.function&~(1U<<31)) || !wait(0x6B400,1U<<31,0,0,1))return r;
    r.stage=Stage::PortClock;uint32_t mux=0;
    if(!read(0x164280,mux) || (mux&3)!=link.identity.pll ||
       !write(0x164280,mux&~(1U<<10)) || !wait(0x164280,UINT32_MAX,mux&~(1U<<10),0,1))return r;
    r.stage=Stage::Buffer;uint32_t buffer=0;
    if(!read(0x64000,buffer) || !write(0x64000,buffer&~(1U<<31)) || !wait(0x64000,0x80000080U,0x80,8,1))return r;
    r.stage=Stage::Gate;
    if(!read(0x164280,mux) || (mux&3)!=link.identity.pll ||
       !write(0x164280,mux|(1U<<10)) || !wait(0x164280,UINT32_MAX,mux|(1U<<10),0,1))return r;
    r.stage=Stage::Panel;if(!panel.step(io,profile,3)){r.error=Error::Panel;return r;}
    // ULPS was witnessed before function disable; its status may reset when
    // the host is disabled. Do not require a disabled host to retain it.
    // Hardware-only proof. The caller must separately drain actual native IRQ
    // handlers/events and detach the shared clock owner before releasing memory.
    if(!wait(0x70180,1U<<31,0,0,1) || !wait(0x70080,63,0,0,1) ||
       !wait(0x7B008,0xC0000000U,0,0,1) || !wait(0x6B400,1U<<31,0,0,1) ||
       !wait(0x64000,0x80000080U,0x80,0,1))return r;
    r.stage=Stage::Complete;r.error=Error::None;return r;
}
template<class IO> Result run(IO &io,const Readout &link,const Profile &profile) {
    RetainedPanel panel;return run(io,link,profile,panel);
}
}}}
#endif
