#include "kern_display13_firmware.hpp"
#include "kern_xelp_probe.hpp"
#include <cassert>
#include <map>
#include <cstdio>
struct IO {
    std::map<uint32_t,uint32_t> regs;
    bool readable(uint32_t r){return regs.count(r);}
    uint32_t read(uint32_t r){assert(readable(r));return regs[r];}
};
int main() {
    for(unsigned d=3;d<=6;++d) {
        XELP::VBT::Ports ports;ports.status=XELP::VBT::Status::Valid;ports.present=2|(1U<<d);
        ports.port[1]={0x60d2,1,2,0,0,0};
        ports.port[d]={0x68c6,13+2*(d-3),0,(d+3)*0x10,0xc0,3};
        auto w=XELP::VBT::display13Fixed(ports,Display13OutputWiring::Physical);
        assert(w.described() && w.hdmi==2 && w.dp==(1U<<d) && w.typeC==w.dp);
        auto bad=ports;bad.port[d].capabilities=2;
        assert(!XELP::VBT::display13Fixed(bad,Display13OutputWiring::Physical).dp);
        bad=ports;bad.port[d].aux=0x40;
        assert(!XELP::VBT::display13Fixed(bad,Display13OutputWiring::Physical).dp);
        XELP::ProbeConfig c;c.profile=XELP::identify(0x46d38086,0);c.combo=c.comboBackend=2;
        c.typeC=1U<<d;c.legacyBackend=0x78;assert(c.valid());
        XELP::ProbeBinding b{1,2,0x1000,c};const uint32_t record[]={d,d+6,1,1,0x400,0x201};
        IO io;io.regs={{0xc4000,0},{0x44470,1U<<(16+d-3)}};
        assert(XELP::probeMode(io,b,2,0x1020+24*d,record)==3);
        // Before ownership no FIA read or ready polling is needed to classify.
        assert(io.regs.size()==2);
        io.regs[0x44470]|=1U<<(d-3);assert(!XELP::probeMode(io,b,2,0x1020+24*d,record));
        io.regs[0x44470]=UINT32_MAX;assert(!XELP::probeMode(io,b,2,0x1020+24*d,record));
        io.regs[0x44470]=1U<<(16+d-3);io.regs[0xc4000]=1U<<(24+d-3);
        assert(!XELP::probeMode(io,b,2,0x1020+24*d,record));
        io.regs[0xc4000]=0;assert(!XELP::probeMode(io,b,3,0x1020+24*d,record));
    }
    puts("PASS Display13 DP-only USB-C wiring, separate live HPD and immutable provider binding");
}
