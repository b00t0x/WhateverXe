#include "kern_dsi_apple_timing.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace XELP::DSI;
int main() {
    Profile p;p.status=ProfileStatus::Valid;p.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
    AppleAdapter::Timing t;assert(AppleAdapter::makeTiming(p,t));
    assert(t.pixelClock==135255000&&t.minPixelClock==135241474&&t.maxPixelClock==135268526);
    assert(t.horizontalActive==1080&&t.horizontalBlanking==46&&t.horizontalSyncOffset==22&&t.horizontalSyncWidth==2);
    assert(t.verticalActive==1920&&t.verticalBlanking==82&&t.verticalSyncOffset==40&&t.verticalSyncWidth==2);
    assert(t.bitsPerColorComponent==2&&t.pixelEncoding==1&&t.numLinks==1);
    assert(!t.scalerFlags&&!t.horizontalScaled&&!t.verticalScaled&&!t.verticalBlankingExtension);
    auto offered=t;offered.reservedA[0]=0x80001000;offered.signalLevels=1;
    offered.pixelClock=offered.minPixelClock=offered.maxPixelClock=135260000;
    assert(AppleAdapter::clientTiming(t,offered,0));
    for(unsigned offset=4;offset<sizeof(offered);++offset) {
        auto corrupt=offered;reinterpret_cast<uint8_t*>(&corrupt)[offset]^=0x80;
        assert(!AppleAdapter::clientTiming(t,corrupt,0));
    }
    assert(AppleAdapter::clientTiming(t,offered,1)); // IDs are opaque, not array positions.
    assert(!AppleAdapter::clientTiming(t,offered,1024));
    for(uint32_t id:{0x80002000U,0x80003000U,0x80005007U}) {
        offered.reservedA[0]=id;assert(AppleAdapter::clientTiming(t,offered,0));
    }
    for(uint32_t id:{0U,AppleAdapter::FixedModeID,0x7FFFFFFFU,UINT32_MAX}) {
        offered.reservedA[0]=id;assert(!AppleAdapter::clientTiming(t,offered,0));
    }
    offered.reservedA[0]=0x80001001;assert(AppleAdapter::clientTiming(t,offered,1));
    assert(!AppleAdapter::acceptsMode(0x80001002,0x80001001));
    uint8_t edid[128];assert(AppleAdapter::makePanelEDID(p.mode,edid));
    unsigned sum=0;for(auto b:edid)sum+=b;assert(!(sum&255));
    assert(edid[18]==1 && edid[19]==4 && edid[20]==0xA0 && edid[24]==2 && !edid[126]);
    const auto d=edid+54;
    assert((unsigned(d[0])+(unsigned(d[1])<<8))*10==135260);
    assert((d[2]|((d[4]&0xF0)<<4))==1080 && (d[3]|((d[4]&15)<<8))==46);
    assert((d[5]|((d[7]&0xF0)<<4))==1920 && (d[6]|((d[7]&15)<<8))==82);
    assert((d[8]|((d[11]>>6)<<8))==22 && (d[9]|(((d[11]>>4)&3)<<8))==2);
    assert(((d[10]>>4)|(((d[11]>>2)&3)<<4))==40 && ((d[10]&15)|((d[11]&3)<<4))==2);
    assert(d[12]==86 && d[13]==154 && !d[14] && d[17]==0x18);
    for(unsigned i=35;i<38;i++)assert(!edid[i]);for(unsigned i=38;i<54;i++)assert(edid[i]==1);
    assert(edid[75]==0xFC && !memcmp(edid+77,"WEX DSI VBT",11));
    auto bad=p.mode;bad.width=4096;assert(!AppleAdapter::makePanelEDID(bad,edid));
    bad=p.mode;bad.vfront=64;assert(!AppleAdapter::makePanelEDID(bad,edid));
    bad=p.mode;bad.clockKHz=10001;assert(AppleAdapter::makePanelEDID(bad,edid));
    bad.clockKHz=10005;assert(!AppleAdapter::makePanelEDID(bad,edid));
    p.mode.hsync=46;assert(!AppleAdapter::makeTiming(p,t)&&!t.pixelClock);
    p={};assert(!AppleAdapter::makeTiming(p,t));
    puts("DSI fixed Apple timing: Hz/range, portrait geometry, RGB8 bit fields, no scaling/DSC: PASS");
}
