// Multi-GOP bootstrap: preserve pipe A, park other GOPs before native
// ownership exists. This does not pretend both GOPs were adopted by the donor.
#ifndef KERN_DISPLAY13_BOOT_HPP
#define KERN_DISPLAY13_BOOT_HPP
#include "kern_alderlake_n.hpp"
#include "kern_dsi_readout.hpp"
#include "kern_xelp_gop_stop.hpp"
#include "kern_display13_fb_irq.hpp"
#include "kern_display13_fb_topology.hpp"
#include "kern_xelp_memory_layout.hpp"
#include "kern_display13_tc_clock.hpp"
namespace Display13Boot {
// Certificate for the linear boot memory. Render qualification is
// separate; callers must also qualify the physical owner, BAR and memory layout.
// DP requires an explicit source certificate; native link adoption remains a
// separate contract. Existing runtime callers retain their TMDS default.
template<class IO> bool reservedPrefix(IO &io,const AlderLakeN::DisplayReadout &display,
                                      bool bootstrapReady,unsigned ddi,const XELPMemory::Layout &memory,
                                      const Display13TC::Boot *tc=nullptr,
                                      AlderLakeN::GOPProtocol protocol=AlderLakeN::GOPProtocol::TMDS) {
    AlderLakeN::LinearPlan plan;
    if(!bootstrapReady || AlderLakeN::linearPlan(display,plan,ddi,protocol,tc)!=AlderLakeN::LinearStatus::Ready ||
       plan.pipe!=0 || !memory.range(plan.surface,AlderLakeN::ScanoutBytes,true))return false;
    const auto stable=[&] {
        return AlderLakeN::bootGeometryUnchanged(io,display,plan.pipe) &&
            (ddi<3 || (tc && tc->unchanged(io))) &&
            (protocol==AlderLakeN::GOPProtocol::TMDS || AlderLakeN::dpSourceUnchanged(io,display,plan.pipe,ddi,0,tc,protocol==AlderLakeN::GOPProtocol::EDP));
    };
    if(!stable())return false;
    for(unsigned i=0;i<memory.bytes/4096;++i)
        if(io.readPTE(0x800000+8*i)!=memory.pte(uint64_t(i)*4096))return false;
    return stable();
}
// Establish the native linear-GGTT prerequisites only after the controller
// startup contract is ready.
// Ownership/power/link drift and the stolen prefix are checked before mutation.
template<class IO> AlderLakeN::LinearResult prepareLinear(IO &io,const AlderLakeN::DisplayReadout &boot,
    bool admitted,unsigned ddi,const XELPMemory::Layout &memory,const Display13TC::Boot *tc=nullptr,
    AlderLakeN::GOPProtocol protocol=AlderLakeN::GOPProtocol::TMDS) {
    using namespace AlderLakeN;
    LinearResult r;
    if(!admitted)return r;
    if(!reservedPrefix(io,boot,true,ddi,memory,tc,protocol)){r.status=LinearStatus::StateChanged;return r;}
    return reinitializeLinear(io,boot,true,ddi,protocol,tc);
}
enum class Status : uint32_t {Unavailable,Complete,Topology,Readback,Timeout,PeerChanged};
struct Result {
    uint32_t version {1};Status status {Status::Unavailable};
    uint32_t stage {},writes {},before {},after {},lastReg {},expected {},observed {},framesBefore {},framesAfter {};
};
// Firmware can leave ownership on a selected direct TC port that has no GOP.
// Linux intel_tc_port_sanitize_mode disconnects unused PHYs. Here admit only
// the already-cold case: disabled/idle buffer, stopped PLL and clock source,
// no transcoder route and no AUX command. Do not stop an unclassified link.
struct UnusedTCResult {
    uint32_t version {1};Status status {Status::Unavailable};
    uint32_t cleared {},writes {},ddi {},buffer {},ready {},clock {},pll {},gate {};
};
template<class IO> UnusedTCResult releaseUnusedTC(IO &io,const AlderLakeN::DisplayReadout &boot,
                                                bool admitted,const Display13HDMIWiring &w,const XELP::DSI::Readout *dsi=nullptr) {
    UnusedTCResult r;
    if(!admitted)return r;
    r.status=Status::Topology;
    if((w.dsi && !dsi) || boot.status!=AlderLakeN::ReadoutStatus::Complete || boot.activePipes!=(dsi?0U:1U) ||
       (dsi && (w.dsi!=1 || dsi->identity.pipe!=0 || !XELP::DSI::sourceUnchanged(io,*dsi))) ||
       !(boot.poweredPipes&1) || !Display13FBTopology::multiOutputWiring(w))return r;
    uint32_t power=0,auxPower=0;
    if(!AlderLakeN::value(boot,0x45404,power) || (power&5)!=5 ||
       !AlderLakeN::value(boot,0x45444,auxPower))return r;
    uint32_t gated=0;
    const auto stable=[&] {
        if(dsi && !XELP::DSI::sourceUnchanged(io,*dsi))return false;
        for(unsigned i=0;i<boot.count;++i) {
            const auto x=boot.registers[i];
            if(x.offset==0x4500C || x.offset==0x70040 || x.offset==0x71040 || x.offset==0x72040)continue;
            if(io.read(x.offset)!=(x.offset==Display13TC::Clock::Gate ? x.value|gated : x.value))return false;
        }
        return true;
    };
    for(unsigned d=3;d<7;++d)if(w.selected()&(1U<<d)) {
        bool routed=false;
        for(unsigned p=0;p<3;++p)if(boot.poweredPipes&(1U<<p)) {
            uint32_t trans=0,clock=0;
            if(!AlderLakeN::value(boot,0x60400+0x1000*p,trans) ||
               !AlderLakeN::value(boot,0x46140+4*p,clock))return r;
            // A selector left on an inactive pipe is also a resource consumer.
            if(((trans>>27)&15)==d+1 || (clock>>28)==d+3)routed=true;
        }
        if(routed)continue;
        Display13TC::Port port;Display13TC::Port::resolve(d,port);
        r.ddi=d;r.buffer=io.read(port.buffer);
        if(r.buffer==UINT32_MAX){r.status=Status::Readback;return r;}
        if(!(r.buffer&64))continue;
        r.ready=io.read(port.ready);r.clock=io.read(port.clock);r.pll=io.read(port.pll);
        r.gate=io.read(Display13TC::Clock::Gate);
        const auto aux=0x64310+0x100*(d-3);
        if((r.buffer&0x800000C0U)!=0xC0 || !(r.ready&4) ||
           !Display13TC::Port::modeMatches(r.ready,(w.typeC&(1U<<d))?3:4) ||
           r.clock==UINT32_MAX || (r.clock&0xF0000000U) || r.pll==UINT32_MAX || (r.pll&0xCC000000U) ||
           r.gate==UINT32_MAX ||
           ((auxPower&(1U<<(2*d))) && (!io.readable(aux) || (io.read(aux)&0x80000000U))))return r;
        if(!stable() || io.read(port.buffer)!=r.buffer || io.read(port.ready)!=r.ready ||
           io.read(port.clock)!=r.clock || io.read(port.pll)!=r.pll) {r.status=Status::PeerChanged;return r;}
        // Some firmware leaves only the unused mux gate open. With no
        // route, clock source, running PLL or buffer, close that gate before
        // returning ownership. Preserve every peer selector/gate bit.
        const auto bit=Display13TC::Clock::gateBit(d-3);
        if(!(r.gate&bit)) {
            io.write(Display13TC::Clock::Gate,r.gate|bit);++r.writes;gated|=bit;
            if(io.read(Display13TC::Clock::Gate)!=(r.gate|bit) || !stable()) {r.status=Status::Readback;return r;}
        }
        io.write(port.buffer,r.buffer&~64U);++r.writes;
        if(io.read(port.buffer)!=(r.buffer&~64U) || !stable() || io.read(port.clock)!=r.clock ||
           io.read(port.pll)!=r.pll) {r.status=Status::Readback;return r;}
        r.cleared|=1U<<d;
    }
    r.status=Status::Complete;return r;
}

// Caller-owned storage keeps snapshots and per-port witnesses off the nested
// kernel bootstrap stack. The input remains live until the last peer check.
struct NormalizeScratch {
    AlderLakeN::DisplayReadout after;
    Display13TC::Boot tc[3];
};
template<class IO> Result normalize(IO &io,const AlderLakeN::DisplayReadout &boot,bool admitted,
                                    const Display13HDMIWiring &w,const XELPMemory::Layout &memory,
                                    NormalizeScratch &scratch) {
    Result r;r.before=boot.activePipes;
    if(!admitted || &boot==&scratch.after)return r;
    r.status=Status::Topology;
    const auto active=boot.activePipes;
    if(w.panel || !memory.valid || !Display13FBTopology::multiOutputWiring(w) || boot.status!=AlderLakeN::ReadoutStatus::Complete ||
       !(active&1) || active==1 || (active&~7U) || (active&~boot.poweredPipes))return r;
    uint32_t ports[3] {},clocks[3] {},transport[3] {},used=0,dpPipes=0;
    auto &tc=scratch.tc;
    for(auto &port:tc)port={};
    uint32_t power=0,ddiPower=0,auxPower=0,gate=0;
    if(!AlderLakeN::value(boot,0x45404,power) || !AlderLakeN::value(boot,0x45454,ddiPower) ||
       !AlderLakeN::value(boot,0x45444,auxPower) ||
       !AlderLakeN::value(boot,Display13TC::Clock::Gate,gate))return r;
    // Every active firmware pipe must be a distinct selected linear GOP.
    // Pipe identity and physical DDI are independent. No foreign cursor/plane/DSB.
    for(unsigned p=0;p<3;++p)if(boot.poweredPipes&(1U<<p)) {
        const auto bank=p*0x1000;uint32_t v=0;
        if(!AlderLakeN::value(boot,0x70080+bank,v) || (v&63))return r;
        for(unsigned n=(active&(1U<<p)) ? 1 : 0;n<5;++n)
            if(!AlderLakeN::value(boot,0x70180+bank+n*0x100,v) || (v&0x80000000))return r;
        for(unsigned d=0;d<3;++d) {
            const auto reg=Display13FBIRQ::DSBControl(p,d);
            if(!io.readable(reg) || (io.read(reg)&0x80000001))return r;
        }
        if(!(active&(1U<<p))) {
            if(!AlderLakeN::value(boot,0x70008+bank,v) || (v&0xC0000000))return r;
            continue;
        }
        uint32_t ctl=0,trans=0,surface=0,live=0;
        if(!AlderLakeN::value(boot,0x70180+bank,ctl) || !AlderLakeN::value(boot,0x60400+bank,trans))return r;
        const auto port=Display13FBTopology::bootDDI(w,trans,true);
        if(port>=7 || (used&(1U<<port)))return r;
        const bool dp=((trans>>24)&7)==2;
        if(port>=3) {
            tc[p]=Display13TC::Boot::capture(io,true,port,p,dp);
            if(!tc[p].valid() || tc[p].power!=power || tc[p].ddiPower!=ddiPower)return r;
            // A powered AUX controller must be idle before its PHY can be
            // returned. An unpowered controller cannot have an active command.
            const auto aux=0x64310+0x100*(port-3);
            if((auxPower&(1U<<(2*port))) &&
               (!io.readable(aux) || (io.read(aux)&0x80000000U)))return r;
        }
        if(dp) {
            if(!AlderLakeN::dpSource(boot,p,port,port>=3?&tc[p]:nullptr))return r;
            dpPipes|=1U<<p;AlderLakeN::value(boot,0x60540+bank,transport[p]);
        }
        ports[p]=port;used|=1U<<port;
        uint32_t stride=0,size=0,position=0,offset=0,ht=0,vt=0,conf=0;
        if(((trans>>20)&7) || !AlderLakeN::value(boot,0x70188+bank,stride) ||
           !AlderLakeN::value(boot,0x70190+bank,size) || !AlderLakeN::value(boot,0x7018C+bank,position) ||
           !AlderLakeN::value(boot,0x701A4+bank,offset) || !AlderLakeN::value(boot,0x60000+bank,ht) ||
           !AlderLakeN::value(boot,0x6000C+bank,vt) || !AlderLakeN::value(boot,0x70008+bank,conf) ||
           (conf&0xC0000000)!=0xC0000000 || !AlderLakeN::value(boot,0x7019C+bank,surface) ||
           (surface&4095) || !AlderLakeN::value(boot,0x701AC+bank,live) || live!=surface)return r;
        const auto geometry=XELPGeometry::linear(ctl,stride,size,position,offset);
        if(!AlderLakeN::bootGeometry(boot,p,geometry,ht,vt) || uint64_t(surface)+geometry.bytes>memory.usable())return r;
        const auto clock=0x46140+4*p,buf=0x64000+0x100*port;
        if(!io.readable(clock) || !io.readable(buf) || !(io.read(buf)&0x80000000) ||
           !io.readable(0x60404+bank) || io.read(0x60404+bank))return r;
        clocks[p]=io.read(clock);
        if(clocks[p]==UINT32_MAX || (clocks[p]>>28)!=port+(port>=3?3:1))return r;
    }
    for(unsigned i=0;i<boot.count;++i) {
        const auto pair=boot.registers[i];
        if(pair.offset==0x70040 || pair.offset==0x71040 || pair.offset==0x72040)continue;
        if(io.read(pair.offset)!=pair.value) {r.status=Status::Readback;return r;}
    }
    for(unsigned p=0;p<3;++p)if((active&(1U<<p)) && ports[p]>=3 && !tc[p].unchanged(io)) {
        r.status=Status::Readback;return r;
    }
    uint32_t parked=0,current=0,stoppingTC=0;
    const auto peer=[&] {
        // These are borrowed firmware requests. Keep them unchanged, including
        // the stopped output's requests; native leases own subsequent changes.
        if(io.read(0x45404)!=power || io.read(0x45454)!=ddiPower || io.read(0x45444)!=auxPower)return false;
        for(unsigned p=0;p<3;++p)if((active&(1U<<p)) && !(parked&(1U<<p)) && p!=current)
            if(io.read(0x46140+4*p)!=clocks[p])return false;
        for(unsigned i=0;i<boot.count;++i) {
            const auto pair=boot.registers[i];const auto reg=pair.offset;
            // 4500C is a diagnostic observation that changes when the firmware
            // plane drains; it is not a shared resource configuration certificate.
            // Preserve actual bank/DDI and known shared control registers.
            bool mutableReg=reg==0x4500C;
            for(unsigned p=1;p<3;++p)if((active&(1U<<p)) && (p==current || (parked&(1U<<p)))) {
                const auto bank=0x1000*p;
                if((reg>=0x60000+bank && reg<0x61000+bank) ||
                   (reg>=0x70000+bank && reg<0x71000+bank) ||
                   (reg>=0x68170+0x800*p && reg<=0x68280+0x800*p) || reg==0x64000+0x100*ports[p] ||
                   reg==0x46140+4*p)mutableReg=true;
            }
            if(reg==0x70040 || reg==0x71040 || reg==0x72040)continue;
            const auto expected=reg==Display13TC::Clock::Gate?gate:pair.value;
            if(!mutableReg && io.read(reg)!=expected) {
                r.lastReg=reg;r.expected=expected;r.observed=io.read(reg);return false;
            }
        }
        for(unsigned p=0;p<3;++p)if((active&(1U<<p)) && ports[p]>=3 && !(stoppingTC&(1U<<p))) {
            if(p!=current) {if(!tc[p].unchanged(io))return false;}
            else if(io.read(tc[p].port.ready)!=tc[p].ready || io.read(tc[p].port.clock)!=tc[p].clock ||
                    io.read(tc[p].port.pll)!=tc[p].pll)return false;
        }
        return true;
    };
    const auto write=[&](uint32_t reg,uint32_t value) {
        io.write(reg,value);++r.writes;
        if(reg==Display13TC::Clock::Gate)gate=value;
        io.read(reg);return true;
    };
    const auto wait=[&](uint32_t reg,uint32_t mask,uint32_t expected) {
        r.lastReg=reg;r.expected=expected;
        return XELPGOP::poll(io,peer,[&](uint32_t v){return (v&mask)==expected;},
            reg,r.observed,r.status,Status::PeerChanged);
    };
    for(unsigned p=1;p<3;++p)if(active&(1U<<p)) {
        current=p;const auto bank=p*0x1000,bufReg=0x64000+0x100*ports[p];
        uint32_t plane=0,surface=0,conf=0,trans=0,buf=0;
        AlderLakeN::value(boot,0x70180+bank,plane);AlderLakeN::value(boot,0x7019C+bank,surface);
        AlderLakeN::value(boot,0x70008+bank,conf);AlderLakeN::value(boot,0x60400+bank,trans);buf=io.read(bufReg);
        r.stage=1;r.framesBefore=io.read(0x70040+bank);
        write(0x70180+bank,plane&~0x80000000U);write(0x7019C+bank,surface);
        if(!XELPGOP::poll(io,peer,[&](uint32_t v){return uint32_t(v-r.framesBefore)>=2 && uint32_t(v-r.framesBefore)<0x10000000U;},
            0x70040+bank,r.framesAfter,r.status,Status::PeerChanged,false))return r;
        if(!wait(0x70180+bank,0x80000000,0))return r;
        // Real frame latch, STOP_ACK and buffer idle precede DDB release.
        // Keep link resources until consumers stop. The TC PLL/ownership is
        // then returned below; shared power and predecessor memory stay held.
        r.stage=2;write(0x70008+bank,conf&~0x80000000U);
        if(!wait(0x70008+bank,0xC0000000,0))return r;
        // Detach only enabled scalers on the stopped secondary pipe. Keep the
        // primary pipe's firmware scaling untouched until native ownership.
        for(unsigned scaler=0;scaler<2;++scaler) {
            const auto base=0x68170+0x800*p+0x100*scaler;
            uint32_t ctl=0;
            if(AlderLakeN::value(boot,base+0x10,ctl) && (ctl&0x80000000U)) {
                write(base+0x10,0);write(base,0);write(base+4,0);
                if(!wait(base+0x10,UINT32_MAX,0) || !wait(base,UINT32_MAX,0) || !wait(base+4,UINT32_MAX,0))return r;
            }
        }
        r.stage=3;write(0x60404+bank,0);write(0x60400+bank,trans&~(0x80000000U|(15U<<27)|(7U<<24)));
        if(!wait(0x60400+bank,0x80000000,0))return r;
        r.stage=4;
        if(!XELPGOP::stopBuffer(write,wait,bank,bufReg,buf,transport[p],dpPipes&(1U<<p)))return r;
        r.stage=5;write(0x46140+4*p,clocks[p]&0x0FFFFFFF);
        if(!wait(0x46140+4*p,UINT32_MAX,clocks[p]&0x0FFFFFFF))return r;
        if(!Display13FBIRQ::consumersStopped(io,1U<<p)) {r.status=Status::Readback;return r;}
        if(ports[p]>=3) {
            // The live GOP certificate authorized this bootstrap transaction.
            // Reuse the runtime ownership and PLL operations after consumers
            // stop. Leaving the firmware ownership bit set would make the
            // later native Mode::connect reject this now-cold connector.
            stoppingTC|=1U<<p;
            struct TCIO {
                IO &io;const Display13TC::Boot &boot;Result &result;
                decltype(peer) &peers;decltype(write) &store;
                Display13TC::Ownership ownership {};
                bool readable(uint32_t reg){return io.readable(reg);}
                uint32_t read(uint32_t reg) {
                    if(!peers()){result.status=Status::PeerChanged;return UINT32_MAX;}
                    const auto v=io.read(reg);result.lastReg=reg;result.observed=v;return v;
                }
                void write(uint32_t reg,uint32_t value) {
                    if(!peers()){result.status=Status::PeerChanged;return;}store(reg,value);
                }
                // Early bootstrap is serialized, before native clock clients
                // and IRQ handlers exist. Preserve other ports' gate bits.
                unsigned lock(){return 0;}void unlock(unsigned){}
                void delayUs(unsigned us){io.delayUs(us);}
                bool powerHeld(){return peers();}
                bool powerHeld(uintptr_t owner,uintptr_t object,unsigned d) {
                    return owner==reinterpret_cast<uintptr_t>(&io) && object==reinterpret_cast<uintptr_t>(&boot) &&
                        d==boot.port.ddi && powerHeld();
                }
                bool quiescent(unsigned d) {
                    const auto bank=boot.pipe*0x1000;
                    return d==boot.port.ddi && !(read(0x70008+bank)&0xC0000000U) &&
                        !(read(0x60400+bank)&0x80000000U) && !(read(0x60540+bank)&0x80000000U) &&
                        !(read(0x46140+4*boot.pipe)&0xF0000000U) &&
                        (read(boot.port.buffer)&0x80000080U)==0x80;
                }
                bool inheritedVerified(uintptr_t owner,uintptr_t object,unsigned d) {
                    return powerHeld(owner,object,d) && quiescent(d);
                }
                bool owned(unsigned d){return d==boot.port.ddi && ownership.active;}
                bool quiescent(uintptr_t owner,uintptr_t object,unsigned d) {
                    return powerHeld(owner,object,d) && quiescent(d) &&
                        !(read(boot.port.pll)&0xCC000000U) && !(read(boot.port.clock)&0xF0000000U) &&
                        (read(Display13TC::Clock::Gate)&Display13TC::Clock::gateBit(boot.port.tc));
                }
            } access{io,tc[p],r,peer,write};
            const auto owner=reinterpret_cast<uintptr_t>(&io),object=reinterpret_cast<uintptr_t>(&tc[p]);
            if(access.ownership.acquire(access,owner,object,ports[p],true).status!=Display13TC::Status::Complete ||
               !Display13TC::Clock::disable(access,tc[p].port) ||
               access.ownership.release(access,owner,object,ports[p]).status!=Display13TC::Status::Complete) {
                if(r.status!=Status::PeerChanged)r.status=Status::Readback;return r;
            }
        }
        r.stage=6;write(0x7027C+bank,0);write(0x7037C+bank,0);write(0x7017C+bank,0);
        if(!wait(0x7027C+bank,UINT32_MAX,0) || !wait(0x7037C+bank,UINT32_MAX,0) || !wait(0x7017C+bank,UINT32_MAX,0))return r;
        parked|=1U<<p;
    }
    current=UINT32_MAX;
    auto &after=scratch.after;AlderLakeN::readDisplay(io,true,after);r.after=after.activePipes;
    if(after.status!=AlderLakeN::ReadoutStatus::Complete || after.activePipes!=1 || !peer()) {r.status=Status::PeerChanged;return r;}
    r.stage=7;r.status=Status::Complete;return r;
}
}
#endif
