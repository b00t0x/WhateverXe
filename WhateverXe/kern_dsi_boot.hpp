// Read-only certificate for a single DSI0/pipe-A linear firmware scanout.
// Caller serializes pre-native bootstrap (before any competing GPU writer),
// or holds the runtime source/pipe leases. No link, plane or PTE is modified.
#ifndef KERN_DSI_BOOT_HPP
#define KERN_DSI_BOOT_HPP
#include "kern_dsi_readout.hpp"
#include "kern_alderlake_n.hpp"
#include "kern_xelp_memory_layout.hpp"
#include "kern_xelp_geometry.hpp"
namespace XELP { namespace DSI {
enum class BootStatus : uint32_t {Unavailable,Candidate,Complete,Source,Peers,Geometry,Memory,PTE,Changed};
struct Boot {
    uint32_t version {1};BootStatus status {BootStatus::Unavailable};
    Readout source;XELPMemory::Layout memory;XELPGeometry::Linear geometry;
    uint32_t poweredPipes {},control {},stride {},size {},surface {},dpt {},verifiedPages {};
    bool usable() const {return status==BootStatus::Candidate || status==BootStatus::Complete;}
    bool complete() const {return status==BootStatus::Complete;}
};
// No active normal transcoder, secondary DSI, cursor, overlay, scaler, DSC or
// DSB is adopted implicitly. Recheck power before reading any pipe-local bank.
template<class IO> bool bootPeers(IO &io,uint32_t poweredPipes) {
    if(!(poweredPipes&1) || (poweredPipes&~7U))return false;
    const auto value=[&](uint32_t reg,uint32_t mask,uint32_t expected) {
        if(!io.readable(reg))return false;const auto v=io.read(reg);
        return v!=UINT32_MAX && (v&mask)==expected;
    };
    if(!io.readable(0x45404))return false;
    const auto power=io.read(0x45404);
    if(power==UINT32_MAX || (power&3)!=3 ||
       (((power>>10)&1)|((power>>11)&2)|((power>>12)&4))!=poweredPipes ||
       ((poweredPipes&6) && (power&5)!=5) || !value(0x7B808,0xC0000000U,0))return false;
    for(unsigned p=0;p<3;++p)if(poweredPipes&(1U<<p)) {
        const auto bank=p*0x1000;
        if(!value(0x60400+bank,0x80000000U,0) || !value(0x70008+bank,0xC0000000U,0) ||
           !value(0x70080+bank,63,0) || !value(0x78000+0x200*p,0xF0300000U,0) ||
           !value(0x78004+0x200*p,0x80008000U,0))return false;
        for(unsigned plane=p ? 0 : 1;plane<7;++plane)
            if(!value(0x70180+bank+0x100*plane,0x80000000U,0))return false;
        for(unsigned scaler=0;scaler<2;++scaler)
            if(!value(0x68180+0x800*p+0x100*scaler,0x80000000U,0))return false;
        for(unsigned dsb=0;dsb<3;++dsb)
            if(!value(0x70B08+bank+0x100*dsb,0x80000001U,0))return false;
    }
    return true;
}
template<class IO> bool bootUnchanged(IO &io,const Boot &boot,const XELPMemory::Layout &memory) {
    if(!boot.usable() || !XELPMemory::same(boot.memory,memory) ||
       !sourceUnchanged(io,boot.source) || !bootPeers(io,boot.poweredPipes))return false;
    const uint32_t regs[]={0x42084,0x70180,0x70188,0x70190,0x7019C,0x701AC,0x7018C,0x701A4};
    const uint32_t expected[]={boot.dpt,boot.control,boot.stride,boot.size,boot.surface,boot.surface,0,0};
    for(unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);++i)
        if(!io.readable(regs[i]) || io.read(regs[i])!=expected[i])return false;
    return sourceUnchanged(io,boot.source);
}
template<class IO> void captureBoot(IO &io,const AlderLakeN::DisplayReadout &normal,
                                    const Profile &profile,const XELPMemory::Layout &memory,bool admitted,Boot &r) {
    r={};if(!admitted || !profile.valid())return;
    r.status=BootStatus::Memory;
    if(!memory.valid || !memory.reservationKnown || !memory.bytes || memory.bytes%4096 ||
       memory.bytes>XELPMemory::Aperture || memory.bytes>memory.apertureBytes || !io.readable(0x800000+8*(memory.bytes/4096)-1))return;
    r.memory=memory;r.poweredPipes=normal.poweredPipes;r.status=BootStatus::Peers;
    if(normal.status!=AlderLakeN::ReadoutStatus::Complete || normal.activePipes ||
       !bootPeers(io,r.poweredPipes))return;
    r.status=BootStatus::Source;
    if(!io.readable(0x51004))return;
    const auto ref=(io.read(0x51004)>>29)&7;const uint32_t references[]={24000,19200,38400};
    if(ref>=3)return;
    r.source=decodeReadout([&](uint32_t reg,uint32_t &v) {
        if(!io.readable(reg))return false;v=io.read(reg);return true;
    },references[ref]);
    if(r.source.identity.pipe!=0 || !r.source.matches(profile))return;
    r.status=BootStatus::Geometry;
    const uint32_t regs[]={0x42084,0x70180,0x70188,0x70190,0x7019C,0x701AC,0x7018C,0x701A4};
    uint32_t words[8]{};
    for(unsigned i=0;i<8;++i){if(!io.readable(regs[i]))return;words[i]=io.read(regs[i]);if(words[i]==UINT32_MAX)return;}
    r.dpt=words[0];r.control=words[1];r.stride=words[2];r.size=words[3];r.surface=words[4];
    r.geometry=XELPGeometry::linear(r.control,r.stride,r.size,words[6],words[7]);
    // LINEAR uses byte GGTT offsets even with global DPT-disable clear.
    if(!XELPGeometry::timing(r.geometry,r.source.htotal,r.source.vtotal) || r.surface!=words[5] ||
       !memory.range(r.surface,r.geometry.bytes,true))return;
    r.status=BootStatus::Candidate;
    if(!bootUnchanged(io,r,memory))r.status=BootStatus::Changed;
    return;
}
template<class IO> Boot captureBoot(IO &io,const AlderLakeN::DisplayReadout &normal,
                                    const Profile &profile,const XELPMemory::Layout &memory,bool admitted) {
    Boot r;captureBoot(io,normal,profile,memory,admitted,r);return r;
}
// Prove the full stolen prefix that the native allocator will protect, not
// merely the visible pixels. This does not grant an allocator reservation.
template<class IO> bool verifyBootPrefix(IO &io,Boot &boot,const XELPMemory::Layout &memory) {
    if(!bootUnchanged(io,boot,memory)){boot.status=BootStatus::Changed;return false;}
    boot.verifiedPages=0;
    for(uint32_t i=0;i<memory.bytes/4096;++i) {
        if(io.readPTE(0x800000+8*i)!=memory.pte(uint64_t(i)*4096)){boot.status=BootStatus::PTE;return false;}
        ++boot.verifiedPages;
    }
    if(!bootUnchanged(io,boot,memory)){boot.status=BootStatus::Changed;return false;}
    boot.status=BootStatus::Complete;return true;
}
}}
#endif
