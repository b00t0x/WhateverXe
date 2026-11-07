#include "kern_display13_fb_topology.hpp"
#include <assert.h>
#include <stdio.h>
using namespace Display13FBTopology;
struct Platform {
    uint64_t words[Bytes/8] {};
    uint8_t *data() {return reinterpret_cast<uint8_t *>(words);}
    uint32_t *config(unsigned d) {return reinterpret_cast<uint32_t *>(data()+0x20+24*d);}
    Platform() {
        data()[0x12]=Ports;data()[0x13]=3;
        const uint32_t panel[] {0,0,0,0,2,0x18};memcpy(config(0),panel,24);
        for(unsigned d=1;d<Ports;++d) {
            const uint32_t record[] {d,d+1,1,0,0x400,0x201};memcpy(config(d),record,24);
        }
        words[0]=0x123456789A490000ULL;words[0x108/8]=0x123456789ABCDEF0ULL;
    }
};
int main() {
    const uint32_t property[] {1,2,0,2,0};const auto wiring=TGLHDMIWiring::fromProperty(property,sizeof(property));
    Platform input,output;const auto original=input;
    assert(apply(output.data(),Bytes,input.data(),wiring));
    assert(!memcmp(input.data(),original.words,Bytes));
    for(unsigned d=0;d<Ports;++d) {
        assert(output.config(d)[0]==d && bound(output.data(),output.config(d),d));
        assert(selected(output.data(),output.config(d),wiring)==(d==1));
        if(d!=1)assert(output.config(d)[4]==1 && !output.config(d)[5]);
    }
    assert(output.config(1)[1]==2 && output.config(1)[2]==2 && output.config(1)[4]==0x800 && output.config(1)[5]==0x202);
    assert(excludedTypeC(output.data(),Bytes,wiring)==0x3F000000);
    auto activeTC=output;activeTC.config(3)[4]=0x400;
    assert(!excludedTypeC(activeTC.data(),Bytes,wiring));
    activeTC=output;activeTC.config(8)[5]=0x201;
    assert(!excludedTypeC(activeTC.data(),Bytes,wiring));
    assert(!excludedTypeC(output.data(),Bytes-1,wiring));
    // The selected record is the only changed route; keep counts/pool/config.
    assert(!memcmp(output.data(),input.data(),0x20));
    assert(!memcmp(output.data()+0xF8,input.data()+0xF8,Bytes-0xF8));
    assert(!selected(output.data(),input.config(1),wiring));
    auto bad=input;bad.config(8)[0]=7;auto sentinel=output;
    assert(!apply(output.data(),Bytes,bad.data(),wiring) && !memcmp(output.data(),sentinel.data(),Bytes));
    bad=input;bad.config(1)[2]=2;assert(!apply(output.data(),Bytes,bad.data(),wiring));
    bad=input;bad.data()[0x12]=8;assert(!apply(output.data(),Bytes,bad.data(),wiring));
    auto wrong=wiring;wrong.ddc[1]=4;assert(!apply(output.data(),Bytes,input.data(),wrong));
    wrong=wiring;wrong.hdmi=3;assert(!apply(output.data(),Bytes,input.data(),wrong));
    assert(!apply(input.data(),Bytes,input.data(),wiring));
    assert(!apply(output.data(),Bytes-1,input.data(),wiring));
    assert(!apply(output.data(),Bytes,nullptr,wiring));
    assert(skipDPLanes(output.data(),output.config(1),wiring,true,false,0,0));
    assert(!skipDPLanes(output.data(),output.config(1),wiring,false,false,0,0));
    assert(!skipDPLanes(output.data(),output.config(1),wiring,true,true,0,0));
    assert(!skipDPLanes(output.data(),output.config(1),wiring,true,false,1,0));
    assert(!skipDPLanes(output.data(),output.config(1),wiring,true,false,0,1));
    assert(!skipDPLanes(output.data(),input.config(1),wiring,true,false,0,0));
    assert(!skipDPLanes(output.data(),output.config(0),wiring,true,false,0,0));
    auto dp=output;dp.config(1)[2]=1;
    assert(!skipDPLanes(dp.data(),dp.config(1),wiring,true,false,0,0));
    // Both physical S1 routes and their simultaneous board description.
    for(unsigned mask=1;mask<=7;++mask) {
        const uint32_t prop[] {1,mask,(mask&1)?1U:0U,(mask&2)?2U:0U,(mask&4)?3U:0U};
        const auto w=TGLHDMIWiring::fromProperty(prop,sizeof(prop));Platform in,out;
        assert(adlnWiring(w)==!(mask&4));
        assert(selectedWiring(w) && singleOutputWiring(w)==(!(mask&(mask-1))));
        assert(singleDDI(w)==((mask&(mask-1))?UINT32_MAX:mask==1?0U:mask==2?1U:2U));
        assert(apply(out.data(),Bytes,in.data(),w));
        // Physical DDI comes from TRANS_DDI_FUNC_CTL, not framebuffer index
        // or the boot path's independent attachment-state field.
        for(unsigned enable=0;enable<2;++enable)for(unsigned selector=0;selector<16;++selector)
            for(unsigned mode=0;mode<8;++mode) {
                const auto trans=(enable<<31)|(selector<<27)|(mode<<24)|0x30100;
                const auto expected=enable && mode<=1 && selector>=1 && selector<=3 &&
                    (mask&(1U<<(selector-1))) ? selector-1 : UINT32_MAX;
                assert(bootDDI(w,trans)==expected);
            }
        assert(bootDDI(w,UINT32_MAX)==UINT32_MAX);
        assert(bootstrapDDI(w,true,3,1,0x89030100)==((mask&1)?0U:UINT32_MAX));
        assert(bootstrapDDI(w,true,3,1,0x91030100)==((mask&2)?1U:UINT32_MAX));
        assert(bootstrapDDI(w,true,3,3,0x89030100)==UINT32_MAX); // No silent second-GOP takeover.
        assert(bootstrapDDI(w,true,2,1,0x89030100)==UINT32_MAX);
        assert(bootstrapDDI(w,false,3,1,0x89030100)==UINT32_MAX);
        assert(bootstrapDDI(w,true,3,2,0x91030100)==UINT32_MAX); // Boot backend still adopts pipe A.
        for(unsigned d=0;d<Ports;++d) {
            const bool active=(mask&(1U<<d))!=0;
            assert(selected(out.data(),out.config(d),w)==active);
            if(active)assert(out.config(d)[1]==d+1 && out.config(d)[2]==2 && out.config(d)[4]==0x800 && out.config(d)[5]==0x202);
            else assert(out.config(d)[4]==1 && !out.config(d)[5]);
        }
        assert(excludedTypeC(out.data(),Bytes,w)==0x3F000000);
        assert(!selected(out.data(),reinterpret_cast<void *>(1),w));
        assert(!memcmp(out.data(),in.data(),0x20) && !memcmp(out.data()+0xF8,in.data()+0xF8,Bytes-0xF8));
        auto malformed=w;malformed.ddc[0]=4;auto keep=out;
        assert(!apply(out.data(),Bytes,in.data(),malformed) && !memcmp(out.data(),keep.data(),Bytes));
        if(mask==3){auto broken=out;broken.config(0)[5]=0x18;assert(!excludedTypeC(broken.data(),Bytes,w));}
    }
    assert(singleOutputWiring(wiring));
    auto invalid=wiring;invalid.ddc[1]=4;assert(bootDDI(invalid,0x90030100)==UINT32_MAX);
    puts("PASS AlderLakeN connector policy: stable identities/counts/pool, A/DDC1, B/DDC2 and A+B; measured single-GOP bootstrap, excluded connectors, malformed wiring and foreign config rejection. CPU only.");
}
