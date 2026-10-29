#include "kern_display13_tc_dp.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace Display13TC;
struct IO {
    Port p;std::map<uint32_t,uint32_t> regs,bank;
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    bool connected {true};
    bool held {},power {true},owner {true},idle {true},stuckPower {},stuckLock {},fault {};
    unsigned time {},locks {},unlocks {};uint32_t reject {},absent {};
    unsigned expectedLanes {4};
    bool link {},failLink {},coldCleanup {};unsigned linkGets {},linkPuts {};
    bool linkPower(bool enable) {
        if(enable){assert((regs[Clock::laneRegister(p.tc)]&Clock::laneMask(p.tc))==((1U<<expectedLanes)-1)<<(4*(p.tc&1)));assert((regs[p.pll]&0xcc000000U)==0xcc000000U && !(regs[Clock::Gate]&Clock::gateBit(p.tc)));++linkGets;if(failLink)return false;link=true;}
        else {if(link){assert(regs[p.pll]&Clock::Enable);++linkPuts;}link=false;}
        return true;
    }
    bool unstarted(){return coldCleanup && !link && !(regs[p.pll]&0xcc000000U);}
    IO(unsigned ddi) {
        assert(Port::resolve(ddi,p));
        regs={{p.pll,0x20000000},{p.clock,0x1234},{p.ready,0xc},{p.buffer,0xc0},
              {Clock::Gate,0xdeadffff|Clock::gateBit(p.tc)},{p.hip,0x01010101},{Clock::laneRegister(p.tc),0xa5a50000}};
        for(auto w:TMDSClock::calculate(148500,38400).words)bank[w.reg]=0xa5a5a5a5;
    }
    bool linkHeld(){return power&&link;}
    bool powerHeld(){return power;}
    bool retainedOwned(unsigned d){return owner && d==p.ddi;}
    bool owned(unsigned d){return retainedOwned(d) && connected;}
    bool quiescent(unsigned d){return idle && d==p.ddi;}
    bool readable(uint32_t r){return r!=absent && (regs.count(r) || (r>=p.phy && r<p.phy+0x1000));}
    uint32_t key(uint32_t r){return (((regs[p.hip]>>p.shift)&15)<<12)|(r-p.phy);}
    uint32_t read(uint32_t r){assert(readable(r));return regs.count(r)?regs[r]:bank[key(r)];}
    void write(uint32_t r,uint32_t v) {
        assert(held && readable(r));writes.emplace_back(r,v);if(r==reject)return;
        if(r==p.pll) {
            auto status=regs[r]&(Clock::Locked|Clock::Powered);
            if(!stuckPower)status=(status&~Clock::Powered)|((v&Clock::Power)?Clock::Powered:0);
            if(!stuckLock)status=(status&~Clock::Locked)|((v&Clock::Enable)?Clock::Locked:0);
            regs[r]=(v&~(Clock::Locked|Clock::Powered))|status;
        } else if(regs.count(r))regs[r]=v;else bank[key(r)]=v;
    }
    unsigned lock(){assert(!held);held=true;++locks;return 7;}
    void unlock(unsigned irq){assert(held && irq==7);held=false;++unlocks;}
    void delayUs(unsigned n){assert(!held);time+=n;}
    void bankFault(){fault=true;}
};
int main() {
    // Independent Linux DP DDI rate encodings; retain ownership, lane width,
    // reversal, enable and unrelated bits; replace the previous rate code.
    for(const auto &pair:std::map<unsigned,unsigned>{{6,0},{10,1},{20,2},{30,3}}) {
        uint32_t value=0xfff1abcd;
        assert(DPSignal::bufferRate(pair.first,value));
        assert(value==((0xfff1abcdU&~0x700000U)|(pair.second<<20)));
    }
    for(unsigned invalid:{0U,1U,12U,UINT32_MAX}) {
        uint32_t value=0xa5a5a5a5;assert(!DPSignal::bufferRate(invalid,value) && value==0xa5a5a5a5);
    }
    for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned reset=0;reset<4;++reset) {
        IO io(ddi);io.link=true;
        io.bank[0x14]=0xa5a50001|((reset&1)?0x800:0);
        io.bank[0x1014]=0x5a5a0002|((reset&2)?0x800:0);
        const auto hip=io.regs[io.p.hip];uint32_t before[2]{},after[2]{};
        assert(DPSignal::prepareBuffer(io,io.p,before,after));
        assert(after[0]==0xa5a50001 && after[1]==0x5a5a0002);
        assert(io.bank[0x14]==after[0] && io.bank[0x1014]==after[1] && io.regs[io.p.hip]==hip);
        io.regs[io.p.buffer]|=0x80000000;
        assert(DPSignal::prepareBuffer(io,io.p,before,after)); // retained, already released
        io.bank[0x14]|=0x800;const auto prior=io.bank;
        assert(!DPSignal::prepareBuffer(io,io.p,before,after) && io.bank==prior);
    }
    for(unsigned failure=0;failure<6;++failure) {
        IO io(3);io.link=true;io.bank[0x14]=io.bank[0x1014]=0x800;
        if(failure==0)io.power=false;if(failure==1)io.owner=false;if(failure==2)io.link=false;
        if(failure==3)io.regs[io.p.buffer]=UINT32_MAX;if(failure==4)io.regs[io.p.buffer]&=~128U;
        if(failure==5)io.reject=io.p.phyRegister(0x14);
        uint32_t before[2]{},after[2]{};assert(!DPSignal::prepareBuffer(io,io.p,before,after));
        assert(io.bank[0x14]==0x800 && io.bank[0x1014]==0x800);
    }
    // Pin D, both physical orientations: do not change the USB pair's
    // signal coefficients or wake register. Logical main-link lanes stay0/1.
    for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned physical:{3U,12U}) {
        IO io(ddi);io.expectedLanes=2;io.regs[io.p.ready]=0x1d;
        for(unsigned group=0;group<2;++group)for(unsigned reg:{0xa0U,0xd00U,0x2c0U,0x2c4U,0x2c8U})io.bank[group*0x1000+reg]=0xa5a5a5a5;
        Clock::LinkStage stage;assert(DPSignal::startLink(io,io.p,DPClock::calculate(10,38400),2,&stage,4,physical));
        const unsigned usb=physical==3?0x1000:0;
        for(unsigned reg:{0xd00U,0x2c0U,0x2c4U,0x2c8U})assert(io.bank[usb+reg]==0xa5a5a5a5);
        const auto count=io.writes.size();
        assert(!DPSignal::startLink(io,io.p,DPClock::calculate(10,38400),4,&stage,4,physical) && io.writes.size()==count);
    }
    // Stop retains ownership and power even when the connector state is gone.
    for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned gone:{0U,4U,0x1dU,UINT32_MAX}) {
        IO io(ddi);io.expectedLanes=2;io.regs[io.p.ready]=0x1d;
        for(unsigned group=0;group<2;++group)for(unsigned reg:{0xa0U,0xd00U,0x2c0U,0x2c4U,0x2c8U})io.bank[group*0x1000+reg]=0;
        assert(DPSignal::startLink(io,io.p,DPClock::calculate(10,38400),2,nullptr,4,12));
        io.connected=false;io.regs[io.p.ready]=gone;
        const auto bank=io.bank;const auto peer=io.regs[Clock::Gate]&~Clock::gateBit(io.p.tc);
        const auto before=io.writes.size();
        assert(!Clock::enable(io,io.p,TMDSClock::calculate(148500,38400)) && io.writes.size()==before);
        assert(Clock::stopLink(io,io.p) && !io.link && io.linkPuts==1);
        assert(io.bank==bank && !(io.regs[io.p.pll]&0xcc000000U));
        assert((io.regs[Clock::Gate]&~Clock::gateBit(io.p.tc))==peer);
        assert(Clock::stopLink(io,io.p) && io.linkPuts==1); // no double put
    }
    for(unsigned fault=0;fault<7;++fault) {
        IO io(4);io.connected=false;io.regs[io.p.ready]=0;
        if(fault==0)io.owner=false;if(fault==1)io.power=false;if(fault==2)io.idle=false;
        if(fault==3)io.regs[io.p.buffer]|=1U<<31;if(fault==4)io.regs[io.p.buffer]&=~64U;
        if(fault==5)io.regs[io.p.buffer]&=~128U;if(fault==6)io.regs[io.p.pll]=UINT32_MAX;
        assert(!Clock::stopLink(io,io.p) && io.writes.empty() && !io.linkPuts);
    }
    // Independent DP divisor oracle: 8.1 GHz / (5 * link symbol clock).
    const unsigned rates[]={6,10,20,30},high[]={5,3,3,2},div[]={2,2,1,1};
    // Existing independent Linux capture of TC3 at RBR x4.
    const uint32_t liveDP[]={0x100,0xa00,0x6200,0x84269,0x1c0027,0x40002000,0x5e000000,0x52};
    auto measured=DPClock::calculate(6,38400);for(unsigned i=0;i<8;++i)assert(measured.words[i].value==liveDP[i]);
    for(unsigned ref:{19200U,24000U,38400U})for(unsigned i=0;i<4;++i) {
        const auto plan=DPClock::calculate(rates[i],ref);
        assert(plan.valid() && plan.dco==8100000 && plan.hs==high[i] && plan.ds==div[i]);
        assert(TMDSClock::frequency(ref,plan.words[3].value,plan.words[6].value,plan.words[2].value)==rates[i]*27000);
        assert(!(plan.words[2].value&0x10000));
        for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned lanes:{1U,2U,4U}) {
            IO io(ddi);io.expectedLanes=lanes;
            for(unsigned group=0;group<2;++group)for(unsigned reg:{0xa0U,0xd00U,0x2c0U,0x2c4U,0x2c8U})io.bank[group*0x1000+reg]=0xa5a5a5a5;
            const auto peer=io.regs[Clock::laneRegister(ddi-3)]&~Clock::laneMask(ddi-3),hip=io.regs[io.p.hip];
            Clock::LinkStage stage;assert(DPSignal::startLink(io,io.p,plan,lanes,&stage));
            assert(stage==Clock::LinkStage::Complete && io.link && io.linkGets==1);
            assert((io.regs[Clock::laneRegister(ddi-3)]&~Clock::laneMask(ddi-3))==peer && io.regs[io.p.hip]==hip);
            for(unsigned group=0;group<2;++group) {
                assert((io.bank[group*0x1000+0xa0]&0xc0)==(lanes==1?(group?0x40U:0U):0x80U));
                assert(!(io.bank[group*0x1000+0x2c8]&0x7c));
            }
            DPRetainedLink retained{plan.rate,plan.reference,lanes};
            assert(retained.matches(io,io.p,plan,lanes));
            auto mismatch=retained;mismatch.lanes=lanes==4?2:4;assert(!mismatch.matches(io,io.p,plan,lanes));
            const auto pll=io.regs[io.p.pll];io.regs[io.p.pll]&=~Clock::Locked;
            assert(!retained.matches(io,io.p,plan,lanes));io.regs[io.p.pll]=pll;
            io.idle=false;assert(retained.matches(io,io.p,plan,lanes)); // Active training link is retained.
            // Training signal changes are valid with a live buffer.
            for(unsigned swing=0;swing<4;++swing)for(unsigned pre=0;pre+swing<4;++pre)assert(DPSignal::program(io,io.p,rates[i],swing,pre));
            assert(!DPSignal::program(io,io.p,rates[i],3,1));
            io.idle=true;assert(Clock::stopLink(io,io.p) && io.linkPuts==1);
        }
    }
    for(unsigned rate:{0U,12U,19U,31U})assert(!DPClock::calculate(rate,38400).valid());
    // ADLP RBR/HBR and HBR2/HBR3 independently expected coefficient vectors.
    const unsigned expectedLow[]={0x107,0x605,0xb02,0x1700,5,0x802,0x1400,2,0xb00,0};
    const unsigned expectedHigh[]={7,0x405,0xa02,0x1800,5,0x602,0x1400,2,0x900,0};
    unsigned index=0;for(unsigned v=0;v<4;++v)for(unsigned pre=0;v+pre<4;++pre,++index) {
        unsigned value;assert(DPSignal::coefficients(6,v,pre,value) && value==expectedLow[index]);
        assert(DPSignal::coefficients(20,v,pre,value) && value==expectedHigh[index]);
    }
    const auto full=TMDSClock::calculate(148500,38400),hd=TMDSClock::calculate(74250,38400);
    // Independent live i915 shared-PLL captures: central FHD / outer 720p.
    const uint32_t f[]={0x100,0x500,0x15400,0x84274,0x1c0027,0x40002000,0x41000000,0x4a};
    const uint32_t h[]={0x100,0x500,0x16500,0xa5278,0x1c0027,0x40002000,0x76600000,0x47};
    assert(full.valid() && hd.valid());
    for(unsigned i=0;i<8;++i){assert(full.words[i].value==f[i]);assert(hd.words[i].value==h[i]);}
    for(unsigned ref:{19200U,24000U,38400U})for(unsigned rate=25000;rate<=600000;rate+=125) {
        auto plan=TMDSClock::calculate(rate,ref);
        // Finite PLL divider range has real clock holes; do not round those
        // into a different display timing. Independently enumerate solutions.
        unsigned oscillator=0;for(unsigned h:{7U,5U,3U,2U})for(unsigned d=10;d;--d)
            if(!oscillator && 5*rate*h*d>=7992000 && 5*rate*h*d<=10000000)oscillator=5*rate*h*d;
        assert(plan.valid()==(oscillator && oscillator/(2*ref)<=255));
        if(!plan.valid())continue;
        const auto actual=TMDSClock::frequency(ref,plan.words[3].value,plan.words[6].value,plan.words[2].value);
        assert(actual==rate);
        for(auto w:plan.words)assert(!(w.value&~w.mask));
    }
    for(unsigned rate:{0U,24999U,600001U,UINT32_MAX})assert(!TMDSClock::calculate(rate,38400).valid());
    for(unsigned ref:{0U,1U,25000U,UINT32_MAX})assert(!TMDSClock::calculate(148500,ref).valid());
    for(unsigned ddi=3;ddi<=6;++ddi) {
        IO io(ddi);const auto before=io.regs;auto previous=io.bank;
        assert(Clock::enable(io,io.p,full));assert(io.regs[io.p.hip]==before.at(io.p.hip));
        assert(io.regs[io.p.pll]==0xec000000 && io.regs[io.p.clock]==0x80001234);
        assert(io.regs[Clock::Gate]==(before.at(Clock::Gate)&~Clock::gateBit(ddi-3)));
        for(auto w:full.words)assert(io.bank[w.reg]==((previous[w.reg]&~w.mask)|w.value));
        const auto count=io.writes.size();assert(!Clock::enable(io,io.p,hd) && io.writes.size()==count);
        assert(Clock::disable(io,io.p));assert(io.regs[io.p.pll]==0x20000000 && io.regs[io.p.clock]==0x1234);
        assert(io.regs[Clock::Gate]==before.at(Clock::Gate) && io.locks==io.unlocks);
        assert(Clock::enable(io,io.p,hd));assert(Clock::disable(io,io.p));
    }
    const uint32_t native[]={464,2048,3,2,2};
    assert(TMDSClock::nativeRate(native,2)==148500);
    assert(!TMDSClock::nativeRate(native,3) && !TMDSClock::nativeRate(nullptr,2));
    for(unsigned d=3;d<=6;++d)for(unsigned preset=0;preset<10;++preset) {
        IO io(d);auto before=io.regs[io.p.hip];
        for(unsigned lane=0;lane<2;++lane)for(unsigned reg:{0xa0U,0xd00U,0x2c0U,0x2c4U,0x2c8U})io.bank[lane*0x1000+reg]=0xa5a5a5a5;
        assert(TMDSSignal::program(io,io.p,preset));
        const unsigned swing[]={7,6,4,2,0,0,0,0,0,0},emphasis[]={0,0,0,0,0,5,6,7,8,10};
        for(unsigned lane=0;lane<2;++lane) {
            assert(io.bank[lane*0x1000+0xa0]==((0xa5a5a5a5&~0xc0U)|0x80));
            assert(io.bank[lane*0x1000+0xd00]==0);
            for(unsigned tx=0;tx<2;++tx)assert(io.bank[lane*0x1000+0x2c0+4*tx]==((0xa5a5a5a5&~0x3ff07U)|swing[preset]|(emphasis[preset]<<8)));
            assert(io.bank[lane*0x1000+0x2c8]==((0xa5a5a5a5&~0x7cU)|(lane?0x78U:0x40U)));
        }
        assert(io.regs[io.p.hip]==before);
        const auto count=io.writes.size();assert(!TMDSSignal::program(io,io.p,10) && io.writes.size()==count);
        io.owner=false;assert(!TMDSSignal::program(io,io.p,7) && io.writes.size()==count);
    }
    for(unsigned failure=0;failure<8;++failure) {
        IO io(3);
        if(failure==0)io.power=false;if(failure==1)io.owner=false;if(failure==2)io.idle=false;
        if(failure==3)io.regs[io.p.ready]=6;if(failure==4)io.regs[io.p.buffer]|=1U<<31;
        if(failure==5)io.regs[io.p.buffer]&=~64U;if(failure==6)io.absent=io.p.pll;if(failure==7)io.regs[io.p.pll]=~0U;
        assert(!Clock::enable(io,io.p,full) && io.writes.empty());
        assert(!Clock::disable(io,io.p) && io.writes.empty());
    }
    {IO io(3);io.stuckPower=true;assert(!Clock::enable(io,io.p,full) && io.time==1000 && io.writes.size()==1);}
    {IO io(3);io.stuckLock=true;assert(!Clock::enable(io,io.p,full) && io.time==1000);assert(!(io.regs[io.p.clock]&0xf0000000));}
    {IO io(3);io.reject=io.p.phyRegister(0x2200);auto bank=io.regs[io.p.hip];assert(!Clock::enable(io,io.p,full));assert(io.regs[io.p.hip]==bank && !(io.regs[io.p.pll]&Clock::Enable));}
    for(unsigned d=3;d<=6;++d) {
        IO io(d);for(unsigned lane=0;lane<2;++lane)for(unsigned reg:{0xa0U,0xd00U,0x2c0U,0x2c4U,0x2c8U})io.bank[lane*0x1000+reg]=0;
        // Cold control/AUX is held, but DDI IO is off until the PLL is locked.
        const auto prior=io.regs[Clock::laneRegister(io.p.tc)];
        assert(Clock::startLink(io,io.p,full,7) && io.link && io.linkGets==1);
        assert(io.regs[Clock::laneRegister(io.p.tc)]==(prior|Clock::laneMask(io.p.tc)));
        assert(Clock::stopLink(io,io.p) && !io.link && io.linkPuts==1);
        assert(Clock::startLink(io,io.p,hd,7) && io.linkGets==2);
        assert(Clock::stopLink(io,io.p) && io.linkPuts==2);
    }
    for(unsigned d=3;d<=6;++d) {
        IO io(d);auto reg=Clock::laneRegister(io.p.tc);io.regs[reg]=0xa5a500a5;
        auto prior=io.regs[reg];assert(Clock::lanes(io,io.p));
        assert(io.regs[reg]==(prior|Clock::laneMask(io.p.tc)));
        io.regs[reg]=UINT32_MAX;auto n=io.writes.size();assert(!Clock::lanes(io,io.p) && io.writes.size()==n);
    }
    {IO io(4);io.stuckLock=true;assert(!Clock::startLink(io,io.p,full,7) && !io.linkGets);}
    {IO io(4);io.failLink=true;assert(!Clock::startLink(io,io.p,full,7) && io.linkGets==1 && !io.link);}
    {IO io(4);io.power=io.owner=false;io.coldCleanup=true;const auto count=io.writes.size();
     assert(Clock::stopLink(io,io.p) && io.writes.size()==count); // local acquisition failure, PLL never started
     io.regs[io.p.pll]|=Clock::Enable;assert(!Clock::stopLink(io,io.p));}
    puts("PASS Display13 TC HDMI PLL live fixtures, clock sweep, reserved bits, bounded failures and lifecycle");
}
