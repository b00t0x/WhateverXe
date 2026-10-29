#include "kern_display13_boot.hpp"
#include "kern_display13_tc_mode.hpp"
#include "kern_display13_tc_dp.hpp"
#include <map>
#include <vector>
#include <cassert>
#include <cstdio>
// Independent scratch per call for the existing hardware-behavior cases.
// Explicit reuse/alias checks below exercise the persistent caller storage.
template<class IO> Display13Boot::Result normalize(IO &io,const AlderLakeN::DisplayReadout &boot,bool admitted,
 const Display13HDMIWiring &w,const XELPMemory::Layout &memory) {
 Display13Boot::NormalizeScratch scratch;
 return Display13Boot::normalize(io,boot,admitted,w,memory,scratch);
}
struct IO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<AlderLakeN::Register> writes;
    uint32_t pteReads {},badPTE {UINT32_MAX};
    XELPMemory::Layout memory=XELPMemory::decode(0x03C1,0x7A800000,0);
    uint64_t readPTE(uint32_t reg) {
        assert(reg==0x800000+8*pteReads);++pteReads;
        return memory.pte(uint64_t(reg-0x800000)/8*4096)^(reg==badPTE?4096:0);
    }
    bool noFrames {},noStop {},noIdle {},changePeer {},stuckPLL {};
    uint32_t timeUs {};
    uint32_t changeRegister {},failWrite {};
    IO() {
        regs[0x45404]=0xFC0F;regs[0x4438C]=0x1C000700;
        regs[0x64000]=regs[0x64100]=0x80000000;
        regs[0x46140]=0x10000000;regs[0x46144]=0x20000000;regs[0x61200]=0x1000;
        for(unsigned p=0;p<2;++p) {
            const auto b=p*0x1000;
            regs[0x60400+b]=p ? 0x90030500 : 0x88030100;
            regs[0x70008+b]=0xC0000000;regs[0x70180+b]=0x84000008;
            regs[0x70188+b]=120;regs[0x70190+b]=0x0437077F;
            regs[0x60000+b]=0x0897077F;regs[0x6000C+b]=0x04640437;
            regs[0x7027C+b]=0x03FF0000;
        }
    }
    bool readable(uint32_t reg) {return reg<0x1000000;}
    uint32_t read(uint32_t reg) {return regs[reg];}
    void write(uint32_t reg,uint32_t value) {
        writes.push_back({reg,value});if(reg==failWrite)return;regs[reg]=value;
        if((reg==0x71008 || reg==0x72008) && !(value&0x80000000) && !noStop)regs[reg]&=~0x40000000;
        if((reg>=0x64000 && reg<=0x64600 && !(reg&255)) && !(value&0x80000000) && !noIdle)regs[reg]|=0x80;
        for(unsigned t=0;t<4;++t)if(reg==0x46038+8*t && !stuckPLL) {
            regs[reg]=(value&~0x44000000U)|((value&0x80000000U)?0x40000000U:0)|((value&0x08000000U)?0x04000000U:0);
        }
        if(writes.size()==1)regs[0x4500C]=0;
        if(changePeer && writes.size()==1)regs[0x70180]^=8;
        if(changeRegister && writes.size()==1)regs[changeRegister]^=1;
    }
    void delayUs(unsigned us) {timeUs+=us;}
    void delay() {if(!noFrames) {++regs[0x70040];if(regs[0x71008]&0xC0000000)++regs[0x71040];if(regs[0x72008]&0xC0000000)++regs[0x72040];}}
};
const uint32_t prop[] {1,7,1,2,3};
const auto wiring=TGLHDMIWiring::fromProperty(prop,sizeof(prop));
const auto memory=XELPMemory::decode(0x03C1,0x7A800000,0);
static void dp(IO &io,unsigned pipe,unsigned port,unsigned lanes,bool enhanced) {
    const auto bank=pipe*0x1000;
    io.regs[0x60400+bank]=0x82030000|((port+1)<<27)|((lanes-1)<<1);
    io.regs[0x64000+port*0x100]=0x80000000|((lanes-1)<<1);
    io.regs[0x60540+bank]=0x80000300|(enhanced?1U<<18:0);
    io.regs[0x60030+bank]=0x7E100000;io.regs[0x60034+bank]=0x800000;
    io.regs[0x60040+bank]=0x100000;io.regs[0x60044+bank]=0x800000;
}
static Display13OutputWiring protocols(unsigned dpMask) {
    Display13OutputWiring w=wiring;w.hdmi=3&~dpMask;w.dp=dpMask;
    for(unsigned d=0;d<3;++d)if(!(w.hdmi&(1U<<d)))w.ddc[d]=0;
    assert(w.described());return w;
}
static IO singleGOP(unsigned ddi,unsigned pipe=0) {
    IO io;
    for(unsigned p=0;p<3;++p) {
        const auto b=0x1000*p;
        io.regs[0x70008+b]=p==pipe?0xC0000000:0;
        io.regs[0x70180+b]=p==pipe?0x84000008:0;
        io.regs[0x60400+b]=p==pipe?0x80030000|((ddi+1)<<27):0;
        if(p==pipe) {
            io.regs[0x70188+b]=120;io.regs[0x70190+b]=0x0437077F;
            io.regs[0x60000+b]=0x0897077F;io.regs[0x6000C+b]=0x04640437;
        }
    }
    return io;
}
static void prefixCertificates() {
    unsigned cases=0;
    for(unsigned ddi=0;ddi<3;++ddi)for(unsigned gms:{1U,2U,3U}) {
        auto io=singleGOP(ddi);io.memory=XELPMemory::decode((gms<<8)|0xC1,0x7A800000,0);
        const auto display=AlderLakeN::readDisplay(io,true);
        assert(Display13Boot::reservedPrefix(io,display,true,ddi,io.memory));
        assert(io.pteReads==io.memory.bytes/4096 && io.writes.empty());
        for(unsigned i:{0U,unsigned(io.memory.bytes/8192),unsigned(io.memory.bytes/4096-1)}) {
            auto bad=io;bad.pteReads=0;bad.badPTE=0x800000+8*i;
            assert(!Display13Boot::reservedPrefix(bad,display,true,ddi,bad.memory));
            assert(bad.pteReads==i+1 && bad.writes.empty());++cases;
        }
        for(unsigned fail=0;fail<7;++fail) {
            auto bad=singleGOP(ddi);auto mem=io.memory;
            if(fail==1)bad=singleGOP(ddi,1);
            if(fail==2)dp(bad,0,ddi,4,true);
            if(fail==3)mem.valid=false;
            if(fail==4)mem.allocationEnd=AlderLakeN::ScanoutBytes-4096;
            if(fail==5)bad.regs[0x701AC]=4096;
            assert(!Display13Boot::reservedPrefix(bad,AlderLakeN::readDisplay(bad,true),fail!=0,
                                                  fail==6?3:ddi,mem));
            assert(!bad.pteReads && bad.writes.empty());++cases;
        }
        ++cases;
    }
    printf("PASS %u Display boot certificates: physical layouts, PTE corruption, no-read rejection\n",cases);
}
static Display13OutputWiring multiGOP(IO &io,const unsigned *ports,unsigned count,uint32_t dpMask,bool owned=true) {
    Display13OutputWiring w;w.valid=true;w.dp=dpMask;
    io.regs[0x42000]=0x8e380000;io.regs[Display13TC::Clock::Gate]=0x01e0ffff;
    for(unsigned p=0;p<3;++p) {
        const auto b=p*0x1000;
        io.regs[0x70008+b]=io.regs[0x70180+b]=io.regs[0x60400+b]=0;
        io.regs[0x46140+4*p]=0;
        if(p>=count)continue;
        const auto d=ports[p];if(!(dpMask&(1U<<d))){w.hdmi|=1U<<d;w.ddc[d]=d+(d>=3?6:1);}
        io.regs[0x70008+b]=0xc0000000;io.regs[0x70180+b]=0x84000008;
        // Linear 720p firmware geometry, independent of protocol and DDI.
        io.regs[0x70188+b]=80;io.regs[0x70190+b]=0x02cf04ff;
        io.regs[0x60000+b]=0x067104ff;io.regs[0x6000c+b]=0x02ed02cf;
        io.regs[0x7027c+b]=0x03ff0000;
        io.regs[0x60400+b]=0x80030100|((d+1)<<27);
        io.regs[0x64000+0x100*d]=0x80000000;
        if(dpMask&(1U<<d))dp(io,p,d,4,true);
        io.regs[0x46140+4*p]=(d+(d>=3?3:1))<<28;
        if(d>=3) {
            Display13TC::Port port;assert(Display13TC::Port::resolve(d,port));
            io.regs[0x45454]|=3U<<(2*d);io.regs[0x45444]|=3U<<(2*d);
            io.regs[port.ready]=0xc;io.regs[port.pll]=0xcc000000;io.regs[port.clock]=0x80000000;
            io.regs[Display13TC::Clock::Gate]&=~Display13TC::Clock::gateBit(port.tc);
            if(owned || !(dpMask&(1U<<d)))io.regs[port.buffer]|=64;
        }
    }
    assert(w.described());return w;
}
// Compose the real boot, power, Mode and clock cores. This models MMIO ACKs;
// it is a regression test for the handoff, not proof of physical output.
struct Runtime : IO {
    Display13TC::Port port;Display13TC::Mode mode;Display13Power::State power;
    Display13Power::Lease ddi;unsigned native {};bool locked {};
    Runtime(const IO &boot,unsigned d):IO(boot) {
        assert(Display13TC::Port::resolve(d,port));writes.clear();
        assert(Display13Power::initialize(*this,power,Display13Power::nativeDomains(0x7b)));
    }
    bool admissionReady(){return true;}
    unsigned nativeMode(){return native;}
    unsigned nativeSetMode(unsigned v){native=v;return 0;}
    bool tcAuxSupported(){return true;}
    bool prepareTCAux(unsigned t){return t==port.tc && (read(port.buffer)&64);}
    bool tcAuxHealthy(unsigned){return true;}
    Display13Power::Result acquireDomains(Display13Power::Lease &l,uint32_t d){return Display13Power::acquire(*this,power,l,d,true);}
    Display13Power::Result releaseDomains(Display13Power::Lease &l){return Display13Power::release(*this,power,l);}
    bool powerHeld(uintptr_t a,uintptr_t b,unsigned d) {
        return a==1 && b==2 && d==port.ddi && Display13Power::held(*this,power,Display13TC::Mode::accessDomains());
    }
    bool inheritedVerified(uintptr_t,uintptr_t,unsigned){return false;}
    bool powerHeld(){return mode.accessLease.owner && mode.auxLease.owner && Display13Power::held(*this,power,mode.auxLease.domains);}
    bool retainedOwned(unsigned d){return owned(d);}
    bool owned(unsigned d){return d==port.ddi && mode.phase==Display13TC::Mode::Phase::Active && mode.ownership.active && native==4;}
    bool quiescent(unsigned d){return d==port.ddi && !(read(port.buffer)&0x80000000U);}
    bool quiescent(uintptr_t,uintptr_t,unsigned d){return quiescent(d) && !(read(port.pll)&0xcc000000U) && !ddi.owner;}
    bool linkPower(bool enable) {
        if(!enable && !ddi.owner)return true;
        return (enable?acquireDomains(ddi,Display13Power::ddiDomain(port.ddi)):releaseDomains(ddi)).status==Display13Power::Status::Complete;
    }
    bool unstarted(){return false;}
    unsigned lock(){assert(!locked);locked=true;return 0;}
    void unlock(unsigned){assert(locked);locked=false;}
    void bankFault(){assert(false);}
};
static void tcComposition() {
    struct Case {unsigned ports[3],count,dp;};
    const Case cases[]={{{5,3,0},2,1U<<5},{{3,5,0},2,1U<<5},
        {{0,4,0},2,0},{{6,1,0},2,1U<<6},{{1,6,0},2,1U<<6},
        {{3,4,5},3,(1U<<3)|(1U<<5)},{{4,1,6},3,1U<<6}};
    for(const auto &c:cases)for(bool owner:{false,true}) {
        IO io;const auto wiring=multiGOP(io,c.ports,c.count,c.dp,owner);
        const auto boot=AlderLakeN::readDisplay(io,true);const auto before=io.regs;
        const auto r=normalize(io,boot,true,wiring,memory);
        assert(r.status==Display13Boot::Status::Complete && r.after==1);
        for(auto reg:{0x45404U,0x45454U,0x45444U,0x60400U,0x60540U,0x46140U,0x70180U,0x7027cU})
            assert(io.regs[reg]==before.at(reg));
        if(c.ports[0]>=3) {
            const auto retained=Display13TC::Boot::capture(io,true,c.ports[0],0,(c.dp&(1U<<c.ports[0])));
            assert(retained.valid());
            for(auto reg:{retained.port.buffer,retained.port.clock,retained.port.pll,retained.port.ready})assert(io.regs[reg]==before.at(reg));
        }
        for(unsigned p=1;p<c.count;++p)if(c.ports[p]>=3) {
            const auto d=c.ports[p];Display13TC::Port port;assert(Display13TC::Port::resolve(d,port));
            assert((io.regs[port.buffer]&0x800000c0U)==0x80 && !(io.regs[port.pll]&0xcc000000U));
            assert(!(io.regs[port.clock]&0xf0000000U) && !io.regs[0x46140+4*p]);
            assert(io.regs[Display13TC::Clock::Gate]&Display13TC::Clock::gateBit(port.tc));
            Runtime runtime(io,d);
            assert(runtime.mode.connect(runtime,1,2,d).status==Display13TC::Status::Complete);
            // No firmware-adoption exemption is supplied to the new Mode.
            const bool started=(c.dp&(1U<<d))?
                Display13TC::DPSignal::startLink(runtime,port,Display13TC::DPClock::calculate(6,38400),4):
                Display13TC::Clock::startLink(runtime,port,Display13TC::TMDSClock::calculate(74250,38400),9);
            assert(started && runtime.ddi.owner && (runtime.regs[port.pll]&0xcc000000U)==0xcc000000U);
            assert(Display13TC::Clock::stopLink(runtime,port));
            assert(runtime.mode.disconnect(runtime,1,2,d).status==Display13TC::Status::Complete);
        }
    }
    const unsigned ports[]={5,3};
    for(unsigned failure=0;failure<4;++failure) {
        IO io;const auto wiring=multiGOP(io,ports,2,1U<<5);const auto boot=AlderLakeN::readDisplay(io,true);
        if(failure==0)io.noIdle=true;
        if(failure==1)io.stuckPLL=true;
        if(failure==2)io.changeRegister=0x46048; // Retained TC3 PLL changes.
        if(failure==3)io.failWrite=Display13TC::Clock::Gate;
        const auto r=normalize(io,boot,true,wiring,memory);
        assert(r.status!=Display13Boot::Status::Complete && io.regs[0x7127c]);
        assert(io.regs[0x64300]&64); // Failure never returns the affected PHY.
        assert(io.regs[0x70180]&0x80000000U);
    }
    // Unsupported source/AUX activity is rejected before either GOP changes.
    for(unsigned bad=0;bad<3;++bad) {
        IO io;const auto wiring=multiGOP(io,ports,2,1U<<5);
        if(bad==0)io.regs[0x64310]=0x80000000;
        if(bad==1)io.regs[0x161500]=1;
        if(bad==2)io.regs[0x46144]=0x40000000; // Combo selector on a TC port.
        assert(normalize(io,AlderLakeN::readDisplay(io,true),true,wiring,memory).status==Display13Boot::Status::Topology);
        assert(io.writes.empty());
    }
    puts("PASS mixed Combo/TC GOP handoff into normal Mode/clock start, peer preservation and failure retention");
}
static void scaledBoot() {
    const unsigned ports[] {5,3,4};
    const auto setup=[&](IO &io,unsigned pipe,unsigned scaler) {
        const auto b=pipe*0x1000,ps=0x68170+pipe*0x800+scaler*0x100;
        io.regs[0x70188+b]=80;io.regs[0x70190+b]=0x02CF04FF;
        io.regs[0x6001C+b]=0x04FF02CF;
        io.regs[0x60000+b]=0x0897077F;io.regs[0x6000C+b]=0x04640437;
        io.regs[ps+0x10]=0x80000000;io.regs[ps]=0;io.regs[ps+4]=(1920U<<16)|1080;
    };
    for(unsigned scaler=0;scaler<2;++scaler) {
        IO io;auto w=multiGOP(io,ports,3,1U<<5);
        setup(io,0,scaler);setup(io,2,1-scaler);
        auto boot=AlderLakeN::readDisplay(io,true);
        assert(boot.status==AlderLakeN::ReadoutStatus::Complete && boot.count<=160);
        auto n=normalize(io,boot,true,w,memory);
        assert(n.status==Display13Boot::Status::Complete && n.after==1);
        assert(io.regs[0x68180+scaler*0x100]==0x80000000);
        assert(!io.regs[0x69180+(1-scaler)*0x100]);
        for(auto wr:io.writes)assert(wr.offset<0x68170 || wr.offset>0x68280);
        auto after=AlderLakeN::readDisplay(io,true);
        auto tc=Display13TC::Boot::capture(io,true,5,0,true);
        AlderLakeN::LinearPlan plan;
        assert(AlderLakeN::linearPlan(after,plan,5,AlderLakeN::GOPProtocol::DP,&tc)==AlderLakeN::LinearStatus::Ready);
        assert(plan.geometry.width==1280 && plan.geometry.height==720);
        assert(Display13Boot::reservedPrefix(io,after,true,5,memory,&tc,AlderLakeN::GOPProtocol::DP));
        io.regs[0x68174+scaler*0x100]^=1;
        assert(!Display13Boot::reservedPrefix(io,after,true,5,memory,&tc,AlderLakeN::GOPProtocol::DP));
    }
    // A timing mismatch alone is never permission to accept scaling.
    for(unsigned fault=0;fault<11;++fault) {
        IO io;auto w=multiGOP(io,ports,3,1U<<5);setup(io,0,0);
        if(fault==0)io.regs[0x68180]=0;
        if(fault==1)io.regs[0x68180]|=1U<<25;
        if(fault==2)io.regs[0x68180]|=1U<<29;
        if(fault==3)io.regs[0x68180]|=1U<<19;
        if(fault==4)io.regs[0x68170]=1;
        if(fault==5)io.regs[0x68174]^=1;
        if(fault==6)io.regs[0x6001C]^=1;
        if(fault==7) {io.regs[0x68280]=0x80000000;io.regs[0x68274]=io.regs[0x68174];}
        if(fault==8)io.regs[0x60000]=0x400004FF;
        if(fault==9)io.regs[0x60000]=0x0100077F;
        if(fault==10)io.regs[0x6000C]=0x046401FF;
        auto n=normalize(io,AlderLakeN::readDisplay(io,true),true,w,memory);
        if(n.status!=Display13Boot::Status::Topology || !io.writes.empty())fprintf(stderr,"fault=%u status=%u writes=%zu\n",fault,unsigned(n.status),io.writes.size());
        assert(n.status==Display13Boot::Status::Topology && io.writes.empty());
    }
    IO drift;auto w=multiGOP(drift,ports,3,1U<<5);setup(drift,0,0);
    drift.changeRegister=0x68174;
    auto n=normalize(drift,AlderLakeN::readDisplay(drift,true),true,w,memory);
    assert(n.status==Display13Boot::Status::PeerChanged && drift.writes.size()==2);
    puts("PASS scaled multi-GOP: binding, full destination, primary preservation, secondary detach and drift rejection");
}
static void unusedTCBoot() {
    const unsigned ports[] {5,4};
    const auto setup=[&](IO &io) {
        auto w=multiGOP(io,ports,2,1U<<5);w.hdmi|=1U<<3;w.ddc[3]=9;
        assert(normalize(io,AlderLakeN::readDisplay(io,true),true,w,memory).status==Display13Boot::Status::Complete);
        io.writes.clear();io.regs[0x64300]=0xC0;io.regs[0x161500]=0xC;
        io.regs[0x4610C]=io.regs[0x46038]=0;io.regs[Display13TC::Clock::Gate]|=1U<<12;
        io.regs[0x45444]|=3U<<6;
        return w;
    };
    IO io;auto w=setup(io);const auto before=io.regs;
    auto r=Display13Boot::releaseUnusedTC(io,AlderLakeN::readDisplay(io,true),true,w);
    assert(r.status==Display13Boot::Status::Complete && r.cleared==8 && r.writes==1 && io.regs[0x64300]==0x80);
    for(auto x:before)if(x.first!=0x64300)assert(io.regs[x.first]==x.second);
    assert(io.writes.size()==1 && io.writes[0].offset==0x64300 && io.writes[0].value==0x80);
    auto again=Display13Boot::releaseUnusedTC(io,AlderLakeN::readDisplay(io,true),true,w);
    assert(again.status==Display13Boot::Status::Complete && again.writes==0);
    // DSI-only GOP firmware can leave DP-alt ownership with no TC output.
    {IO t;auto wiring=setup(t);wiring.hdmi&=~8U;wiring.ddc[3]=0;wiring.dp=wiring.typeC=8;
     t.regs[0x161500]=0x1d;const auto before=t.regs;
     const auto released=Display13Boot::releaseUnusedTC(t,AlderLakeN::readDisplay(t,true),true,wiring);
     assert(released.status==Display13Boot::Status::Complete && released.cleared==8 && released.writes==1);
     for(auto x:before)if(x.first!=0x64300)assert(t.regs[x.first]==x.second);
     t.regs[0x64300]=0xc0;t.regs[0x161500]=6;t.writes.clear();
     assert(Display13Boot::releaseUnusedTC(t,AlderLakeN::readDisplay(t,true),true,wiring).status!=Display13Boot::Status::Complete && t.writes.empty());}
    // Firmware may leave the idle port mux ungated after the last OS used it.
    for(unsigned alt=0;alt<2;++alt) {
        IO t;auto wiring=setup(t);
        if(alt){wiring.hdmi&=~8U;wiring.ddc[3]=0;wiring.dp=wiring.typeC=8;t.regs[0x161500]=0x1d;}
        t.regs[Display13TC::Clock::Gate]&=~(1U<<12);const auto before=t.regs;
        const auto released=Display13Boot::releaseUnusedTC(t,AlderLakeN::readDisplay(t,true),true,wiring);
        assert(released.status==Display13Boot::Status::Complete && released.cleared==8 && released.writes==2);
        assert(t.writes[0].offset==Display13TC::Clock::Gate && t.writes[0].value==(before.at(Display13TC::Clock::Gate)|(1U<<12)));
        assert(t.writes[1].offset==0x64300 && t.writes[1].value==0x80);
        for(auto x:before)if(x.first!=0x64300 && x.first!=Display13TC::Clock::Gate)assert(t.regs[x.first]==x.second);
        IO failed;auto fw=setup(failed);failed.regs[Display13TC::Clock::Gate]&=~(1U<<12);
        failed.failWrite=Display13TC::Clock::Gate;
        const auto rejected=Display13Boot::releaseUnusedTC(failed,AlderLakeN::readDisplay(failed,true),true,fw);
        assert(rejected.status==Display13Boot::Status::Readback && failed.regs[0x64300]==0xc0 && failed.writes.size()==1);
    }
    // Any live/ambiguous resource prevents release of this unclassified port.
    for(unsigned fault=0;fault<9;++fault) {
        IO t;auto wiring=setup(t);
        if(fault==0)t.regs[0x64300]|=0x80000000;
        if(fault==1)t.regs[0x64300]&=~0x80U;
        if(fault==2)t.regs[0x161500]|=1;
        if(fault==3)t.regs[0x4610C]=0x80000000;
        if(fault==4)t.regs[0x46038]=0xCC000000;
        if(fault==5)t.regs[Display13TC::Clock::Gate]=UINT32_MAX;
        if(fault==6)t.regs[0x64310]=0x80000000;
        if(fault==7)t.regs[0x64300]=UINT32_MAX;
        if(fault==8)t.regs[0x161500]=UINT32_MAX;
        auto r=Display13Boot::releaseUnusedTC(t,AlderLakeN::readDisplay(t,true),true,wiring);
        assert(r.status!=Display13Boot::Status::Complete && t.writes.empty());
    }
    for(unsigned kind=0;kind<2;++kind) {
        IO t;auto wiring=setup(t);
        t.regs[kind?0x46148:0x62400]=kind?0x60000000:0x20000000;
        auto r=Display13Boot::releaseUnusedTC(t,AlderLakeN::readDisplay(t,true),true,wiring);
        assert(r.status==Display13Boot::Status::Complete && r.writes==0 && t.regs[0x64300]==0xC0);
    }
    IO changed;auto wiring=setup(changed);auto boot=AlderLakeN::readDisplay(changed,true);changed.regs[0x68174]=1;
    r=Display13Boot::releaseUnusedTC(changed,boot,true,wiring);
    assert(r.status==Display13Boot::Status::PeerChanged && changed.writes.empty());
    IO failed;wiring=setup(failed);failed.failWrite=0x64300;
    r=Display13Boot::releaseUnusedTC(failed,AlderLakeN::readDisplay(failed,true),true,wiring);
    assert(r.status==Display13Boot::Status::Readback && failed.writes.size()==1);
    puts("PASS unused fixed TC firmware ownership: cold-only release, peers, live-resource refusal and readback");
}
int main() {
    // Repeated normalizations must reset old witnesses and read a fresh final
    // snapshot. Aliasing that storage with the live input must fail before IO.
    Display13Boot::NormalizeScratch scratch;
    for(unsigned trial=0;trial<3;++trial) {
        IO fresh;auto boot=AlderLakeN::readDisplay(fresh,true);
        const auto result=Display13Boot::normalize(fresh,boot,true,wiring,memory,scratch);
        assert(result.status==Display13Boot::Status::Complete && result.writes==10);
        assert(boot.activePipes==3 && scratch.after.activePipes==1);
    }
    IO alias;AlderLakeN::readDisplay(alias,true,scratch.after);
    const auto rejected=Display13Boot::normalize(alias,scratch.after,true,wiring,memory,scratch);
    assert(rejected.status==Display13Boot::Status::Unavailable && alias.writes.empty());
    unusedTCBoot();
    scaledBoot();
    prefixCertificates();
    tcComposition();
    IO io;const auto before=io.regs;
    const auto r=normalize(io,AlderLakeN::readDisplay(io,true),true,wiring,memory);
    assert(r.status==Display13Boot::Status::Complete && r.after==1 && r.stage==7 && r.writes==10);
    assert(!io.regs[0x7127C] && !io.regs[0x7137C] && !io.regs[0x7117C]);
    for(auto w:io.writes)assert(w.offset==0x71180 || w.offset==0x7119C || w.offset==0x71008 ||
        w.offset==0x61404 || w.offset==0x61400 || w.offset==0x64100 || w.offset==0x46144 ||
        w.offset==0x7127C || w.offset==0x7137C || w.offset==0x7117C);
    for(auto pair:before)if(pair.first!=0x70040 && pair.first!=0x46144 && pair.first<0x61000 && pair.first!=0x60404)
        assert(io.regs[pair.first]==pair.second);
    assert(io.regs[0x46140]==0x10000000 && !io.regs[0x46144] && io.regs[0x61200]==0x1000);
    IO frames;frames.noFrames=true;
    assert(normalize(frames,AlderLakeN::readDisplay(frames,true),true,wiring,memory).status==Display13Boot::Status::Timeout);
    assert(frames.writes.size()==2 && frames.regs[0x7127C]);
    IO stop;stop.noStop=true;
    assert(normalize(stop,AlderLakeN::readDisplay(stop,true),true,wiring,memory).status==Display13Boot::Status::Timeout);
    assert(stop.writes.size()==3 && stop.regs[0x7127C]);
    IO idle;idle.noIdle=true;
    assert(normalize(idle,AlderLakeN::readDisplay(idle,true),true,wiring,memory).status==Display13Boot::Status::Timeout);
    assert(idle.writes.size()==6 && idle.regs[0x7127C]);
    IO peer;peer.changePeer=true;
    assert(normalize(peer,AlderLakeN::readDisplay(peer,true),true,wiring,memory).status==Display13Boot::Status::PeerChanged);
    assert(peer.writes.size()==2 && peer.regs[0x7127C]);
    for(auto reg:{0x71080U,0x71280U,Display13FBIRQ::DSBControl(1,0),0x60404U}) {
        IO foreign;foreign.regs[reg]=0x80000001;
        assert(normalize(foreign,AlderLakeN::readDisplay(foreign,true),true,wiring,memory).status==Display13Boot::Status::Topology);
        assert(foreign.writes.empty());
    }
    IO port;port.regs[0x61400]=0x88030500;
    assert(normalize(port,AlderLakeN::readDisplay(port,true),true,wiring,memory).status==Display13Boot::Status::Topology && port.writes.empty());
    IO denied;assert(normalize(denied,AlderLakeN::readDisplay(denied,true),false,wiring,memory).status==Display13Boot::Status::Unavailable && denied.writes.empty());
    // Three firmware GOPs, all six physical-port/pipe permutations.
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)for(unsigned c=0;c<3;++c) {
        if(a==b || a==c || b==c)continue;
        IO t;unsigned ports[] {a,b,c};
        for(unsigned p=0;p<3;++p) {
            const auto bank=p*0x1000;
            for(auto reg:{0x70008U,0x70180U,0x70188U,0x70190U,0x60000U,0x6000CU,0x7027CU})t.regs[reg+bank]=t.regs[reg];
            t.regs[0x60400+bank]=0x80030100|((ports[p]+1)<<27);
            t.regs[0x46140+4*p]=(ports[p]+1)<<28;
            t.regs[0x64000+0x100*ports[p]]=0x80000000;
        }
        auto n=normalize(t,AlderLakeN::readDisplay(t,true),true,wiring,memory);
        assert(n.status==Display13Boot::Status::Complete && n.before==7 && n.after==1 && n.writes==20);
        assert(t.regs[0x64000+0x100*a]&0x80000000);
        assert(!t.regs[0x7127C] && !t.regs[0x7227C] && t.regs[0x7027C]);
    }
    for(auto width:{1280U,1920U,2560U,3840U})for(auto height:{720U,1080U,1440U,2160U}) {
        IO t;
        for(unsigned p=0;p<2;++p) {
            const auto bank=p*0x1000;
            t.regs[0x70188+bank]=(width*4+63)/64;
            t.regs[0x70190+bank]=((height-1)<<16)|(width-1);
            t.regs[0x60000+bank]=((width+280-1)<<16)|(width-1);
            t.regs[0x6000C+bank]=((height+45-1)<<16)|(height-1);
        }
        assert(normalize(t,AlderLakeN::readDisplay(t,true),true,wiring,memory).status==Display13Boot::Status::Complete);
    }
    unsigned combinations=0;
    for(unsigned a=0;a<2;++a)for(unsigned parked:{1U,2U})for(unsigned dpMask=0;dpMask<4;++dpMask)
    for(unsigned lanes:{1U,2U,4U})for(bool enhanced:{false,true}) {
        IO t;auto mixed=protocols(dpMask);
        if(parked==2) {
            for(auto reg:{0x70008U,0x70180U,0x70188U,0x70190U,0x60000U,0x6000CU,0x7027CU})t.regs[reg+0x2000]=t.regs[reg];
            t.regs[0x61400]=t.regs[0x71008]=t.regs[0x71180]=0;
        }
        for(unsigned p:{0U,parked}) {
            const auto port=p?1-a:a;
            t.regs[0x60400+p*0x1000]=0x80030100|((port+1)<<27);
            t.regs[0x46140+4*p]=(port+1)<<28;
            if(dpMask&(1U<<port))dp(t,p,port,lanes,enhanced);
        }
        const auto boot=AlderLakeN::readDisplay(t,true);const auto before=t.regs;
        assert(boot.version==2 && boot.count==157);
        const auto result=normalize(t,boot,true,mixed,memory);
        const bool parkDP=dpMask&(1U<<(1-a));
        assert(result.status==Display13Boot::Status::Complete && result.after==1 && result.writes==(parkDP?11U:10U));
        for(auto reg:{0x60400U,0x60540U,0x60030U,0x60034U,0x60040U,0x60044U,0x46140U,0x78000U,0x78004U})
            assert(t.regs[reg]==before.at(reg));
        assert(t.regs[0x64000+0x100*a]==before.at(0x64000+0x100*a));
        if(parkDP) {
            assert(t.writes[5].offset==0x64000+0x100*(1-a));
            assert(t.writes[6].offset==0x60540+parked*0x1000 && !(t.writes[6].value&0x80000000));
            assert(t.writes[7].offset==0x46140+4*parked);
        }
        for(auto write:t.writes)for(auto mn:{0x60030U,0x60034U,0x60040U,0x60044U})
            assert(write.offset!=mn && write.offset!=mn+parked*0x1000);
        ++combinations;
    }
    auto mixed=protocols(3);
    // Both the retained GOP and the parked GOP must have a valid source.
    const AlderLakeN::Register invalid[] {{0x60540,0x80000000},{0x60540,0xC0000300},
        {0x60540,0x88000300},{0x60540,0x80000380},{0x60404,0x10},{0x78000,1U<<29},
        {0x78004,1U<<31},{0x60030,0x7E000000},{0x60034,0},{0x60040,0},{0x60044,0},
        {0x60030,0xFE100000},{0x60034,0x100},{0x60040,0x1000000},{0x60044,0x1000000},
        {0x60400,0x8A030004},{0x60400,0x8B030006},{0x60400,0x8A030206}};
    for(unsigned p:{0U,1U})for(auto bad:invalid) {
        IO t;dp(t,0,0,4,true);dp(t,1,1,4,true);
        auto reg=bad.offset+(bad.offset>=0x78000?p*0x200:p*0x1000);
        auto val=bad.value;if(bad.offset==0x60400)val+=p<<27;
        t.regs[reg]=val;
        assert(normalize(t,AlderLakeN::readDisplay(t,true),true,mixed,memory).status==Display13Boot::Status::Topology);
        assert(t.writes.empty());
    }
    for(auto reg:{0x64000U,0x64100U,0x46140U,0x46144U}) {
        IO t;dp(t,0,0,4,true);dp(t,1,1,4,true);t.regs[reg]=0;
        assert(normalize(t,AlderLakeN::readDisplay(t,true),true,mixed,memory).status==Display13Boot::Status::Topology && t.writes.empty());
    }
    // Snapshot freshness and peer preservation cover DP metadata as well.
    for(auto reg:{0x60540U,0x60030U,0x60034U,0x60040U,0x60044U,0x46140U,0x78004U}) {
        IO t;dp(t,0,0,4,true);dp(t,1,1,4,true);const auto boot=AlderLakeN::readDisplay(t,true);
        t.regs[reg]^=1;
        assert(normalize(t,boot,true,mixed,memory).status==Display13Boot::Status::Readback && t.writes.empty());
        t.regs[reg]^=1;t.changeRegister=reg;
        assert(normalize(t,boot,true,mixed,memory).status==Display13Boot::Status::PeerChanged && t.writes.size()==2);
    }
    IO tp;dp(tp,0,0,4,true);dp(tp,1,1,4,true);tp.failWrite=0x61540;
    auto failure=normalize(tp,AlderLakeN::readDisplay(tp,true),true,mixed,memory);
    assert(failure.status==Display13Boot::Status::Timeout && failure.stage==4 && tp.writes.size()==7 && tp.regs[0x7127C]);
    assert(Display13FBTopology::bootDDI(mixed,tp.regs[0x60400])==UINT32_MAX); // Runtime stays closed.
    printf("PASS GOP bootstrap: %u HDMI/DP combinations, STOP/transport/idle ordering, peer/M-N preservation and bounded failures\n",combinations);
}
