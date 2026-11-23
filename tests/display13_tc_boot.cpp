#include "kern_display13_boot.hpp"
#include "kern_display13_handoff.hpp"
#include "kern_display13_fb_memory.hpp"
#include <map>
#include <vector>
#include <cassert>
#include <cstdio>
using namespace AlderLakeN;
struct IO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<Register> writes;
    XELPMemory::Layout memory=XELPMemory::decode(0x04C1,0x38000000,0);
    unsigned ticks {},changeTick {},pteReads {};
    uint32_t changeReg {},changePTEReg {},badPTE {UINT32_MAX};
    bool noFrames {},failEnable {};
    bool readable(uint32_t reg) const {return reg<=0xFFFFFC;}
    uint32_t read(uint32_t reg) {return reg==0x70040 ? ticks : regs[reg];}
    uint64_t readPTE(uint32_t reg) {
        ++pteReads;
        if(pteReads==3 && changePTEReg)regs[changePTEReg]^=1;
        return memory.pte(uint64_t(reg-0x800000)/8*4096)^(reg==badPTE?4096:0);
    }
    void write(uint32_t reg,uint32_t value) {
        writes.push_back({reg,value});
        if(failEnable && reg==0x70180 && value==0x94000008){failEnable=false;return;}
        regs[reg]=value;
    }
    void delay() {
        if(!noFrames)++ticks;
        if(changeTick && ticks==changeTick)regs[changeReg]^=1;
    }
};
static IO firmware(unsigned ddi) {
    IO io;const auto tc=ddi-3;
    // G660i single TC1 GOP geometry and clock/ownership, translated only
    // by the Display13 physical TC stride for the other register instances.
    io.regs={{0x45404,0xFC0F},{0x45454,3U<<(2*ddi)},
        {0x161500+4*tc,0xC},{0x64000+0x100*ddi,0x80000040},
        {0x46038+8*tc,0xCC000000},{0x46100+4*ddi,0x80000000},
        {0x46140,(ddi+3)<<28},{0x60400,0x80030100|((ddi+1)<<27)},
        {0x60000,0x0897077F},{0x6000C,0x04640437},{0x70008,0xC0000000},
        {0x70028,0xA0F},{0x70180,0x84000008},{0x70188,120},{0x70190,0x0437077F}};
    return io;
}
template<class T> static void put(void *p,size_t offset,T value) {memcpy(static_cast<uint8_t *>(p)+offset,&value,sizeof(value));}
static Display13FBMemory::Readout nativeMemory(IO &io,unsigned ddi,const Display13TC::Boot &tc,
                                              const DisplayReadout *dp=nullptr) {
    uint8_t controller[0x1C20] {},fb[0x4B90] {};
    put(fb,0x1D0,reinterpret_cast<uintptr_t>(controller));
    put(controller,0xD5C,uint32_t(1));put(controller,0xD60,reinterpret_cast<uintptr_t>(fb));
    put(controller,0xDF5,uint8_t(1));put(controller,0xDF4,uint8_t(1));put(controller,0xDF0,uint32_t(1));
    put(controller,0x1A10,uint32_t(1));put(controller,0x1A28,io.memory.base);
    put(controller,0x1A30,uint32_t(0x4000000));put(controller,0x1A34,uint32_t(0x4000));
    return Display13FBMemory::inspect(io,controller,sizeof(controller),fb,sizeof(fb),0x10000000,io.memory,true,0,ddi,&tc,dp);
}
static IO dpFirmware(unsigned ddi,unsigned lanes,bool enhanced) {
    auto io=firmware(ddi);
    // Synthetic SST source from Linux's Display13 register fields. The
    // measured HDMI fixture supplies only geometry/physical TC coordinates;
    // this is not a claim about a particular firmware's DP startup values.
    io.regs[0x60400]=0x82030000|((ddi+1)<<27)|((lanes-1)<<1);
    io.regs[0x64000+0x100*ddi]=0x80000040|((lanes-1)<<1);
    io.regs[0x60540]=0x80000300|(enhanced?1U<<18:0);
    io.regs[0x60030]=0x7E100000;io.regs[0x60034]=0x800000;
    io.regs[0x60040]=0x100000;io.regs[0x60044]=0x800000;
    return io;
}
static void dpHandoff() {
    unsigned cases=0;
    for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned lanes:{1U,2U,4U})for(bool enhanced:{false,true}) {
        auto io=dpFirmware(ddi,lanes,enhanced);const auto original=io.regs;
        const auto tc=Display13TC::Boot::capture(io,true,ddi,0);
        const auto source=readDisplay(io,true);LinearPlan plan;
        assert(dpSource(source,0,ddi,&tc) && dpSourceUnchanged(io,source,0,ddi,0,&tc));
        // The powered selector and PHY link rate must describe this TC, even
        // if another port changes. A gated port cannot inherit the stream.
        {
            auto test=io;
            const unsigned rates[]={162000,270000,540000,810000,216000,243000,324000,432000};
            for(unsigned code=0;code<8;++code) {
                test.regs[tc.port.buffer]=(tc.buffer&~(7U<<20))|(code<<20);
                Display13Handoff::Stream stream;DisplayReadout scratch;
                assert(Display13Handoff::stream(test,scratch,ddi,false,24000,stream));
                assert(stream.symbolKHz==rates[code] && stream.lanes()==lanes);
                test.regs[0x164280]=Display13TC::Clock::gateBit(ddi-3);
                assert(!Display13Handoff::stream(test,scratch,ddi,false,24000,stream));
                test.regs[0x164280]=0;
            }
        }
        // No unpowered TC register is pulled into the shared reader, and
        // no DP acceptance is obtained by omitting either source/TC proof.
        uint32_t ignored=0;assert(!value(source,tc.port.buffer,ignored));
        assert(!dpSource(source,0,ddi));
        assert(!Display13Boot::reservedPrefix(io,source,true,ddi,io.memory,&tc));
        assert(linearPlan(source,plan,ddi,GOPProtocol::DP)==LinearStatus::InvalidTCSource);
        auto foreign=tc;foreign.port.ddi=ddi==6?3:ddi+1;
        assert(!dpSource(source,0,ddi,&foreign));
        assert(!dpSourceUnchanged(io,source,0,ddi,2U<<(2*ddi),&tc));
        assert(Display13Boot::reservedPrefix(io,source,true,ddi,io.memory,&tc,GOPProtocol::DP));
        assert(io.writes.empty());
        const auto result=Display13Boot::prepareLinear(io,source,true,ddi,io.memory,&tc,GOPProtocol::DP);
        assert(result.status==LinearStatus::Success && result.writes==10 && tc.unchanged(io));
        for(auto pair:original)if(pair.first!=0x70028 && pair.first!=0x70180)assert(io.regs[pair.first]==pair.second);
        auto after=readDisplay(io,true);
        assert(nativeMemory(io,ddi,tc).status==Display13FBMemory::Status::DisplayState);
        assert(nativeMemory(io,ddi,tc,&after).status==Display13FBMemory::Status::Complete);
        // Reject loss/drift at entry, while waiting for a frame, and during
        // the PTE walk. A changed active link forbids speculative rollback.
        for(auto reg:{0x45404U,0x45454U,0x45444U,0x60400U,0x60540U,0x60030U,
                      0x60034U,0x60040U,0x60044U,0x46140U,0x78000U,0x78004U,tc.port.buffer}) {
            auto changed=dpFirmware(ddi,lanes,enhanced);changed.regs[reg]^=1;
            auto r=reinitializeLinear(changed,source,true,ddi,GOPProtocol::DP,&tc);
            assert(r.status==LinearStatus::StateChanged && changed.writes.empty());
            for(unsigned tick:{1U,3U,5U}) {
                auto drift=dpFirmware(ddi,lanes,enhanced);drift.changeReg=reg;drift.changeTick=tick;
                r=reinitializeLinear(drift,source,true,ddi,GOPProtocol::DP,&tc);
                assert(r.status==LinearStatus::StateChanged && !r.restored && r.writes==(tick==1?0U:tick==3?2U:10U));
            }
            auto prefix=dpFirmware(ddi,lanes,enhanced);prefix.changePTEReg=reg;
            assert(!Display13Boot::reservedPrefix(prefix,source,true,ddi,prefix.memory,&tc,GOPProtocol::DP));
            assert(prefix.writes.empty());
            auto pool=io;pool.changePTEReg=reg;pool.pteReads=0;
            assert(nativeMemory(pool,ddi,tc,&after).status==Display13FBMemory::Status::StateChanged);
        }
        for(auto invalid:std::vector<Register>{{0x60400,0x83030006|((ddi+1)<<27)},
            {0x60540,0x80000000},{0x60540,0x80000380},{0x60034,0},{0x60040,0},
            {0x78000,0x80000000},{0x78004,0x80000000},{0x60404,1},
            {tc.port.buffer,0x80000040|((lanes==4?0:3)<<1)}}) {
            auto bad=dpFirmware(ddi,lanes,enhanced);bad.regs[invalid.offset]=invalid.value;
            const auto witness=Display13TC::Boot::capture(bad,true,ddi,0);
            assert(!dpSource(readDisplay(bad,true),0,ddi,&witness));
        }
        auto rollback=dpFirmware(ddi,lanes,enhanced);rollback.failEnable=true;
        auto r=reinitializeLinear(rollback,source,true,ddi,GOPProtocol::DP,&tc);
        assert(r.status==LinearStatus::EnableTimeout && r.restored && tc.unchanged(rollback));
        ++cases;
    }
    printf("PASS %u TC SST source/plane/pool handoffs, proof mismatch, drift and rollback\n",cases);
}
static void unownedDP() {
    for(unsigned ddi=3;ddi<=6;++ddi) {
        auto io=dpFirmware(ddi,4,true);const auto reg=0x64000+0x100*ddi;
        io.regs[reg]&=~64U;
        assert(!Display13TC::Boot::capture(io,true,ddi,0).valid());
        const auto tc=Display13TC::Boot::capture(io,true,ddi,0,true);
        const auto view=readDisplay(io,true);LinearPlan plan;
        assert(tc.valid() && dpSourceUnchanged(io,view,0,ddi,0,&tc));
        assert(Display13Boot::prepareLinear(io,view,true,ddi,io.memory,&tc,GOPProtocol::DP).status==LinearStatus::Success);
        assert(!(io.regs[reg]&64)); // Plane preparation cannot silently acquire PHY ownership.
        const auto unowned=readDisplay(io,true);
        assert(nativeMemory(io,ddi,tc,&unowned).status==Display13FBMemory::Status::DisplayState);
        io.regs[reg]|=64;
        assert(!tc.unchanged(io)); // Ownership transfer needs a new strict witness.
        const auto owned=Display13TC::Boot::capture(io,true,ddi,0);
        const auto after=readDisplay(io,true);
        assert(owned.valid() && nativeMemory(io,ddi,owned,&after).status==Display13FBMemory::Status::Complete);
        auto hdmi=firmware(ddi);hdmi.regs[reg]&=~64U;
        const auto optional=Display13TC::Boot::capture(hdmi,true,ddi,0,true);
        assert(optional.valid());
        assert(linearPlan(readDisplay(hdmi,true),plan,ddi,GOPProtocol::TMDS,&optional)==LinearStatus::InvalidTCSource);
        assert(nativeMemory(hdmi,ddi,optional).status==Display13FBMemory::Status::DisplayState);
    }
}
static void sstPayloadReadout() {
    for(unsigned ddi=3;ddi<=6;++ddi) {
        auto io=dpFirmware(ddi,4,true);
        // Observed firmware SST source: bit8 set, enhanced framing, four lanes.
        // The coordinate translation is synthetic for the other TC slots.
        io.regs[0x60400]|=1U<<8;
        io.regs[0x60030]=0x7e1a6666;io.regs[0x60040]=0x23333;io.regs[0x60044]=0x80000;
        const auto tc=Display13TC::Boot::capture(io,true,ddi,0);
        const auto source=readDisplay(io,true);
        assert(Display13Boot::prepareLinear(io,source,true,ddi,io.memory,&tc,GOPProtocol::DP).status==LinearStatus::Success);
        assert(io.regs[0x60400]==(0x82030106U|((ddi+1)<<27)));
        const auto after=readDisplay(io,true);
        assert(nativeMemory(io,ddi,tc,&after).status==Display13FBMemory::Status::Complete);
        io.regs[0x60400]^=1U<<8;
        assert(!dpSourceUnchanged(io,source,0,ddi,0,&tc));
        io.regs[0x60400]|=1U<<24; // Real MST mode still rejected.
        assert(!dpSource(readDisplay(io,true),0,ddi,&tc));
    }
}
int main() {
    sstPayloadReadout();
    unownedDP();
    unsigned cases=0;
    for(unsigned ddi=3;ddi<=6;++ddi) {
        auto io=firmware(ddi);const auto original=io.regs;
        auto tc=Display13TC::Boot::capture(io,true,ddi,0);
        auto display=readDisplay(io,true);LinearPlan plan;
        assert(tc.valid() && tc.unchanged(io));
        assert(linearPlan(display,plan,ddi)==LinearStatus::InvalidTCSource);
        assert(Display13Boot::reservedPrefix(io,display,true,ddi,io.memory,&tc));
        assert(io.writes.empty());
        for(auto reg:original)assert(io.regs[reg.first]==reg.second);
        assert(nativeMemory(io,ddi,tc).status==Display13FBMemory::Status::DisplayState);
        io.pteReads=0;
        auto result=Display13Boot::prepareLinear(io,display,true,ddi,io.memory,&tc);
        io.pteReads=0;
        assert(result.status==LinearStatus::Success && result.writes==10 && tc.unchanged(io));
        for(auto w:io.writes)assert(w.offset==0x42084 || w.offset==0x70028 || w.offset==0x70180 ||
            w.offset==0x70188 || w.offset==0x7018C || w.offset==0x70190 || w.offset==0x701A4 || w.offset==0x7019C);
        for(auto r:original)if(r.first!=0x70028 && r.first!=0x70180)assert(io.regs[r.first]==r.second);
        assert(Display13Boot::reservedPrefix(io,readDisplay(io,true),true,ddi,io.memory,&tc));
        assert(io.pteReads==io.memory.bytes/4096);
        const auto native=nativeMemory(io,ddi,tc);
        assert(native.status==Display13FBMemory::Status::Complete && native.verifiedPages==0x4000);
        auto rejected=firmware(ddi);rejected.badPTE=0x800010;
        auto preparation=Display13Boot::prepareLinear(rejected,readDisplay(rejected,true),true,ddi,rejected.memory,&tc);
        assert(preparation.status==LinearStatus::StateChanged && rejected.writes.empty());
        auto denied=firmware(ddi);
        preparation=Display13Boot::prepareLinear(denied,readDisplay(denied,true),false,ddi,denied.memory,&tc);
        assert(preparation.status==LinearStatus::NotAdmitted && denied.writes.empty() && !denied.pteReads);
        auto bad=firmware(ddi);bad.badPTE=0x800010;
        assert(!Display13Boot::reservedPrefix(bad,readDisplay(bad,true),true,ddi,bad.memory,&tc) && bad.pteReads==3);
        for(unsigned wrong:{0U,1U,2U,7U,8U,UINT32_MAX})assert(!Display13TC::Boot::capture(io,true,wrong,0).valid());
        assert(!Display13TC::Boot::capture(io,false,ddi,0).valid());
        assert(!Display13TC::Boot::capture(io,true,ddi,3).valid());
        const auto p=tc.port;
        const uint32_t link[]={0x45404,0x45454,p.ready,p.buffer,p.clock,p.pll,0x46140};
        for(auto reg:link) {
            auto changed=firmware(ddi);auto source=readDisplay(changed,true);changed.regs[reg]^=1;
            auto r=reinitializeLinear(changed,source,true,ddi,GOPProtocol::TMDS,&tc);
            assert(r.status==LinearStatus::StateChanged && changed.writes.empty());
            for(unsigned tick:{1U,3U,5U}) {
                auto drift=firmware(ddi);drift.changeReg=reg;drift.changeTick=tick;
                r=reinitializeLinear(drift,readDisplay(drift,true),true,ddi,GOPProtocol::TMDS,&tc);
                assert(r.status==LinearStatus::StateChanged && !r.restored && r.writes==(tick==1?0U:tick==3?2U:10U));
            }
            auto unavailable=firmware(ddi);unavailable.regs[reg]=UINT32_MAX;
            assert(!Display13TC::Boot::capture(unavailable,true,ddi,0).valid());
        }
        for(unsigned state:{0U,1U,2U,5U,6U,7U}) {
            auto alt=firmware(ddi);alt.regs[p.ready]=state;
            assert(!Display13TC::Boot::capture(alt,true,ddi,0).valid());
        }
        auto wrongClock=firmware(ddi);wrongClock.regs[0x46140]=(ddi+1)<<28;
        assert(!Display13TC::Boot::capture(wrongClock,true,ddi,0).valid());
        auto rollback=firmware(ddi);rollback.failEnable=true;
        result=reinitializeLinear(rollback,readDisplay(rollback,true),true,ddi,GOPProtocol::TMDS,&tc);
        assert(result.status==LinearStatus::EnableTimeout && result.restored && tc.unchanged(rollback));
        auto stopped=firmware(ddi);stopped.noFrames=true;
        result=reinitializeLinear(stopped,readDisplay(stopped,true),true,ddi,GOPProtocol::TMDS,&tc);
        assert(result.status==LinearStatus::NoVblank && stopped.writes.empty());
        ++cases;
    }
    printf("PASS %u TC GOP handoffs: power/ownership/clock preservation, plane rollback, GGTT prefix and link drift\n",cases);
    dpHandoff();
}
