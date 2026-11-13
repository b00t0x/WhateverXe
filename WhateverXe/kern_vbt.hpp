// Bounded firmware facts, separate from donor ABI and runtime link state.
// Wire offsets/port numbering checked against Intel VBT definitions in Linux
// v6.12. This parser is independently implemented; no Linux code is included.
#ifndef KERN_VBT_HPP
#define KERN_VBT_HPP
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kern_xelp_platform.hpp"
#include "kern_tgl_hdmi_wiring.hpp"
namespace XELP { namespace VBT {
inline uint16_t u16(const uint8_t *p) {return uint16_t(uint16_t(p[0]) | uint16_t(p[1])<<8);}
inline uint32_t u32(const uint8_t *p) {return uint32_t(u16(p)) | uint32_t(u16(p+2))<<16;}
inline uint64_t u64(const uint8_t *p) {return uint64_t(u32(p)) | uint64_t(u32(p+4))<<32;}
enum class Status : uint32_t { Missing, Valid, Header, Checksum, Blocks, Version, Children, Conflict, Platform };
struct Port {
    uint32_t type {},dvo {},ddc {},aux {},flags {},capabilities {};
    uint32_t hdmiPreset {UINT32_MAX}; // BDB child byte7, table index; absent firmware uses default.
    // flags preserves byte23 (lane reversal, LSPCON, HPD polarity, lane count).
    // capabilities preserves byte33 (USB-C/TBT capabilities, NOT current mode).
    bool dp() const {return type&4;}
    bool internal() const {return type&0x1000;}
    bool tmdsOnly() const {return (type&0x12)==0x12 && !(type&(4|0x1000|0x400|0x20|1)) &&
        !(flags&0x17);}
    bool fixedTMDS() const {return tmdsOnly() && !(capabilities&3);}
};
struct Ports {
    Status status {Status::Missing};
    uint32_t version {},bytes {},present {},unmapped {},ambiguous {};
    uint32_t checksum {};
    struct Panel {
        bool valid {},lowSwing {},hobl {};
        uint32_t type {},ddi {PortCount};
        // BDB 27: five little-endian 100us delays per panel, including T12.
        uint16_t delays[5] {};
        // BDB 43 is optional and independent of the signal-policy facts.
        bool backlightValid {},pwmPresent {},activeLowPWM {};
        uint32_t backlightType {},controller {},pwmHz {};
    } panel;
    Port port[PortCount];
    bool valid() const {return status==Status::Valid;}
    // Linux direct_aux_ch_map: A=0x40, B/C/D=0x10/0x20/0x30, E..I=0x50..0x90.
    static uint32_t tglTCAux(unsigned ddi) {return ddi>=3 && ddi<PortCount ? (ddi==3?0x30U:(ddi+1)*0x10U) : 0;}
    // USB-C DP-alt uses the native mode3 owner, not the fixed-DP mode4
    // backend. VBT AUX D is 0x30 (A is 0x40), then E..I are 0x50..0x90.
    uint32_t tglDPAltCandidates(uint32_t backend) const {
        if(!valid())return 0;
        uint32_t result=0;
        for(unsigned d=3;d<PortCount;++d) {
            const auto &p=port[d];
            bool shared=false;
            for(unsigned other=0;other<PortCount;++other)
                if(other!=d && (present&(1U<<other)) && port[other].dp() && port[other].aux==p.aux)shared=true;
            if((present&backend&(1U<<d)) && p.dp() && !p.internal() &&
               !(p.type&(0x400|0x20|1)) && !(p.flags&0x17) && (p.capabilities&1) &&
               !shared && p.aux==tglTCAux(d))result|=1U<<d;
        }
        return result;
    }
    // TGL external DP candidates, not a fixed/alt/TBT mode decision. Current
    // native AUX routing follows DDI; different firmware AUX needs a backend.
    uint32_t tglDPCandidates(uint32_t backend) const {
        if(!valid())return 0;
        uint32_t result=0;
        for(unsigned d=3;d<PortCount;++d) {
            const auto &p=port[d];
            bool shared=false;
            for(unsigned other=0;other<PortCount;++other)
                if(other!=d && (present&(1U<<other)) && port[other].dp() && port[other].aux==p.aux)shared=true;
            if((present&backend&(1U<<d)) && (p.type&0x14)==0x14 &&
               !(p.type&(0x1000|0x400|0x20|1)) && !(p.flags&0x17) &&
               !shared && p.aux==tglTCAux(d))result|=1U<<d;
        }
        return result;
    }
    TGLHDMIWiring hdmi(uint32_t backend) const {
        auto w=TGLHDMIWiring::empty();
        if(!valid())return w; // no guessed wiring; caller retains existing guards
        uint32_t used=0;
        for(unsigned d=0;d<3;++d) {
            if(!(present&backend&(1U<<d)) || !port[d].fixedTMDS())continue;
            const auto pin=port[d].ddc;
            if(pin<1 || pin>3 || (used&(1U<<pin)))return {}; // ambiguous DDC ownership
            used|=1U<<pin;w.hdmi|=1U<<d;w.ddc[d]=pin;
        }
        return w;
    }
};
// Mailbox 5 contains up to two complete EDID blocks. Only the product ID is
// consumed, but validate the advertised EDID before using it as panel identity.
inline const uint8_t *panelEDID(const void *data,size_t size) {
    const auto b=static_cast<const uint8_t *>(data);
    if(!b || size<8192 || !(u32(b+0x58)&0x10))return nullptr;
    const auto blocks=u32(b+0x1c00)&3;
    const auto e=b+0x1c04;
    const uint8_t signature[]={0,255,255,255,255,255,255,0};
    if(!blocks || blocks>2 || memcmp(e,signature,8) || e[126]+1!=blocks || !(e[20]&0x80))return nullptr;
    for(unsigned n=0;n<blocks;++n) {
        uint8_t sum=0;for(unsigned i=0;i<128;++i)sum+=e[128*n+i];
        if(sum)return nullptr;
    }
    return e;
}
// BDB 41 offsets are relative to the BDB; BDB 42 stores FP/DTD/PNP records.
// Only identity is consumed here, never the firmware mode timing. Validate the
// complete pointer layout, including the historical six-byte FP size hole.
inline uint16_t panelMatches(const uint8_t *b,const uint8_t *ptr,size_t ptrSize,
                             const uint8_t *data,size_t dataSize,const uint8_t *edid) {
    if(!edid || !data)return 0;
    size_t stride=66,fp=38;
    if(ptr) {
        if(ptrSize<145 || ptr[0]!=3 || ptr[3]<32 || ptr[6]!=18 || ptr[9]!=10)return 0;
        const auto first=u16(ptr+1),next=u16(ptr+10);
        if(next<=first)return 0;
        stride=next-first;fp=ptr[3];
        if(stride==fp+6+28)fp+=6;
        if(stride!=fp+28)return 0;
    }
    if(stride>dataSize/16)return 0;
    uint16_t exact=0,undated=0;
    for(unsigned n=0;n<16;++n) {
        const auto at=n*stride;
        if(ptr) {
            const auto q=ptr+1+9*n;
            const auto base=size_t(data-b)+at;
            if(q[2]!=ptr[3] || q[5]!=18 || q[8]!=10 ||
               u16(q)!=base || u16(q+3)!=base+fp || u16(q+6)!=base+fp+18)return 0;
        }
        if(u16(data+at+fp-2)!=0xffff)return 0;
        const auto id=data+at+fp+18;
        if(!memcmp(id,edid+8,10))exact|=1U<<n;
        else if(!memcmp(id,edid+8,8) && !u16(id+8))undated|=1U<<n;
    }
    return exact?exact:undated;
}
inline bool samePanelFacts(const Ports::Panel &a,const Ports::Panel &b) {
    if(a.lowSwing!=b.lowSwing || a.hobl!=b.hobl ||
       a.backlightValid!=b.backlightValid || a.pwmPresent!=b.pwmPresent ||
       a.activeLowPWM!=b.activeLowPWM || a.backlightType!=b.backlightType ||
       a.controller!=b.controller || a.pwmHz!=b.pwmHz)return false;
    for(unsigned i=0;i<5;++i)if(a.delays[i]!=b.delays[i])return false;
    return true;
}
// Optional panel facts must never weaken General Definitions admission or
// make an unrelated malformed firmware tail reject usable connector facts.
inline void panelFacts(const uint8_t *b,size_t header,size_t size,Ports &ports,const uint8_t *identity,uint16_t handle) {
    const uint8_t *options=nullptr,*edp=nullptr,*power=nullptr,*backlight=nullptr;
    size_t optionsSize=0,edpSize=0,powerSize=0,backlightSize=0,ptrSize=0,dataSize=0;
    const uint8_t *ptr=nullptr,*data=nullptr;bool identityDuplicate=false;
    bool duplicate=false,backlightDuplicate=false;
    for(size_t at=header;at<size;) {
        if(size-at<3)break;
        size_t n=u16(b+at+1);
        if(b[at]==53 && size-at>=4 && b[at+3]>=3) {
            if(size-at<8)break;
            n=u32(b+at+4);
        }
        if(n>size-at-3)break;
        auto block=b+at+3;
        if(b[at]==41){identityDuplicate|=ptr!=nullptr;ptr=block;ptrSize=n;}
        if(b[at]==42){identityDuplicate|=data!=nullptr;data=block;dataSize=n;}
        if(b[at]==40){duplicate|=options!=nullptr;options=block;optionsSize=n;}
        if(b[at]==27){duplicate|=edp!=nullptr;edp=block;edpSize=n;}
        if(b[at]==43){backlightDuplicate|=backlight!=nullptr;backlight=block;backlightSize=n;}
        if(b[at]==44){duplicate|=power!=nullptr;power=block;powerSize=n;}
        at+=3+n;
    }
    if(duplicate || !options || optionsSize<(handle==0x80?2U:1U) ||
       !edp || edpSize<212 || (ports.version>=232 && (!power || powerSize<56)))return;
    unsigned internal=PortCount;
    for(unsigned d=0;d<PortCount;++d)if((ports.present&(1U<<d)) &&
        ports.port[d].internal() && ports.port[d].dp()) {
        if(internal!=PortCount)return; // no guessed panel/connector association
        internal=d;
    }
    if(internal==PortCount)return;
    const auto requested=options[handle==0x80?1:0];
    if(requested>=16 && requested!=0xff)return;
    // No blind profile-zero fallback. Missing identity is usable only if all
    // profiles agree on every fact consumed by WEX. Ambiguous identity follows
    // the same equivalence rule; UINT32_MAX reports that no index was selected.
    uint16_t candidates=requested<16?uint16_t(1U<<requested):
        identityDuplicate?0:panelMatches(b,ptr,ptrSize,data,dataSize,identity);
    if(!candidates)candidates=0xffff;
    bool found=false;unsigned count=0;
    for(unsigned type=0;type<16;++type)if(candidates&(1U<<type)) {
        Ports::Panel p;p.type=type;p.ddi=internal;
        p.lowSwing=((u64(edp+204)>>(4*type))&15)==0;
        p.hobl=ports.version>=232 && (u16(power+54)&(1U<<type));
        p.valid=true;
        for(unsigned i=0;i<5;++i)p.delays[i]=u16(edp+10*type+2*i);
        if(!backlightDuplicate && backlight && backlightSize>=129 && backlight[0]==6) {
            const auto entry=backlight+1+6*type;
            p.pwmPresent=(entry[0]&3)==2;p.activeLowPWM=entry[0]&4;
            p.pwmHz=u16(entry+1);
            p.backlightType=backlight[113+type]&15;
            p.controller=backlight[113+type]>>4;p.backlightValid=true;
        }
        if(found && !samePanelFacts(ports.panel,p)){ports.panel={};return;}
        ports.panel=p;found=true;++count;
    }
    if(count>1)ports.panel.type=UINT32_MAX;
}
// Return the donor's physical-DDI slot, never an FB index. Display13 VBT
// retains C/D/E labels even on ADL-N, whose physical Combo set is only A/B.
inline uint32_t ddi(Family family,uint32_t dvo) {
    const auto adapter=displayAdapter(family);
    const uint8_t dp[]={10,7,8,9,11,13,15,17,19};
    const uint8_t hd[]={0,1,2,3,12,14,16,18,20};
    for(unsigned p=0;p<9;++p)if(dvo==dp[p] || dvo==hd[p]) {
        if(adapter==DisplayAdapter::TGL)return p==2 ? PortCount : p;
        if((adapter==DisplayAdapter::ADLN || adapter==DisplayAdapter::ADLP))return p<2 ? p : p>=5 ? p-2 : PortCount;
        if(adapter==DisplayAdapter::ADLS)return p==0 ? 0 : p<5 ? p+2 : PortCount;
    }
    return PortCount;
}
inline Ports parse(const void *data,size_t length,Family family,const uint8_t *identity=nullptr) {
    Ports r;
    if(!data || !length)return r;
    if(displayAdapter(family)==DisplayAdapter::Unknown){r.status=Status::Platform;return r;}
    const auto p=static_cast<const uint8_t *>(data);r.status=Status::Header;
    if(length<48 || memcmp(p,"$VBT",4))return r;
    const size_t header=u16(p+22),size=u16(p+24),bdb=u32(p+28);
    if(header<48 || size<header || size>length || bdb<header || bdb>size || size-bdb<22)return r;
    // Firmware may update runtime fields without a matching table checksum.
    // Like i915, admit by bounded structure and connector semantics; retain
    // the checksum residue for diagnosis, never rewrite the firmware bytes.
    uint8_t sum=0;for(size_t i=0;i<size;++i)sum+=p[i];
    r.checksum=sum;
    const auto b=p+bdb;const size_t bh=u16(b+18),bs=u16(b+20);
    if(memcmp(b,"BIOS_DATA_BLOCK ",16) || bh<22 || bs<bh || bs>size-bdb)return r;
    r.version=u16(b+16);r.bytes=static_cast<uint32_t>(size);
    // Reviewed field layout. Unknown future versions never become board facts.
    if(r.version<216 || r.version>256){r.status=Status::Version;return r;}
    r.status=Status::Blocks;const uint8_t *children=nullptr;size_t childBytes=0;
    for(size_t at=bh;at<bs;) {
        if(bs-at<3)return r;
        size_t count=u16(b+at+1);
        if(b[at]==53) { // MIPI sequence v3 uses a 32-bit block length
            if(bs-at<4)return r;
            if(b[at+3]>=3){if(bs-at<8)return r;count=u32(b+at+4);}
        }
        if(count>bs-at-3)return r;
        // Consume only General Definitions; unrelated later blocks need not
        // parse successfully (real firmware has off-by-one BDB tail sizes).
        if(b[at]==2){children=b+at+3;childBytes=count;break;}
        at+=3+count;
    }
    r.status=Status::Children;
    if(!children || childBytes<5)return r;
    const auto stride=children[4];
    if(stride<38 || stride>64 || (childBytes-5)%stride)return r;
    const uint8_t *records[PortCount] {};uint16_t panelHandle=0;
    for(size_t at=5;at<childBytes;at+=stride) {
        const auto c=children+at;const auto type=u16(c+2);
        // Non-digital placeholders have no DVO association. ADL-S firmware
        // includes TV/CRT entries with DVO0 alongside the real digital A.
        if(!(type&2))continue;
        const auto d=ddi(family,c[16]);
        if(d==PortCount){++r.unmapped;continue;}
        if(r.present&(1U<<d)) {
            // Identical wire records carry no competing ownership. Preserve
            // every byte (including the native handle) when deciding equality.
            if(!memcmp(records[d],c,stride))continue;
            r.status=Status::Conflict;return r;
        }
        records[d]=c;if((type&0x1004)==0x1004)panelHandle=u16(c);
        auto &q=r.port[d];q.type=type;q.dvo=c[16];q.ddc=c[19];q.aux=c[25];q.flags=c[23];q.capabilities=c[33];q.hdmiPreset=c[7]&31;
        r.present|=1U<<d;
        if(!q.fixedTMDS())r.ambiguous|=1U<<d;
    }
    r.status=Status::Valid;panelFacts(b,bh,bs,r,identity,panelHandle);return r;
}
// OpRegion acquisition descriptor, kept pure for malformed/overflow testing.
struct Region {uint64_t address {};uint32_t bytes {},inlineBytes {};bool valid {};};
inline Region region(const void *data,size_t length,uint32_t asls) {
    Region r;const auto p=static_cast<const uint8_t *>(data);
    if(!p || length<8192 || !asls || asls>0xFFFFE000U || memcmp(p,"IntelGraphicsMem",16) || u32(p+16)<8)return r;
    r.valid=true;const auto boxes=u32(p+0x58);
    r.inlineBytes=((boxes&0x10)?0x1C00:8192)-0x400;
    if(p[23]!=2 || !(boxes&4))return r;
    auto address=u64(p+0x3BA);const auto bytes=u32(p+0x3C2);
    if(!address || bytes<48 || bytes>65536)return r;
    if(p[22]>=1){if(address<8192 || address>UINT64_MAX-asls)return r;address+=asls;}
    // x86 physical-address ceiling; reject overflow before mapping firmware.
    if(address>0xFFFFFFFFFFFFFULL || bytes>0x10000000000000ULL-address)return r;
    r.address=address;r.bytes=bytes;return r;
}
inline TGLHDMIWiring selectHDMI(bool explicitProperty,const void *bytes,uint32_t length,
                                 const Ports &firmware,uint32_t backend) {
    return explicitProperty ? TGLHDMIWiring::fromProperty(bytes,length) : firmware.hdmi(backend);
}
} }
#endif
