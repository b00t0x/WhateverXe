#include "kern_dsi_math.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace XELP::DSI;
using Bytes=std::vector<uint8_t>;
void put(Bytes &b,size_t at,uint32_t value,unsigned n=2) {for(unsigned i=0;i<n;++i)b.at(at+i)=value>>(8*i);}
void add(Bytes &b,uint8_t id,const Bytes &data) {
    b.push_back(id);b.push_back(data.size());b.push_back(data.size()>>8);
    b.insert(b.end(),data.begin(),data.end());
}
Bytes fixture() {
    Bytes b(70);memcpy(b.data(),"$VBT",4);put(b,22,48);put(b,28,48,4);
    memcpy(b.data()+48,"BIOS_DATA_BLOCK ",16);put(b,64,256);put(b,66,22);
    Bytes child(45);child[4]=40;put(child,7,0x1400);child[21]=21;add(b,2,child);
    add(b,40,Bytes{0});
    Bytes config(792);put(config,2,0x11190,4);put(config,6,12);
    config[93]=8;config[94]=50;put(config,98,310);config[103]=110;config[104]=50;put(config,105,200);
    for(unsigned i=0;i<5;++i)put(config,732+2*i,i==4?5000:500);
    add(b,52,config);
    Bytes dtd(30);put(dtd,0,28);put(dtd,2,135255,4);
    const unsigned fields[]={1080,46,22,2,1920,82,40,2,86,154};
    for(unsigned i=0;i<10;++i)put(dtd,6+2*i,fields[i]);add(b,58,dtd);
    Bytes bl(273);bl[0]=6;bl[1]=2;put(bl,2,40000);bl[113]=2;put(bl,129,255);put(bl,193,6);bl[257]=8;add(b,43,bl);
    Bytes seq(10);seq[0]=3;seq[5]=0;
    // Empty, terminated sequences prove structure without firmware commands.
    for(unsigned id:{1U,2U,4U,5U,6U,7U,10U,11U}) {
        seq.push_back(id);seq.push_back(1);seq.insert(seq.end(),4,0);
    }
    seq.push_back(0);put(seq,6,seq.size()-10,4);put(seq,1,seq.size()-5,4);add(b,53,seq);
    put(b,24,b.size());put(b,68,b.size()-48);return b;
}
Profile parse(const Bytes &b,bool quirk=false) {return parseProfile({b.data(),b.size()},quirk);}
void reject(const Bytes &b) {auto r=parse(b);assert(!r.valid());assert(!r.mode.clockKHz&&!r.sequences.packets);}
int main(int argc,char **argv) {
    auto b=fixture();auto r=parse(b);
    if(!r.valid())printf("status %u\n",unsigned(r.status));
    assert(r.valid()&&r.mode.htotal()==1126&&r.mode.vtotal()==2002&&r.pwmHz==40000);
    assert(r.clockPrepare==50&&r.clockPrepareZero==310&&r.hsPrepareZero==200&&r.ppsUs[4]==500000);
    const auto link=linkPlan(r);assert(link.valid&&link.afeKHz==811530&&link.portKHz==162306);
    assert(link.escapeDivider==56&&link.phyEscapeDivider==3);
    // Linux v7.0 readback, reserved bits omitted for the escape divider.
    assert(link.clockTiming==0xC8690000&&link.dataTiming==0x84830083);
    assert(link.htotal==0x04650437&&link.hsync==0x044F044D&&link.vtotal==0x07D1077F&&link.vsync==0x07A907A7);
    const auto pll=pllPlan(link.portKHz,19200);
    assert(pll.valid&&pll.divider==12&&pll.p==2&&pll.q==3&&pll.k==2);
    assert(pll.cfg0()==0x6999FB&&pll.cfg1()==0xE84);
    const auto doubled=pllPlan(link.portKHz,38400);
    assert(doubled.valid&&doubled.integer==pll.integer&&doubled.fraction==(pll.fraction+1)/2);
    assert(doubled.cfg0()==0x34CDFB&&doubled.cfg1()==0xE84); // saved DPLL0, 38.4 MHz + WA
    assert(pllAFE(doubled.cfg0(),doubled.cfg1(),38400)==811529);
    assert(pllAFE(pll.cfg0(),pll.cfg1(),19200)==811529);
    assert(!pllAFE(doubled.cfg0(),0,38400)&&!pllAFE(doubled.cfg0(),0xE84,27000));
    assert(!pllAFE(doubled.cfg0()|1U<<29,0xE84,38400)); // SSC outside supported profile
    assert(!pllPlan(0,19200).valid&&!pllPlan(UINT32_MAX,19200).valid&&!pllPlan(162306,27000).valid);
    for(unsigned clock=10000;clock<500000;clock+=137) {
        auto v=pllPlan(clock,24000);if(!v.valid)continue;
        assert(v.divider==v.p*v.q*v.k&&v.dcoKHz>=7998000&&v.dcoKHz<=10000000);
        const auto reconstructed=(uint64_t(v.integer)*32768+v.fraction)*24000/32768;
        assert(reconstructed<=v.dcoKHz&&v.dcoKHz-reconstructed<2);
    }
    for(size_t i=0;i<b.size();++i)reject(Bytes(b.begin(),b.begin()+i));
    auto bad=b;put(bad,64,257);reject(bad);
    bad=b;put(bad,68,b.size()-49);reject(bad);assert(parse(bad,true).valid()&&parse(bad,true).bdbTailQuirk);
    bad.back()=1;assert(!parse(bad,true).valid());
    bad=b;put(bad,68,b.size()-50);assert(!parse(bad,true).valid());
    // Reject each independently invalid interpretation, not merely checksums.
    for(size_t at=70;at<b.size();) {
        const auto id=b[at];const size_t n=profileU16(b.data()+at+1),p=at+3;
        bad=b;
        if(id==2){bad[p+21]=22;reject(bad);}
        if(id==40){bad[p]=6;reject(bad);}
        if(id==52){bad[p+6]=8;reject(bad);bad=b;put(bad,p+98,49);reject(bad);}
        if(id==58){put(bad,p+8,1);reject(bad);}
        if(id==43){bad[p+257]=16;reject(bad);}
        // Duplicate required blocks reject before publication.
        Bytes duplicate(b.begin()+at,b.begin()+at+3+n);bad=b;bad.insert(bad.end(),duplicate.begin(),duplicate.end());
        put(bad,24,bad.size());put(bad,68,bad.size()-48);reject(bad);
        at+=3+n;
    }
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);assert(f);Bytes real{std::istreambuf_iterator<char>(f),{}};
        assert(!parse(real).valid());auto q=parse(real,true);
        printf("saved status=%u\n",unsigned(q.status));
        assert(q.valid()&&q.bdbTailQuirk&&q.panel==0&&q.lanes==4);
        assert(q.mode.clockKHz==135255&&q.mode.width==1080&&q.mode.height==1920);
        assert(q.sequences.packets==359&&q.sequences.gpios==8&&q.sequences.delayUs==486000);
        assert(q.clockPrepare==50&&q.hsPrepare==50&&q.clockPrepareZero==310&&q.hsPrepareZero==200);
        assert(q.ppsUs[0]==50000&&q.ppsUs[4]==500000&&q.minimum==6&&q.pwmHz==40000);
        const auto saved=linkPlan(q);assert(saved.valid&&saved.clockTiming==link.clockTiming&&saved.dataTiming==link.dataTiming);
        printf("saved profile: %ux%u clock=%u kHz PWM=%u Hz\n",q.mode.width,q.mode.height,q.mode.clockKHz,q.pwmHz);
    }
    puts("DSI profile bounds, explicit tail quirk, supported transport: PASS");
}
