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
    {
        auto fixed=t;fixed.pixelClock=fixed.minPixelClock=fixed.maxPixelClock=165920000;
        auto os=fixed;os.numLinks=2;os.reservedA[0]=0x8000300E;
        assert(AppleAdapter::clientTiming(fixed,AppleAdapter::panelOffer(os,fixed),0));
        os.horizontalBlanking++;
        assert(!AppleAdapter::clientTiming(fixed,AppleAdapter::panelOffer(os,fixed),0));
    }
    for(unsigned offset=4;offset<sizeof(offered);++offset) {
        auto corrupt=offered;reinterpret_cast<uint8_t*>(&corrupt)[offset]^=0x80;
        assert(!AppleAdapter::clientTiming(t,corrupt,0));
    }
    auto exact=offered;exact.pixelClock=exact.minPixelClock=exact.maxPixelClock=t.pixelClock;
    assert(AppleAdapter::clientTiming(t,exact,1));
    exact.minPixelClock--;assert(!AppleAdapter::clientTiming(t,exact,1));
    assert(AppleAdapter::clientTiming(t,offered,1)); // IDs are opaque, not array positions.
    assert(AppleAdapter::clientTiming(t,offered,1024));
    for(uint32_t id:{0x80002000U,0x80003000U,0x80005007U}) {
        offered.reservedA[0]=id;assert(AppleAdapter::clientTiming(t,offered,0));
    }
    for(uint32_t id:{0U,AppleAdapter::FixedModeID,0x7FFFFFFFU,UINT32_MAX}) {
        offered.reservedA[0]=id;assert(!AppleAdapter::clientTiming(t,offered,0));
    }
    offered.reservedA[0]=0x80001001;assert(AppleAdapter::clientTiming(t,offered,1));
    for(unsigned rotation:{0x30U,0x50U}) {
        auto r=offered;r.scalerFlags=rotation;
        assert(AppleAdapter::presentationTiming(t,r,1));
        r.horizontalScaled=t.verticalActive;r.verticalScaled=t.horizontalActive;
        assert(AppleAdapter::presentationTiming(t,r,1));
        r.horizontalScaled=2880;r.verticalScaled=1620;
        assert(AppleAdapter::presentationTiming(t,r,1));
        AppleAdapter::ModeInformation info;info.width=2880;info.height=1620;info.flags=0x02000004;
        assert(AppleAdapter::annotateMode(p.mode,info,true,2880,1620));
        assert(!(info.flags&0x02000004) && info.widthMM==154 && info.heightMM==86);
        r.horizontalScaled=8192;assert(AppleAdapter::presentationTiming(t,r,1));
        r.horizontalScaled=2880;r.verticalScaled=0;assert(AppleAdapter::presentationTiming(t,r,1));
        r=offered;r.scalerFlags=rotation;r.horizontalActive=1920;r.verticalActive=1080;
        assert(!AppleAdapter::presentationTiming(t,r,1));
    }
    auto portrait=offered;portrait.horizontalScaled=1620;portrait.verticalScaled=2880;
    assert(AppleAdapter::presentationTiming(t,portrait,0));
    portrait.scalerFlags=1;assert(AppleAdapter::presentationTiming(t,portrait,0));
    portrait.scalerFlags=0x20;portrait.horizontalInset=4;assert(AppleAdapter::presentationTiming(t,portrait,0));
    assert(!AppleAdapter::acceptsMode(0x80001002,0x80001001));
    {auto mode=p.mode;mode.clockKHz=750000;uint8_t base[128],ext[128];
     assert(AppleAdapter::makePanelEDID(mode,base) && base[126]==1);
     assert(AppleAdapter::makePanelEDID(mode,ext,2));
     assert((uint32_t(ext[8])|(uint32_t(ext[9])<<8)|(uint32_t(ext[10])<<16))==74999);
     for(auto bytes:{base,ext}){unsigned sum=0;for(unsigned i=0;i<128;++i)sum+=bytes[i];assert(!(sum&255));}
     mode.clockKHz=UINT32_MAX;assert(!AppleAdapter::makePanelEDID(mode,base));}
    uint8_t edid[128];assert(AppleAdapter::makePanelEDID(p.mode,edid));
    unsigned sum=0;for(auto b:edid)sum+=b;assert(!(sum&255));
    assert(edid[16]==0xFF && edid[17]>=17); // Model year enables native linear brightness.
    assert(edid[18]==1 && edid[19]==4 && edid[20]==0xA0 && edid[24]==2 && !edid[126]);
    const auto d=edid+54;
    assert((unsigned(d[0])+(unsigned(d[1])<<8))*10==135260);
    assert((d[2]|((d[4]&0xF0)<<4))==1080 && (d[3]|((d[4]&15)<<8))==46);
    assert((d[5]|((d[7]&0xF0)<<4))==1920 && (d[6]|((d[7]&15)<<8))==82);
    assert((d[8]|((d[11]>>6)<<8))==22 && (d[9]|(((d[11]>>4)&3)<<8))==2);
    assert(((d[10]>>4)|(((d[11]>>2)&3)<<4))==40 && ((d[10]&15)|((d[11]&3)<<4))==2);
    assert(d[12]==86 && d[13]==154 && !d[14] && d[17]==0x18);
    for(unsigned i=35;i<38;i++)assert(!edid[i]);for(unsigned i=38;i<54;i++)assert(edid[i]==1);
    assert(edid[75]==0xFC && !memcmp(edid+77,"MIPI DSI\n    ",13));
    auto wide=p.mode;wide.width=4096;assert(AppleAdapter::makePanelEDID(wide,edid) && edid[126]==1);
    auto tall=p.mode;tall.vfront=64;assert(AppleAdapter::makePanelEDID(tall,edid) && edid[126]==1);
    Mode extended{136887,1200,68,40,12,1920,239,205,2,141,226};
    assert(AppleAdapter::makePanelEDID(extended,edid) && edid[126]==1 && !edid[54] && edid[57]==0x10);
    sum=0;for(auto b:edid)sum+=b;assert(!(sum&255));
    uint8_t ext[128];assert(AppleAdapter::makePanelEDID(extended,ext,2));
    sum=0;for(auto b:ext)sum+=b;assert(!(sum&255));
    sum=0;for(unsigned i=1;i<=28;++i)sum+=ext[i];assert(!(sum&255));
    assert(ext[0]==0x70 && ext[1]==0x12 && ext[2]==23 && ext[5]==3 && ext[7]==20);
    auto u16=[](const uint8_t *b){return unsigned(b[0])|(unsigned(b[1])<<8);};
    const auto e=ext+8;
    assert((1+e[0]+(unsigned(e[1])<<8)+(unsigned(e[2])<<16))*10==136890 && e[3]==0x80);
    const unsigned expected[]={1200,68,40,12,1920,239,205,2};
    for(unsigned i=0;i<8;++i)assert(u16(e+4+2*i)+1==expected[i]);
    assert(!AppleAdapter::makePanelEDID(p.mode,ext,2) && !AppleAdapter::makePanelEDID(extended,ext,3));
    auto bad=extended;bad.vfront=32769;bad.vblank=40000;assert(!AppleAdapter::makePanelEDID(bad,edid));
    bad=p.mode;bad.clockKHz=10001;assert(AppleAdapter::makePanelEDID(bad,edid));
    bad.clockKHz=10005;assert(!AppleAdapter::makePanelEDID(bad,edid));
    p.mode.hsync=46;assert(!AppleAdapter::makeTiming(p,t)&&!t.pixelClock);
    p={};assert(!AppleAdapter::makeTiming(p,t));
    puts("DSI fixed Apple timing: Hz/range, portrait geometry, RGB8 bit fields, no scaling/DSC: PASS");
}
