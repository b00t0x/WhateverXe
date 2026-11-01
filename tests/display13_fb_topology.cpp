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
            const uint32_t record[] {d,d<3?d+1:d+6,1,d<3?0U:1U,0x400,0x201};memcpy(config(d),record,24);
        }
        words[0]=0x123456789A490000ULL;words[0x108/8]=0x123456789ABCDEF0ULL;
    }
};
int main() {
    // DSI has a native internal-panel shape but no DP/AUX/HPD protocol.
    // Description is usable offline; runtime admission remains closed.
    for(unsigned external=0;external<4;++external) {
        Display13OutputWiring w;w.valid=true;w.dsi=1;
        if(external&1){w.hdmi=2;w.ddc[1]=2;}
        if(external&2){w.dp=8;w.typeC=8;}
        Platform in,out;assert(w.described() && w.internal()==1 && w.links()==w.dp);
        assert(!supportedWiring(w,true) && deferredBootPrepare(w,false));
        assert(w.transport(0)==Display13OutputWiring::Transport::DSI);
        assert(apply(out.data(),Bytes,in.data(),w));
        assert(!memcmp(out.config(0),in.config(0),24));
        assert(boundPorts(out.data(),w).selected==w.selected());
        assert(excludedHotplug(out.data(),Bytes,w)&(1U<<16));
        assert(bootDDI(w,0x88030000)==UINT32_MAX && bootDDI(w,0x8A030000,true)==UINT32_MAX);
        auto before=out;
        for(unsigned collision=0;collision<3;++collision) {
            auto bad=w;
            if(collision==0){bad.hdmi|=1;bad.ddc[0]=1;}
            if(collision==1)bad.dp|=1;
            if(collision==2)bad.panel=1;
            assert(!apply(out.data(),Bytes,in.data(),bad) && !memcmp(out.data(),before.data(),Bytes));
        }
        auto bad=w;bad.dsi=2;assert(!bad.described());
        bad=w;bad.ddc[0]=1;assert(!bad.described());
    }
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
    assert(excludedHotplug(output.data(),Bytes,wiring)==0x3F050000);
    auto activeTC=output;activeTC.config(3)[4]=0x400;
    assert(!excludedHotplug(activeTC.data(),Bytes,wiring));
    activeTC=output;activeTC.config(8)[5]=0x201;
    assert(!excludedHotplug(activeTC.data(),Bytes,wiring));
    assert(!excludedHotplug(output.data(),Bytes-1,wiring));
    // Unselected Combo records must be disabled just like TC records.
    for(unsigned d=0;d<3;d+=2)for(unsigned field=4;field<=5;++field) {
        auto bad=output;bad.config(d)[field]=0x400;
        assert(!excludedHotplug(bad.data(),Bytes,wiring));
    }
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
        assert(excludedHotplug(out.data(),Bytes,w)==(0x3F000000|((7U&~mask)<<16)));
        assert(!selected(out.data(),reinterpret_cast<void *>(1),w));
        assert(!memcmp(out.data(),in.data(),0x20) && !memcmp(out.data()+0xF8,in.data()+0xF8,Bytes-0xF8));
        auto malformed=w;malformed.ddc[0]=4;auto keep=out;
        assert(!apply(out.data(),Bytes,in.data(),malformed) && !memcmp(out.data(),keep.data(),Bytes));
        if(mask==3){auto broken=out;broken.config(0)[5]=0x18;assert(!excludedHotplug(broken.data(),Bytes,w));}
    }
    // All fixed Combo HDMI/DP assignments compose CPU topology/resources.
    // DP runtime/boot admission stays closed until the hardware hooks exist.
    for(unsigned assignment=1;assignment<9;++assignment) {
        Display13OutputWiring w;w.valid=true;unsigned code=assignment;
        for(unsigned d=0;d<2;++d){unsigned kind=code%3;code/=3;
            if(kind==1){w.hdmi|=1U<<d;w.ddc[d]=d+1;}if(kind==2)w.dp|=1U<<d;}
        Platform in,out;assert(selectedWiring(w) && adlnWiring(w));
        assert(supportedWiring(w)==!w.dp);assert(apply(out.data(),Bytes,in.data(),w));
        const auto ports=boundPorts(out.data(),w);assert(ports.valid() && ports.selected==w.selected());
        for(unsigned d=0;d<9;++d) {
            const bool on=w.selected()&(1U<<d),hdmi=w.hdmi&(1U<<d);
            assert(selected(out.data(),out.config(d),w)==on);
            if(on)assert(out.config(d)[2]==(hdmi?2U:1U));
            for(unsigned mode=0;mode<8;++mode)
                assert(bootDDI(w,0x80000000|((d+1)<<27)|(mode<<24))==(hdmi&&mode<=1?d:~0U));
        }
        assert(excludedHotplug(out.data(),Bytes,w)==(0x3f000000|((7U&~w.selected())<<16)));
        auto reloaded=w;reloaded=wiring;assert(!reloaded.dp && reloaded.hdmi==2);
        auto bad=w;bad.dp|=bad.hdmi; // Empty HDMI in DP-only: inject explicit overlap.
        if(!bad.hdmi){bad.hdmi=bad.dp;for(unsigned d=0;d<2;++d)if(bad.hdmi&(1U<<d))bad.ddc[d]=d+1;}
        auto saved=out;assert(!apply(out.data(),Bytes,in.data(),bad) && !memcmp(out.data(),saved.data(),Bytes));
        bad=w;bad.dp|=4;assert(!apply(out.data(),Bytes,in.data(),bad) && !memcmp(out.data(),saved.data(),Bytes));
        auto corrupted=out;
        for(unsigned d=0;d<2;++d)if(w.dp&(1U<<d)) {
            corrupted.config(d)[2]=2;assert(!boundPorts(corrupted.data(),w).valid());corrupted=out;
        }
    }
    // The production topology must apply every physical fixed-HDMI subset,
    // including mixed Combo/TC and all four TC slots, preserving native bindings.
    for(unsigned mask=1;mask<128;++mask)if(!(mask&~Display13OutputWiring::Physical)) {
        Display13OutputWiring w;w.valid=true;w.hdmi=mask;
        for(unsigned d=0;d<7;++d)if(mask&(1U<<d))w.ddc[d]=d<2?d+1:d+6;
        Platform in,out;
        assert(supportedWiring(w,true) && apply(out.data(),Bytes,in.data(),w));
        assert(boundPorts(out.data(),w).selected==mask);
        assert(excludedHotplug(out.data(),Bytes,w)==((0x3f000000&~((mask>>3)<<24))|((7U&~mask)<<16)));
        for(unsigned d=0;d<9;++d)assert(selected(out.data(),out.config(d),w)==bool(mask&(1U<<d)));
        for(unsigned d=3;d<7;++d)if(mask&(1U<<d)) {
            auto bad=w;bad.ddc[d]=8;assert(!supportedWiring(bad,true));
            bad=w;bad.dp=1U<<d;assert(!supportedWiring(bad,true));
        }
    }
    // DP boot is an explicit selection request, followed by source/power proof.
    // Exercise every physical TC slot with an independent HDMI neighbour.
    for(unsigned d=3;d<=6;++d) {
        Display13OutputWiring w;w.valid=true;w.dp=1U<<d;w.hdmi=2;w.ddc[1]=2;
        assert(supportedWiring(w,true));
        for(unsigned port=0;port<7;++port)for(unsigned mode=0;mode<8;++mode) {
            const auto trans=0x80000000U|((port+1)<<27)|(mode<<24);
            const auto expected=port==d && mode==2?d:port==1 && mode<=1?1U:UINT32_MAX;
            assert(bootstrapDDI(w,true,1,1,trans,true)==expected);
            assert(bootstrapDDI(w,true,1,1,trans)==(port==1 && mode<=1?1U:UINT32_MAX));
            assert(bootstrapDDI(w,true,3,3,trans,true)==UINT32_MAX);
        }
    }
    // A failure on the last selected predecessor must not publish a copy.
    {const uint32_t prop[]={1,7,1,2,3};const auto w=TGLHDMIWiring::fromProperty(prop,sizeof(prop));
     Platform in,out;memset(out.data(),0x5a,Bytes);auto saved=out;in.config(2)[2]=0;
     assert(!apply(out.data(),Bytes,in.data(),w) && !memcmp(out.data(),saved.data(),Bytes));}
    assert(singleOutputWiring(wiring));
    auto invalid=wiring;invalid.ddc[1]=4;assert(bootDDI(invalid,0x90030100)==UINT32_MAX);
    puts("PASS AlderLakeN connector policy: stable identities/counts/pool, A/DDC1, B/DDC2 and A+B; measured single-GOP bootstrap, excluded connectors, malformed wiring and foreign config rejection. CPU only.");
}
