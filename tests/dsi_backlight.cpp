#include "kern_dsi_backlight.hpp"
#include <cassert>
#include <cstdio>
#include <map>
using namespace XELP::DSI;
struct IO {
    bool owned=true,drop=false;unsigned writes=0;
    std::map<uint32_t,uint32_t> regs{{Backlight::Control,0x80000000},{Backlight::Period,480},{Backlight::Duty,154}};
    bool ready(){return owned;}
    bool readable(uint32_t reg){return regs.count(reg);}
    uint32_t read(uint32_t reg){return regs.at(reg);}
    void write(uint32_t reg,uint32_t value){assert(reg==Backlight::Duty && owned);++writes;if(!drop)regs[reg]=value;}
};
int main() {
    Profile p;p.status=ProfileStatus::Valid;p.precision=8;p.minimum=6;
    Backlight b;IO io;
    assert(b.capture(p,0x80000000,480,154) && b.requested==21026 && b.minimum==11);
    assert(!b.capture(p,0x80000000,480,154));
    for(auto value:{0U,1U,32768U,65535U}) {
        assert(b.request(value));const auto before=io.writes;
        // Deferred requests have no IO dependency and must not relight the panel.
        assert(io.writes==before);assert(b.apply(io));
        assert(b.applied==(value==0?0U:value==1?11U:value==32768?240U:480U));
    }
    const auto requested=b.requested;
    assert(!b.request(65536) && !b.request(0x100000001ULL) && b.requested==requested);
    io.owned=false;auto writes=io.writes;assert(!b.apply(io) && io.writes==writes && !b.faulted);
    io.owned=true;io.regs[Backlight::Period]=481;
    assert(!b.apply(io) && b.faulted && io.writes==writes && !b.request(123));
    Backlight dropped;IO bad;assert(dropped.capture(p,0x80000000,480,154));
    assert(dropped.request(0));bad.drop=true;
    assert(!dropped.apply(bad) && dropped.faulted && dropped.applied==154);
    for(unsigned which=0;which<5;++which) {
        Backlight invalid;auto profile=p;
        if(which==0)profile.controller=1;if(which==1)profile.precision=16;
        assert(!invalid.capture(profile,which==2?0:0x80000000,which==3?0:480,which==4?481:154));
    }
    Backlight wide;assert(wide.capture(p,0x80000000,1000000,500000));
    assert(wide.request(65535) && wide.target()==1000000);
    puts("PASS DSI backlight: Apple range, VBT floor, zero duty, deferred request, 64-bit scaling, ownership/readback failure");
}
