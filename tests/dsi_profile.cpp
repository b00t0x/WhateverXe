#include "kern_dsi_restart.hpp"
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
Bytes fixture(unsigned version=256,bool inlineReset=false,unsigned panel=0,unsigned stride=28,unsigned childStride=0,unsigned mask=0xCFE) {
    Bytes b(70);memcpy(b.data(),"$VBT",4);put(b,22,48);put(b,28,48,4);
    memcpy(b.data()+48,"BIOS_DATA_BLOCK ",16);put(b,64,version);put(b,66,22);
    Bytes child(5+(childStride?childStride:version<256?39:40));child[4]=child.size()-5;put(child,7,0x1400);child[21]=21;add(b,2,child);
    add(b,40,Bytes{uint8_t(panel)});
    Bytes config(792);put(config,2,0x11190,4);put(config,6,12);
    config[93]=8;config[94]=50;put(config,98,310);config[103]=110;config[104]=50;put(config,105,200);
    for(unsigned i=0;i<5;++i)put(config,732+2*i,i==4?5000:500);
    if(panel) {
        for(unsigned i=0;i<122;++i)config[122*panel+i]=config[i];
        for(unsigned i=0;i<10;++i)config[732+10*panel+i]=config[732+i];
    }
    add(b,52,config);
    Bytes dtd(2+stride*(panel+1),0xA5);put(dtd,0,stride);const auto t=2+stride*panel;put(dtd,t,135255,4);
    const unsigned fields[]={1080,46,22,2,1920,82,40,2,86,154};
    for(unsigned i=0;i<10;++i)put(dtd,t+4+2*i,fields[i]);dtd[t+24]=0xC0;add(b,58,dtd);
    Bytes bl(273,0xA5);bl[0]=6;bl[1+6*panel]=2;put(bl,2+6*panel,40000);
    bl[113+panel]=2;put(bl,129+4*panel,255);put(bl,193+4*panel,6);bl[257+panel]=8;add(b,43,bl);
    Bytes seq(10);seq[0]=3;seq[5]=panel;
    // Empty, terminated sequences prove structure without firmware commands.
    for(unsigned id:{1U,2U,4U,5U,6U,7U,10U,11U}) {
        if(!(mask&(1U<<id)) || (inlineReset && (id==1 || id==5)))continue;
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
    assert(r.mode.hsyncPositive && r.mode.vsyncPositive);
    assert(r.clockPrepare==50&&r.clockPrepareZero==310&&r.hsPrepareZero==200&&r.ppsUs[4]==500000);
    for(unsigned version=236;version<=260;++version) {
        auto compatible=fixture(version,true);auto p=parse(compatible);
        assert(p.valid() && p.version==version && !p.sequences.sequence[1].data && !p.sequences.sequence[5].data);
    }
    reject(fixture(235));
    reject(fixture(256,false,0,28,32));
    for(unsigned stride:{33U,38U,39U,40U,80U,255U})assert(parse(fixture(300,false,0,28,stride)).valid());
    for(unsigned panel=0;panel<6;++panel)for(unsigned stride:{28U,32U,64U}) {
        auto bytes=fixture(300,false,panel,stride,80);auto p=parse(bytes);
        assert(p.valid() && p.panel==panel && p.brightness==255 && p.minimum==6);
        assert(p.mode.clockKHz==135255 && p.mode.width==1080 && p.ppsUs[4]==500000);
        for(size_t at=70;at<bytes.size();at+=3+profileU16(bytes.data()+at+1))if(bytes[at]==58) {
            auto bad=bytes;put(bad,at+3,27);reject(bad);
            bad=bytes;put(bad,at+3,stride+1);reject(bad); // partial or missing selected entry
        }
    }
    for(unsigned omitted:{2U,4U,6U,7U,10U,11U})assert(parse(fixture(256,false,0,28,0,0xCFE&~(1U<<omitted))).valid());
    assert(parse(fixture(256,false,0,28,0,0)).valid()); // selected panel, no operations
    for(unsigned clock:{350000U,450000U,750000U}) {
        auto p=r;p.mode.clockKHz=clock;const auto plan=Restart::plan(p,38400);
        assert(plan.valid && plan.link.afeKHz==clock*6);
        const auto actual=pllAFE(plan.pll.cfg0(),plan.pll.cfg1(),38400);
        assert(actual<=clock*6 && clock*6-actual<10);
    }
    for(unsigned clock:{1U,1000000U,715827883U,UINT32_MAX}) {
        auto p=r;p.mode.clockKHz=clock;assert(!Restart::plan(p,38400).valid);
    }
    {auto p=r;p.mode.clockKHz=2000000;assert(!linkPlan(p).valid);} // nine-bit divider
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
    auto bad=b;put(bad,64,235);reject(bad);
    bad=b;put(bad,68,b.size()-49);reject(bad);assert(parse(bad,true).valid()&&parse(bad,true).bdbTailQuirk);
    bad.back()=1;assert(!parse(bad,true).valid());
    bad=b;put(bad,68,b.size()-50);assert(!parse(bad,true).valid());
    // Reject each independently invalid interpretation, not merely checksums.
    for(size_t at=70;at<b.size();) {
        const auto id=b[at];const size_t n=profileU16(b.data()+at+1),p=at+3;
        bad=b;
        if(id==2){bad[p+21]=22;reject(bad);}
        if(id==40){bad[p]=6;reject(bad);}
        if(id==52){
            for(unsigned turn=0;turn<4;++turn){auto oriented=b;put(oriented,p+2,0x11190|(turn<<14),4);
                auto profile=parse(oriented);assert(profile.valid() && profile.orientationQuarterTurns==turn);
                auto plan=linkPlan(profile);assert(plan.valid && plan.htotal==link.htotal && plan.vtotal==link.vtotal && plan.afeKHz==link.afeKHz);}
            bad[p+6]=8;reject(bad);bad=b;put(bad,p+98,49);reject(bad);
        }
        if(id==58){put(bad,p+8,1);reject(bad);}
        if(id==43){bad[p+257]=16;reject(bad);}
        if(id==53){
            auto absent=b;absent.erase(absent.begin()+at,absent.begin()+at+3+n);
            put(absent,24,absent.size());put(absent,68,absent.size()-48);
            auto p=parse(absent);assert(p.valid() && p.sequences.valid() && !p.sequences.packets);
            bad=b;bad[at+3]=4;reject(bad); // present but unsupported is not absent
            bad=b;bad[at+8]=1;reject(bad); // inconsistent selected panel still rejects
        }
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
