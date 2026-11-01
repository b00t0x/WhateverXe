// Native display GPIOs used by Intel VBT DSI sequences (intel_dsi_vbt.c v7.0).
// GPIO role is a display function, not an OS gpiochip number. The adapter must
// hold the display IRQ lock across RESET's HPD control read/modify/write only.
#ifndef KERN_DSI_PANEL_HPP
#define KERN_DSI_PANEL_HPP
#include "kern_dsi_commands.hpp"
#include "kern_dsi_surface.hpp"
#include "kern_dsi_backlight.hpp"
namespace XELP { namespace DSI { namespace Panel {
struct GPIOResult {uint32_t role {},value {},reg {},before {},after {},writes {};bool complete {};};
template<class IO> GPIOResult gpio(IO &io,unsigned role,unsigned value) {
    GPIOResult r;r.role=role;r.value=value;
    if(!nativePanelGPIO(role) || value>1 || !io.ready())return r;
    const unsigned bank=role/5,function=role%5;
    r.reg=function==0?0xC4030:0xC7204+0x100*bank;
    if(!io.readable(r.reg))return r;
    r.before=io.read(r.reg);if(r.before==UINT32_MAX)return r;
    const uint32_t mask=function==0?(12U<<(4*bank)):function==1?1U:4U;
    const uint32_t bits=value?(function==0?(4U<<(4*bank)):function==1?1U:4U):0;
    // HPD status is W1C. Reset must not acknowledge another port's event.
    const auto next=(r.before&~(mask|(function==0?0x3333U:0U)))|bits;
    if(!io.ready())return r;
    io.write(r.reg,next);++r.writes;
    r.after=io.read(r.reg);
    r.complete=r.after!=UINT32_MAX && (r.after&mask)==bits && io.ready();return r;
}
enum class Stage : uint32_t {
    Input,BacklightOff,PlaneOff,TranscoderOff,DisplayOff,AssertReset,PowerOff,
    PowerCycle,PowerOn,DeassertReset,Init,TranscoderOn,PlaneOn,BacklightOn,Complete,HostOff,HostOn
};
enum class Error : uint32_t {None,Input,Ownership,Readback,Timeout,Sequence,Host};
struct Result {
    Stage stage {Stage::Input};Error error {Error::Input};
    uint32_t reg {},value {},writes {},waitUs {},frameBefore {},frameAfter {};
    Host host {};Readout source;bool attempted {};
    bool complete() const {return stage==Stage::Complete && error==Error::None;}
};
// LP commands submitted during video can wait for a blanking opportunity.
// Bound each credit wait by two actual frame periods, not the stopped host's
// 100 us allowance. Keep completion/drain checks before delays and GPIOs.
inline unsigned videoCreditBudgetUs(const Mode &m) {
    if(!m.valid())return 0;
    const uint64_t period=(uint64_t(m.htotal())*m.vtotal()*1000+m.clockKHz-1)/m.clockKHz;
    if(period<1000 || period>100000)return 0;
    return unsigned((2*period+9)/10*10);
}
// Panel half of a serialized runtime shutdown. Host/IRQ/clock/memory remain
// owned by the caller. Reset/power-off must contain no packets after ULPS.
struct Shutdown {
    Host host {true};unsigned stage {};bool complete {};
    bool preflight(const Profile &p) {
        if(!p.valid() || !videoCreditBudgetUs(p.mode))return false;
        constexpr unsigned ids[]={7,4,5,11};
        for(auto id:ids) {
            if(absentSequence(p.sequences.sequence[id]))continue;
            Sequences n;
            if(validateSequence(p.sequences.sequence[id],n)!=SequenceStatus::Valid ||
               n.delayUs>5000000 || ((id==5 || id==11) && n.packets))return false;
        }
        return p.ppsUs[2]<=5000000 && p.ppsUs[3]<=5000000;
    }
    template<class IO> bool step(IO &io,const Profile &p,unsigned next) {
        if(next!=stage+1 || complete)return false;
        stage=next;
        struct Port:CommandPort<IO> {
            explicit Port(IO &q):CommandPort<IO>{q} {}
            bool gpio(unsigned role,unsigned value){return this->io.gpio(role,value);}
        } port{io};
        if(next==1)return host.run(port,p.sequences,7,videoCreditBudgetUs(p.mode)) && host.delay(port,p.ppsUs[2]);
        if(next==2)return host.run(port,p.sequences,4);
        if(next==3) {
            complete=host.run(port,p.sequences,5) && host.delay(port,p.ppsUs[3]) &&
                host.run(port,p.sequences,11) && !host.pending && !port.failed;
            return complete;
        }
        return false;
    }
};
// Bounded bootstrap panel experiment. Keep existing scanout backing pinned.
// Stop pixel transfer, execute the complete panel off/reset/on sequences, and
// restart the same scanout. Lifecycle may stop/rebuild the host between panel
// operations; RetainedHost leaves its settings intact. Neither variant is a
// complete OS DPMS implementation or releases allocation/shared power ownership.
// A verified Shutdown permits the same power-on/init tail on runtime resume;
// native backing, cursor and IRQ lifetime still belong to the owning adapter.
// IO::gpio serializes HPD writes; no delays may run under that IRQ lock.
// A failed transaction is never retried and must retain all resources.
template<class IO,class Lifecycle> void cycle(IO &io,const Readout &link,const Profile &profile,
                             uint32_t planeControl,uint32_t surface,Result &r,Lifecycle &lifecycle,const Shutdown *shutdown=nullptr,Backlight *backlight=nullptr) {
    Readout current=link;
    if(r.attempted)return;
    r.attempted=true;
    if((shutdown && (!shutdown->complete || shutdown->host.faulted || shutdown->host.pending)) ||
       !profile.valid() || !link.matches(profile) || link.identity.pipe!=0 ||
       !(planeControl&(1U<<31)) || (surface&4095) || !profile.sequences.valid())return;
    constexpr unsigned lifecycleIds[]={1,2,3,4,5,6,7,10,11};
    for(auto id:lifecycleIds) {
        if(absentSequence(profile.sequences.sequence[id]))continue;
        Sequences count;
        if(validateSequence(profile.sequences.sequence[id],count)!=SequenceStatus::Valid || count.delayUs>5000000)return;
    }
    for(auto us:profile.ppsUs)if(us>5000000)return;
    const uint64_t period=(uint64_t(profile.mode.htotal())*profile.mode.vtotal()*1000+profile.mode.clockKHz-1)/profile.mode.clockKHz;
    if(period<1000 || period>100000)return;
    const auto held=[&]() {if(!io.ready()){r.error=Error::Ownership;return false;}return true;};
    const auto read=[&](uint32_t reg,uint32_t &v) {
        r.reg=reg;if(!held())return false;
        if(!io.readable(reg)){r.error=Error::Readback;return false;}
        v=io.read(reg);r.value=v;if(v==UINT32_MAX){r.error=Error::Readback;return false;}return true;
    };
    const auto write=[&](uint32_t reg,uint32_t v) {
        if(!held() || !io.readable(reg)){r.error=Error::Ownership;return false;}
        io.write(reg,v);++r.writes;return true;
    };
    const auto wait=[&](uint32_t reg,uint32_t mask,uint32_t value,uint32_t budget) {
        for(uint32_t us=0;;us+=1000) {
            uint32_t observed=0;if(!read(reg,observed))return false;
            if((observed&mask)==value)return true;
            if(us>=budget){r.error=Error::Timeout;return false;}
            io.delayUs(1000);r.waitUs+=1000;
        }
    };
    const auto frame=[&]() {
        if(!read(0x70040,r.frameBefore))return false;
        for(uint32_t us=0;;us+=1000) {
            if(!read(0x70040,r.frameAfter))return false;
            const auto n=uint32_t(r.frameAfter-r.frameBefore);
            if(n && n<0x80000000U)return true;
            if(n || us>=3*period+1000){r.error=Error::Timeout;return false;}
            io.delayUs(1000);r.waitUs+=1000;
        }
    };
    struct Port : CommandPort<IO> {
        explicit Port(IO &q):CommandPort<IO>{q} {}
        bool gpio(unsigned role,unsigned value){return this->io.gpio(role,value);}
    } port{io};
    const auto sequence=[&](unsigned id) {
        const bool video=r.stage==Stage::BacklightOff || r.stage==Stage::TranscoderOn || r.stage==Stage::BacklightOn;
        const auto budget=video?videoCreditBudgetUs(profile.mode):100;
        if(!r.host.run(port,profile.sequences,id,budget)){r.error=Error::Sequence;return false;}return true;
    };
    const auto delay=[&](unsigned index) {
        if(!r.host.delay(port,profile.ppsUs[index])){r.error=Error::Sequence;return false;}return true;
    };
    r.error=Error::Readback;
    uint32_t credits=0,lp=0;
    if(shutdown) {
        // The adapter retains the native backing and all display leases.
        // Restart::start separately proves the stopped PLL/host and its owner.
        if(!held() || !wait(0x7B008,0xC0000000U,0,0) || !wait(0x70180,1U<<31,0,0) ||
           !wait(0x70080,63,0,0))return;
        r.host=shutdown->host;
    } else {
        if(!held() || !Surface::source(io,link) ||
           !Surface::word(io,0x70180,UINT32_MAX,planeControl) ||
           !Surface::word(io,0x7019C,UINT32_MAX,surface) ||
           !read(0x6B0D0,credits) || !read(0x6B0D8,lp) ||
           (credits&0x1FFF)!=0x1040 || (lp&0x30001))return;
        r.host.enabled=true;
        r.stage=Stage::BacklightOff;if(!sequence(7) || !delay(2))return;
        r.stage=Stage::PlaneOff;
        if(!write(0x70180,planeControl&~(1U<<31)) || !write(0x7019C,surface) ||
           !wait(0x70180,1U<<31,0,0) || !frame())return;
        r.stage=Stage::TranscoderOff;
        if(!write(0x7B008,link.conf&~(1U<<31)) || !wait(0x7B008,0xC0000000U,0,50000))return;
        r.stage=Stage::DisplayOff;if(!sequence(4))return;
        r.stage=Stage::HostOff;if(!lifecycle.stop(io,link,profile)){r.error=Error::Host;return;}
        r.stage=Stage::AssertReset;if(!sequence(5) || !delay(3))return;
        r.stage=Stage::PowerOff;if(!sequence(11))return;
    }
    r.stage=Stage::PowerCycle;if(!delay(4))return;
    r.stage=Stage::PowerOn;if(!sequence(10) || !delay(0))return;
    r.stage=Stage::DeassertReset;if(!sequence(1))return;
    r.stage=Stage::HostOn;if(!lifecycle.start(io,current)){r.error=Error::Host;return;}
    r.stage=Stage::Init;if(!sequence(2))return;
    // Sequences drain before returning; never enable video over queued data.
    if(port.failed || r.host.pending || !held())return;
    r.stage=Stage::TranscoderOn;
    if(!write(0x7B008,link.conf) || !wait(0x7B008,0xC0000000U,0xC0000000U,10000))return;
    if(!sequence(3))return;
    r.stage=Stage::PlaneOn;
    if(!write(0x70180,planeControl) || !write(0x7019C,surface) ||
       !wait(0x70180,UINT32_MAX,planeControl,0) || !frame() ||
       !wait(0x701AC,UINT32_MAX,surface,0))return;
    r.stage=Stage::BacklightOn;
    // Restore a deferred brightness before enabling the panel's backlight GPIO.
    if(backlight && !backlight->apply(io)){r.error=Error::Readback;return;}
    if(!delay(1) || !sequence(6))return;
    if(!held() || !Surface::source(io,current) || port.failed || r.host.pending ||
       !read(0x6B0D0,credits) || !read(0x6B0D8,lp) ||
       (credits&0x1FFF)!=0x1040 || (lp&0x30001))return;
    r.source=current;r.stage=Stage::Complete;r.error=Error::None;
}
struct RetainedHost {
    template<class IO> bool stop(IO &,const Readout &,const Profile &){return true;}
    template<class IO> bool start(IO &,Readout &){return true;}
};
template<class IO> void cycle(IO &io,const Readout &link,const Profile &profile,
                             uint32_t planeControl,uint32_t surface,Result &r) {
    RetainedHost retained;cycle(io,link,profile,planeControl,surface,r,retained);
}
}}}
#endif
