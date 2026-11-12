#include "kern_xelp_gt_resources.hpp"
#include "kern_display13_firmware.hpp"
#include "kern_display13_fb_topology.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "gen12_resource_cases.hpp"
using namespace XELP;
int main() {
    // Hardware captures: 46A6, six DSS, 96 EU, eight L3 banks, VCS0/2.
    uint32_t f[]={1,0x3f,0,0,0xe00fa};GTResources r;
    assert(decodeADLPGT(f,r) && r.dss==6 && r.nativeSubslices==12 && r.eus==96 && r.l3Banks==8 && r.urbKiB==512);
    assert(!decodeSmallGT(f,r));
    ResourceCases::check(decodeADLPGT);
    assert(!decodeADLPGT(nullptr,r));
    for(unsigned i=0;i<5;++i){auto save=f[i];f[i]=~0U;assert(!decodeADLPGT(f,r));f[i]=save;}
    for(unsigned bad:{0U,2U,3U}){f[0]=bad;assert(!decodeADLPGT(f,r));}f[0]=1;
    f[4]=0xfffff;assert(!decodeADLPGT(f,r));
    VBT::Ports ports;ports.status=VBT::Status::Valid;ports.present=0x3a;
    ports.port[1]={0x68c6,7,0,0x10,0xc0,0};
    ports.port[3]={0x60d2,13,3,0,0xc0,3};
    ports.port[4]={0x60d2,15,4,0,0xc0,3};
    ports.port[5]={0x60d6,17,5,0x80,0xc0,3};
    auto w=VBT::display13HDMI(ports,0x7b);
    assert(w.described() && w.hdmi==0x18 && w.ddc[3]==9 && w.ddc[4]==10);
    assert(!ports.hdmi(3).hdmi); // Existing Combo policy is unchanged.
    assert(Display13FBTopology::xeLPDWiring(w));
    assert(!Display13FBTopology::supportedWiring(w)); // Runtime stays closed until hooks are installed.
    assert(identify(0x46a68086,12).admitted());
    for(unsigned d=3;d<7;++d) {
        assert(VBT::ddi(Family::AlderLakeP,13+2*(d-3))==d);
        auto p=ports;p.present=1U<<d;p.port[d]={0x60d2,13+2*(d-3),d,0,0xc0,3};
        auto v=VBT::display13HDMI(p,0x7b);assert(v.described() && v.ddc[d]==d+6);
        alignas(uint64_t) uint8_t native[0x1c0] {},out[0x1c0] {};native[0x12]=9;
        for(unsigned k=0;k<9;++k) {
            uint32_t rec[]={k,k<3?k+1:k+6,1,k>=3?1U:0U,0x400,0x201};
            const uint32_t panel[]={0,0,0,0,2,0x18};
            memcpy(native+0x20+24*k,k?rec:panel,24);
        }
        assert(Display13FBTopology::apply(out,sizeof(out),native,v));
        const auto c=reinterpret_cast<uint32_t *>(out+0x20+24*d);
        assert(v.matchesHDMI(c) && c[3]==1 && c[1]==d+6);
        assert(Display13FBTopology::boundPorts(out,v).selected==(1U<<d));
        assert(Display13FBTopology::excludedHotplug(out,sizeof(out),v)==((0x3f000000&~(1U<<(21+d)))|0x70000));
        assert(Display13FBTopology::bootDDI(v,0x80030100|((d+1)<<27))==d);
        for(unsigned flag:{1U,2U,4U,0x10U}) {p.port[d].flags|=flag;assert(!VBT::display13HDMI(p,0x7b).hdmi);p.port[d].flags=0xc0;}
        p.port[d].type|=4;assert(!VBT::display13HDMI(p,0x7b).hdmi);
    }
    auto duplicate=ports;duplicate.port[4].ddc=3;assert(!VBT::display13HDMI(duplicate,0x7b).valid);
    puts("PASS ADLP fuse resources, TC HDMI firmware wiring and native topology; physical family selection");
}
