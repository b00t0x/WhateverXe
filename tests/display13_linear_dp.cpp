#include "kern_alderlake_n.hpp"
#include <map>
#include <vector>
#include <cassert>
#include <cstdio>
using namespace AlderLakeN;
struct IO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<Register> writes;
    unsigned pipe {},ticks {},changeTick {};
    uint32_t changeReg {};
    bool failEnable {},noFrames {};
    bool readable(uint32_t reg) {return reg<0x1000000;}
    uint32_t read(uint32_t reg) {return reg==0x70040+pipe*0x1000 ? ticks : regs[reg];}
    void write(uint32_t reg,uint32_t v) {
        writes.push_back({reg,v});
        if(failEnable && reg==0x70180+pipe*0x1000 && v==0x94000008){failEnable=false;return;}
        regs[reg]=v;
    }
    void delay() {
        if(!noFrames)++ticks;
        if(changeTick && ticks==changeTick)regs[changeReg]^=1;
    }
};
static IO firmware(unsigned pipe,unsigned ddi,unsigned lanes,bool enhanced,unsigned width=1920) {
    IO io;io.pipe=pipe;const auto b=pipe*0x1000;
    io.regs={{0x45404,0xFC0F},{0x45454,3U<<(2*ddi)},{0x45444,0},{0x42000,0x8E380000},
        {0x60400+b,0x82030000|((ddi+1)<<27)|((lanes-1)<<1)},
        {0x60540+b,0x80000300|(enhanced?1U<<18:0)},
        {0x64000+ddi*0x100,0x80000000|((lanes-1)<<1)},{0x46140+4*pipe,(ddi+1)<<28},
        {0x60030+b,0x7E100000},{0x60034+b,0x800000},{0x60040+b,0x100000},{0x60044+b,0x800000},
        {0x60000+b,((width+279)<<16)|(width-1)},{0x6000C+b,0x04640437},{0x70008+b,0xC0000000},
        {0x70028+b,0xA0F},{0x70180+b,0x84000008},{0x70188+b,(width*4+63)/64},
        {0x70190+b,0x04370000|(width-1)}};
    return io;
}
int main() {
    // The firmware drives one uncompressed internal panel with CMTG A secondary.
    auto secondary=firmware(0,0,4,true);
    secondary.regs[0x60404]=8; secondary.regs[0x6fa88]=0x80800000;
    const auto secondaryBoot=readDisplay(secondary,true);
    assert(dpSource(secondaryBoot,0,0,nullptr,true) && !dpSource(secondaryBoot,0,0,nullptr,false));
    for(unsigned bit=0;bit<32;++bit) {
        auto changed=secondary;
        changed.regs[0x6fa88]^=1U<<bit;
        assert(!dpSource(readDisplay(changed,true),0,0,nullptr,true));
        assert(!dpSourceUnchanged(changed,secondaryBoot,0,0,0,nullptr,true));
    }
    auto preserved=reinitializeLinear(secondary,secondaryBoot,true,0,GOPProtocol::EDP);
    assert(preserved.status==LinearStatus::Success && preserved.writes);
    for(auto write:secondary.writes)
        assert(write.offset!=0x60404 && write.offset!=0x6fa88);

    unsigned cases=0;
    for(unsigned pipe=0;pipe<3;++pipe)for(unsigned ddi=0;ddi<2;++ddi)for(unsigned lanes:{1U,2U,4U})
    for(unsigned depth:{0U,1U,2U,3U})for(bool enhanced:{false,true})for(unsigned width:{1280U,1920U,2560U,3840U}) {
        auto io=firmware(pipe,ddi,lanes,enhanced,width);io.regs[0x60400+pipe*0x1000]|=depth<<20;auto boot=readDisplay(io,true);const auto before=io.regs;
        LinearPlan plan;
        assert(linearPlan(boot,plan,ddi)==LinearStatus::NotSingleHDMI);
        assert(linearPlan(boot,plan,ddi,GOPProtocol::DP)==LinearStatus::Ready && plan.pipe==pipe);
        auto result=reinitializeLinear(io,boot,true,ddi,GOPProtocol::DP);
        assert(result.status==LinearStatus::Success && result.writes==10 && result.framesAfter>=6);
        assert(dpSourceUnchanged(io,boot,pipe,ddi));
        const uint32_t b=pipe*0x1000;
        for(auto w:io.writes)assert(w.offset==0x42084 || w.offset==0x70028+b || w.offset==0x70180+b ||
            w.offset==0x70188+b || w.offset==0x7018C+b || w.offset==0x70190+b || w.offset==0x701A4+b || w.offset==0x7019C+b);
        for(auto r:before)if(r.first!=0x42084 && r.first!=0x70028+b && r.first!=0x70180+b)
            assert(io.regs[r.first]==r.second);
        ++cases;
    }
    const uint32_t link[] {0x60400,0x60404,0x60540,0x60030,0x60034,0x60040,0x60044,0x70008,
        0x46140,0x64000,0x78000,0x78004,0x45404,0x45454,0x45444,0x42000};
    for(auto reg:link) {
        auto io=firmware(0,0,4,true);auto boot=readDisplay(io,true);io.regs[reg]^=1;
        auto r=reinitializeLinear(io,boot,true,0,GOPProtocol::DP);
        assert(r.status==LinearStatus::StateChanged && io.writes.empty());
        // Loss before first write, after plane stop, and after plane restart.
        // Do not attempt rollback through a changed power/link configuration.
        for(unsigned tick:{1U,3U,5U}) {
            auto io=firmware(0,0,4,true);auto boot=readDisplay(io,true);io.changeReg=reg;io.changeTick=tick;
            auto r=reinitializeLinear(io,boot,true,0,GOPProtocol::DP);
            assert(r.status==LinearStatus::StateChanged && !r.restored && r.writes==(tick==1?0U:tick==3?2U:10U));
        }
    }
    for(unsigned ddi=0;ddi<2;++ddi)for(unsigned pipe=0;pipe<3;++pipe)for(unsigned sr=0;sr<2;++sr)for(unsigned depth:{0U,1U,2U,3U}) {
        auto io=firmware(pipe,ddi,2,true);io.regs[0x60400+pipe*0x1000]|=depth<<20;io.regs[0x60540+0x1000*pipe]|=sr<<6;
        auto boot=readDisplay(io,true);LinearPlan plan;
        assert(dpSource(boot,pipe,ddi)==!sr);
        assert(dpSource(boot,pipe,ddi,nullptr,true));
        assert(linearPlan(boot,plan,ddi,GOPProtocol::EDP)==LinearStatus::Ready);
        auto r=reinitializeLinear(io,boot,true,ddi,GOPProtocol::EDP);
        assert(r.status==LinearStatus::Success && dpSourceUnchanged(io,boot,pipe,ddi,0,nullptr,true));
        io.regs[0x60540+0x1000*pipe]^=64;assert(!dpSourceUnchanged(io,boot,pipe,ddi,0,nullptr,true));
    }
    // Intel TRANS_DDI_BPC: 8/10/6/12 are 0/1/2/3. 4..7 are reserved.
    // This is the link format, independent of the four-byte RGB8 plane.
    for(unsigned depth:{4U,5U,6U,7U}) {
        auto io=firmware(0,0,4,true);io.regs[0x60400]|=depth<<20;
        auto r=reinitializeLinear(io,readDisplay(io,true),true,0,GOPProtocol::EDP);
        assert(r.status==LinearStatus::InvalidDPSource && io.writes.empty());
    }
    auto bad=firmware(0,0,4,true);bad.regs[0x60540]=0x80000000;
    auto r=reinitializeLinear(bad,readDisplay(bad,true),true,0,GOPProtocol::DP);
    assert(r.status==LinearStatus::InvalidDPSource && bad.writes.empty());
    auto stopped=firmware(0,0,4,true);stopped.noFrames=true;
    r=reinitializeLinear(stopped,readDisplay(stopped,true),true,0,GOPProtocol::DP);
    assert(r.status==LinearStatus::NoVblank && stopped.writes.empty());
    auto rollback=firmware(0,0,4,true);const auto boot=readDisplay(rollback,true);rollback.failEnable=true;
    r=reinitializeLinear(rollback,boot,true,0,GOPProtocol::DP);
    assert(r.status==LinearStatus::EnableTimeout && r.restored && dpSourceUnchanged(rollback,boot,0,0));
    auto lost=firmware(0,0,4,true);lost.failEnable=true;lost.changeReg=0x60540;lost.changeTick=7;
    r=reinitializeLinear(lost,readDisplay(lost,true),true,0,GOPProtocol::DP);
    assert(r.status==LinearStatus::StateChanged && !r.restored && r.writes==12);
    printf("PASS DP plane bootstrap: %u route/lane/framing/geometry cases, source/power drift and bounded rollback\n",cases);
}
