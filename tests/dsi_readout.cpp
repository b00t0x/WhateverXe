#include "kern_dsi_readout.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <fstream>
#include <vector>
using namespace XELP::DSI;
using Registers=std::map<uint32_t,uint32_t>;
Readout decode(const Registers &regs,uint32_t ref=38400) {
    return decodeReadout([&](uint32_t a,uint32_t &v){auto i=regs.find(a);if(i==regs.end())return false;v=i->second;return true;},ref);
}
Registers firmware() {
    // Numeric register facts from a DSI0/pipeA/PLL1 GOP; no firmware code.
    return {{0x7B008,0xC0000000},{0x6B400,0x80030006},{0x45404,0xFC0F},{0x70008,0},
        {0x6B030,0x20130300},{0x6B094,1},{0x6B404,0},{0x6B034,0},{0x64000,0x80000000},{0x164280,0x1E07801},
        {0x46014,0xCC000000},{0x16428C,0x3001FB},{0x164290,0xE84},
        {0x6B000,0x04650437},{0x6B008,0x044F044D},{0x6B00C,0x07D1077F},{0x6B014,0x07A907A7}};
}
int main(int argc,char **argv) {
    auto regs=firmware();auto r=decode(regs);
    assert(r.complete()&&r.identity.valid()&&r.identity.pipe==0&&r.identity.transcoder==0&&r.identity.phy==0&&r.identity.pll==1);
    assert(r.normalConf==0&&r.activePipes()==1&&r.afeKHz==811500);
    struct IO {
        Registers regs;std::vector<uint32_t> reads;
        bool readable(uint32_t reg){return regs.count(reg);}
        uint32_t read(uint32_t reg){reads.push_back(reg);return regs.at(reg);}
    } io{regs,{}};
    io.regs[0x51004]=0x40000000;
    assert(sourceUnchanged(io,r));
    io.regs[0x164280]^=0x808;assert(sourceUnchanged(io,r)); // peer B's PLL/gate only
    io.regs[0x164280]^=1;assert(!sourceUnchanged(io,r));
    io.regs[0x164280]=r.mux;io.regs[0x51004]=0;assert(!sourceUnchanged(io,r));
    io.regs[0x51004]=0x40000000;io.regs[0x45404]=0;io.reads.clear();
    assert(!sourceUnchanged(io,r) && io.reads==std::vector<uint32_t>{0x45404});
    Profile profile;profile.status=ProfileStatus::Valid;profile.lanes=4;
    profile.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
    assert(r.matches(profile)&&!r.matches(profile,1));
    for(const auto &entry:regs) {
        auto missing=regs;missing.erase(entry.first);assert(!decode(missing).complete());
        auto absent=regs;absent[entry.first]=UINT32_MAX;assert(!decode(absent).complete());
    }
    auto linux=regs;linux[0x164280]=0x1E06C04;linux[0x46010]=0xCC000000;
    linux[0x164284]=0x34CDFB;linux[0x164288]=0xE84;linux[0x6B030]=0x20130304;
    auto l=decode(linux);assert(l.complete()&&l.identity.pll==0&&l.afeKHz==811529&&l.matches(profile)&&l.ddiClockGated());
    // DSI0 may consume pipe B; neither its register bank nor PHY follows pipe.
    auto b=regs;b[0x6B400]|=5<<12;b.erase(0x70008);b[0x71008]=0;
    auto pipeB=decode(b);assert(pipeB.complete()&&pipeB.activePipes()==2&&pipeB.identity.pipe==1&&pipeB.identity.pll==1);
    b[0x71008]=0xC0000000;assert(decode(b).status==ReadoutStatus::Routing);
    for(auto pair:Registers{{0x7B008,0},{0x6B400,0x80034006},{0x6B094,0},{0x6B404,1},
        {0x6B034,1},{0x45404,1},{0x164280,0x1E07803},{0x46014,0x80000000},{0x64000,0},
        {0x6B000,0x04370465},{0x6B400,0x80130006}}) {
        auto bad=regs;bad[pair.first]=pair.second;assert(!decode(bad).complete());
    }
    auto transition=regs;transition[0x7B008]=0x80000000;assert(decode(transition).status==ReadoutStatus::Transition);
    assert(!decode(regs,27000).complete());
    if(argc==2) {
        std::ifstream f(argv[1]);assert(f);Registers saved;uint32_t a,v;
        while(f>>std::hex>>a>>v){assert(saved.emplace(a,v).second);}assert(f.eof());
        auto actual=decode(saved);assert(actual.complete()&&actual.matches(profile));
        printf("saved DSI snapshot: pipe=%u transcoder=%u PHY=%u PLL=%u AFE=%u kHz normalCONF=%x\n",
            actual.identity.pipe,actual.identity.transcoder,actual.identity.phy,actual.identity.pll,actual.afeKHz,actual.normalConf);
    }
    puts("DSI snapshot: separate pipe/transcoder/PHY/PLL, bounded clocks, missing/conflicting state: PASS");
}
