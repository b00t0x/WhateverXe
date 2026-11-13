// ADL-N/Twin Lake identity and fuse-derived small GT profile.
// Runtime qualification remains separate from PCI family recognition.
// Linux v6.12 intel_display_power_map.c / i915_reg.h provide Display 13
// register definitions. TGL donor startup remains blocked until separately
// qualified; recognizing this profile does not make the donor safe to start.
#ifndef kern_alderlake_n_hpp
#define kern_alderlake_n_hpp
#include <stdint.h>
#include "kern_xelp_platform.hpp"
#include "kern_xelp_gt_resources.hpp"
#include "kern_xelp_memory_layout.hpp"
#include "kern_xelp_geometry.hpp"
#include <stddef.h>
#include "kern_display13_power_bridge.hpp"
#include "kern_display13_clock.hpp"
#include "kern_display13_tc.hpp"

namespace AlderLakeN {
constexpr uint32_t Identity = 0x46D18086;
constexpr uint32_t DisplayVersion = 13;
constexpr uint32_t GraphicsStep = 0; // A0, separate stepping domain.
constexpr uint32_t DisplayStep = 3;  // D0 in the ADL-N revision table.

// Linux v6.12 INTEL_ADLN_IDS includes Twin Lake 46D3/46D4. Do not infer
// EU count from these IDs, and do not admit other ADL-P devices here.
inline bool family(uint32_t identity) {
    return XELP::identify(identity,0).family==XELP::Family::AlderLakeN;
}
inline bool matches(uint32_t identity, uint8_t revision) {
    // Revision identifies the hardware; untested revisions use the family backend.
    const auto p=XELP::identify(identity,revision);
    return p.family==XELP::Family::AlderLakeN && p.admitted();
}

using Resources=XELP::SmallGTResources;
using XELP::smallGTResources;
inline bool resources(uint32_t slice,uint32_t dss,uint32_t euDisable,uint32_t l3Disable,Resources &out) {
    return smallGTResources(slice,dss,euDisable,l3Disable,out);
}

struct Register { uint32_t offset {}, value {}; };
enum class GTStatus : uint32_t {
    NotAdmitted, ExistingOwner, AckTimeout, Unavailable, ProfileMismatch, ReleaseFailed, Complete
};
struct GTReadout {
    uint32_t version {1};
    GTStatus status {GTStatus::NotAdmitted};
    uint32_t writes {}, requestedMask {}, releasedMask {}, releaseFailedMask {};
    uint32_t requestBefore[2] {}, ackBefore[2] {}, ackHeld[2] {}, requestAfter[2] {}, ackAfter[2] {};
    uint32_t fuses[5] {};
    Resources resources;
    uint32_t count {};
    Register registers[16] {};
};
static_assert(sizeof(GTReadout)==60*4,"AlderLakeN GT readout layout");

// Linux v6.12 gen12 forcewake domains: fuses and BCS are GT, RCS is render.
// Only acquire an unowned kernel bit0. Never reset domains or alter engines.
template<class IO> GTReadout readGT(IO &io, bool admitted, uint32_t *edram=nullptr,
    bool (*decode)(const uint32_t *,Resources &)=nullptr) {
    GTReadout out;
    if(!admitted)return out;
    constexpr uint32_t request[] {0xA188,0xA278},ack[] {0x130044,0xD84};
    constexpr uint32_t fuse[] {0x9138,0x913C,0x9134,0x9118,0x9140};
    constexpr uint32_t engine[] {0x2030,0x2034,0x2038,0x203C,0x209C,0x2234,0x2238,0x229C,
        0x22030,0x22034,0x22038,0x2203C,0x2209C,0x22234,0x22238,0x2229C};
    out.status=GTStatus::Unavailable;
    for(unsigned d=0;d<2;++d)if(!io.readable(request[d]) || !io.readable(ack[d]))return out;
    for(auto reg:fuse)if(!io.readable(reg))return out;
    for(auto reg:engine)if(!io.readable(reg))return out;
    for(unsigned d=0;d<2;++d) {
        out.requestBefore[d]=io.read(request[d]);out.ackBefore[d]=io.read(ack[d]);
        if(out.requestBefore[d]==UINT32_MAX || out.ackBefore[d]==UINT32_MAX)return out;
        if((out.requestBefore[d]|out.ackBefore[d])&1) {out.status=GTStatus::ExistingOwner;return out;}
    }
    const auto wait=[&](unsigned d,bool held,uint32_t &last) {
        for(unsigned n=0;n<500;++n) {
            last=io.read(ack[d]);
            if(last==UINT32_MAX)return false;
            if(bool(last&1)==held)return true;
            io.delay(); // 100us, bounded to Linux's 50ms ACK timeout.
        }
        return false;
    };
    for(unsigned d=0;d<2;++d) {
        io.write(request[d],0x00010001);++out.writes;out.requestedMask|=1U<<d;
        io.read(request[d]);
    }
    out.status=GTStatus::AckTimeout;
    bool held=true;
    for(unsigned d=0;d<2;++d)if(!wait(d,true,out.ackHeld[d]))held=false;
    if(out.ackHeld[0]==UINT32_MAX || out.ackHeld[1]==UINT32_MAX)out.status=GTStatus::Unavailable;
    if(held) {
        out.status=GTStatus::Complete;
        for(unsigned i=0;i<5;++i) {
            out.fuses[i]=io.read(fuse[i]);
            if(out.fuses[i]==UINT32_MAX)out.status=GTStatus::Unavailable;
        }
        if(out.status==GTStatus::Complete &&
           !(decode ? decode(out.fuses,out.resources) :
             (resources(out.fuses[0],out.fuses[1],out.fuses[2],out.fuses[3],out.resources) &&
              out.fuses[4]==0x000E00FE)))out.status=GTStatus::ProfileMismatch;
        if(edram) {
            *edram=io.readable(0x120010) ? io.read(0x120010) : UINT32_MAX;
            if(*edram==UINT32_MAX)out.status=GTStatus::Unavailable;
        }
        for(auto reg:engine) {
            const auto value=io.read(reg);out.registers[out.count++]={reg,value};
            if(value==UINT32_MAX)out.status=GTStatus::Unavailable;
        }
    }
    for(int d=1;d>=0;--d) {
        io.write(request[d],0x00010000);++out.writes;
        out.requestAfter[d]=io.read(request[d]);
        // Upper16 are the masked-write enable field, not client ownership.
        if(wait(unsigned(d),false,out.ackAfter[d]) &&
           (out.requestAfter[d]&0xFFFF)==(out.requestBefore[d]&0xFFFF))
            out.releasedMask|=1U<<d;
        else out.releaseFailedMask|=1U<<d;
    }
    if(out.releaseFailedMask)out.status=GTStatus::ReleaseFailed;
    return out;
}
enum class ReadoutStatus : uint32_t { NotAdmitted, Complete, Unavailable, PowerChanged };
struct DisplayReadout {
    uint32_t version {2};
    ReadoutStatus status {ReadoutStatus::NotAdmitted};
    uint32_t count {}, poweredPipes {}, activePipes {}, hdmiBPipes {};
    Register registers[160] {};
};

// Read only. Check Display 13 PW_A/B/C before pipe-local registers; do not
// access TGL FIA/TC/Dekel addresses or issue selector/power/MMIO writes.
template<class IO> void readDisplay(IO &io, bool admitted, DisplayReadout &r) {
    // Reused storage must not retain pipe bits or registers from an earlier
    // snapshot. Reset fields in place: aggregate assignment can itself reserve
    // a full DisplayReadout temporary on the unoptimized kernel stack.
    r.version=2;r.status=ReadoutStatus::NotAdmitted;
    r.count=r.poweredPipes=r.activePipes=r.hdmiBPipes=0;
    for(auto &reg:r.registers) {reg.offset=0;reg.value=0;}
    if (!admitted) return;
    const auto add = [&](uint32_t offset) {
        if (r.count >= sizeof(r.registers) / sizeof(r.registers[0]) || !io.readable(offset)) {
            r.status = ReadoutStatus::Unavailable;
            return UINT32_MAX;
        }
        const uint32_t value = io.read(offset);
        r.registers[r.count++] = {offset, value};
        if (value == UINT32_MAX) r.status = ReadoutStatus::Unavailable;
        return value;
    };
    r.status = ReadoutStatus::Complete;
    const uint32_t power = add(0x45404);
    if (r.status != ReadoutStatus::Complete) return;
    // Always-on / PW1 display state, including BIOS and DDI I/O ownership.
    const uint32_t common[] {0x45400, 0x45408, 0x4540C, 0x45450, 0x45454, 0x4545C,
        0x45420, 0x45424, 0x45444, 0x42000, 0x42084,
        0x44300, 0x4438C, 0x45008, 0x4500C, 0x45010, 0x45014,
        0x46000, 0x46004, 0x164280, 0x164284, 0x164288, 0x16428C,
        0x64000, 0x64100, 0x64200};
    for (auto offset : common) add(offset);
    for (unsigned pipe = 0; pipe < 3; ++pipe) {
        // XELPD_PW_CTL_IDX_PW_A = 5, B = 6, C = 7; state bit = 2*idx.
        if (!(power & (1U << (2 * (5 + pipe))))) continue;
        r.poweredPipes |= 1U << pipe;
        const auto func = add(0x60400 + 0x1000 * pipe);
        if (func != UINT32_MAX && (func & 0x80000000)) {
            r.activePipes |= 1U << pipe;
            // TGL+ uses (port + 1) in bits30:27. B is encoded as 2.
            if (((func >> 27) & 15) == 2 && ((func >> 24) & 7) <= 1)
                r.hdmiBPipes |= 1U << pipe;
        }
        const uint32_t local[] {0x60000, 0x60004, 0x60008, 0x6000C, 0x60010,
            0x60014, 0x6001C, 0x70008, 0x70028, 0x70040, 0x70080, 0x70084,
            0x70180, 0x70188, 0x7018C, 0x70190, 0x7019C, 0x701A4, 0x701AC,
            0x7027C, 0x7037C, 0x70280, 0x70380, 0x70480, 0x70580, 0x70680, 0x70780};
        for (auto offset : local) add(offset + 0x1000 * pipe);
        // TGL+ DP transport is transcoder-indexed, not DDI-indexed. M2/N2
        // are absent on Display13 (Linux intel_cpu_transcoder_has_m2_n2).
        const uint32_t transport[] {0x60404,0x60540,0x60030,0x60034,0x60040,0x60044};
        for (auto offset : transport)
            add(offset + 0x1000 * pipe);
        add(0x46140 + 4 * pipe);
        // Display11+ has two scalers per powered pipe (Linux display runtime).
        // Capture the binding and destination, without touching power/coefficients.
        for(unsigned scaler=0;scaler<2;++scaler) {
            const auto base=0x68170+0x800*pipe+0x100*scaler;
            add(base);add(base+4);add(base+0x10);
        }
        // Display13 DSC/joiner belongs to the corresponding pipe well.
        add(0x78000 + 0x200 * pipe);add(0x78004 + 0x200 * pipe);
    }
    const auto after = add(0x45404);
    if (r.status == ReadoutStatus::Complete && after != power)
        r.status = ReadoutStatus::PowerChanged;
}
// Value-returning callers retain the existing API. Early bootstrap uses the
// in-place overload with one allocated snapshot instead of stacked copies.
template<class IO> DisplayReadout readDisplay(IO &io, bool admitted) {
    DisplayReadout r;
    readDisplay(io,admitted,r);
    return r;
}

inline bool value(const DisplayReadout &r, uint32_t offset, uint32_t &out) {
    if (r.count > sizeof(r.registers) / sizeof(r.registers[0])) return false;
    for (uint32_t i=0; i<r.count; ++i)
        if (r.registers[i].offset==offset) { out=r.registers[i].value; return out!=UINT32_MAX; }
    return false;
}

// Source-side certificate for an already-running, uncompressed external SST
// GOP. This does not prove sink DPCD state, PLL rate or native AUX ownership.
inline bool dpSource(const DisplayReadout &boot,unsigned pipe,unsigned ddi,const Display13TC::Boot *tc=nullptr,bool panel=false) {
    const bool typeC=ddi>=3 && ddi<=6;
    if((panel && ddi>=2) || pipe>=3 || (ddi>=2 && !typeC) || boot.status!=ReadoutStatus::Complete ||
       !(boot.activePipes&boot.poweredPipes&(1U<<pipe)))return false;
    const auto bank=pipe*0x1000;
    uint32_t trans=0,tp=0,buf=0,clock=0,ctl2=0,dss1=0,dss2=0,dm=0,dn=0,lm=0,ln=0;
    // TC buffer access needs the separate powered TCSS/ownership witness.
    // Do not add every TC register to the unconditional common readout.
    if(typeC) {
        uint32_t power=0,ddiPower=0;
        if(!tc || !tc->valid() || tc->port.ddi!=ddi || tc->pipe!=pipe ||
           !value(boot,0x45404,power) || power!=tc->power ||
           !value(boot,0x45454,ddiPower) || ddiPower!=tc->ddiPower)return false;
        buf=tc->buffer;
    } else if(!value(boot,0x64000+ddi*0x100,buf))return false;
    if(!value(boot,0x60400+bank,trans) || !value(boot,0x60540+bank,tp) ||
       !value(boot,0x46140+4*pipe,clock) ||
       !value(boot,0x60404+bank,ctl2) || !value(boot,0x78000+pipe*0x200,dss1) ||
       !value(boot,0x78004+pipe*0x200,dss2) || !value(boot,0x60030+bank,dm) ||
       !value(boot,0x60034+bank,dn) || !value(boot,0x60040+bank,lm) ||
       !value(boot,0x60044+bank,ln))return false;
    const unsigned lanes=((trans>>1)&7)+1;
    // Linux v6.12 i915_reg.h: SST, normal training, scrambling, no FEC/MST,
    // port sync, HDCP, DSC or joined pipes. Enhanced framing may be on or off.
    // intel_ddi_read_func_ctl classifies SST from MODE_SELECT, not the MST
    // payload-allocation bit. Firmware may leave bit8 set on an SST GOP;
    // preserve it and include it in the unchanged-source check, never clear it
    // on an enabled transcoder or infer an MST transport from that bit alone.
    return (trans&0xFF000000U)==(0x82000000U|((ddi+1)<<27)) && XELPGeometry::rgbLinkDepth(trans) &&
        !(trans&((1U<<15)|(1U<<9))) && !ctl2 &&
        (lanes==1 || lanes==2 || lanes==4) && ((buf>>1)&7)==lanes-1 &&
        (buf&(typeC && tc->ownershipOptional?0x80000080U:0x800000C0U))==
            (typeC && !tc->ownershipOptional?0x80000040U:0x80000000U) &&
        (clock>>28)==ddi+(typeC?3:1) && (!typeC || clock==tc->transClock) &&
        (tp&~((1U<<18)|(panel?1U<<6:0)))==0x80000300U &&
        !(dss1&0xF0300000U) && !(dss2&0x80008000U) &&
        !(dm&0x81000000U) && (dm&0xFFFFFF) &&
        dn && !(dn&0xFF000000U) && lm && !(lm&0xFF000000U) && ln && !(ln&0xFF000000U) &&
        (dm&0xFFFFFF)<=dn;
}
enum class GOPProtocol : uint32_t {TMDS,DP,EDP};
// Source and power must remain unchanged while only the plane is reinitialized.
// This is an early, serialized bootstrap transaction, not a modeset or link
// adoption certificate. A changed link stops further writes, including rollback.
template<class IO> bool dpSourceUnchanged(IO &io,const DisplayReadout &r,unsigned pipe,unsigned ddi,
                                         uint32_t addedAUXRequest=0,const Display13TC::Boot *tc=nullptr,bool panel=false) {
    // addedAUXRequest is the existing Combo boot-lease transition only.
    // TC lifetime uses Mode's own AUX lease, not the Combo MainLinkAUX slot.
    if(!dpSource(r,pipe,ddi,tc,panel) || (addedAUXRequest && (ddi>=2 || addedAUXRequest!=(2U<<(2*ddi)))))return false;
    if(ddi>=3 && !tc->unchanged(io))return false;
    const uint32_t bank=pipe*0x1000;
    // Recheck power before touching pipe-local registers. A captured snapshot
    // is not permission to read a bank whose well has since disappeared.
    const uint32_t regs[] {0x45404,0x45454,0x45444,0x42000,
        0x60400+bank,0x60404+bank,0x60540+bank,0x60030+bank,0x60034+bank,
        0x60040+bank,0x60044+bank,0x70008+bank,0x46140+4*pipe,0x64000+0x100*ddi,
        0x78000+0x200*pipe,0x78004+0x200*pipe};
    for(auto reg:regs) {
        uint32_t expected=0;
        if(ddi>=3 && reg==0x64000+0x100*ddi)expected=tc->buffer;
        else if(!value(r,reg,expected))return false;
        if(!io.readable(reg))return false;
        if(reg==Display13Power::AUX)expected|=addedAUXRequest;
        if(io.read(reg)!=expected)return false;
    }
    return true;
}
// The GOP is already driving DP. Keep its physical AUX domain on without
// inventing a native transaction reference or claiming the firmware request.
// Caller holds the display owner lock throughout both proof and acquisition.
template<class IO> Display13Power::Result holdDPBootAUX(IO &io,const DisplayReadout &boot,
    Display13Power::NativeBridge &bridge,Display13Power::MainLinkAUX &link,unsigned ddi,bool panel=false) {
    using namespace Display13Power;Result r;
    if(ddi>=2 || boot.activePipes!=1 || !dpSource(boot,0,ddi,nullptr,panel) || !dpSourceUnchanged(io,boot,0,ddi,0,nullptr,panel) ||
       bridge.bootPort!=ddi || bridge.bootAdopted || !bridge.bootPGProtected || !bridge.bootDDIProtected ||
       !bridge.outputHeld(io,ddi,0) || link.owner || link.bootPending || link.lease.owner)return r;
    uint32_t aux=0;
    if(!value(boot,AUX,aux) || !(aux&(1U<<(2*ddi))))return r; // No active-link cold power-up.
    r=acquire(io,bridge.state,link.lease,auxDomain(ddi));
    if(r.status!=Status::Complete)return r;
    link.port=ddi;link.bootPending=true;
    if(!dpSourceUnchanged(io,boot,0,ddi,2U<<(2*ddi),nullptr,panel) || !held(io,bridge.state,link.lease.domains)) {
        // Do not release a newly acquired request under a still-active GOP.
        bridge.state.faulted=true;r.status=Status::RequestLost;
    }
    return r;
}
// Bind the protected lease to the actual, independently validated native port.
// The caller retains that object on success and then adopts the boot path.
// No hardware request or native AUX transaction counter changes at handoff.
template<class IO> Display13Power::Result adoptDPBootAUX(IO &io,const DisplayReadout &boot,
    Display13Power::NativeBridge &bridge,Display13Power::MainLinkAUX &link,uintptr_t target,unsigned ddi,bool panel=false) {
    using namespace Display13Power;Result r;
    if(!target || ddi>=2 || boot.activePipes!=1 || !dpSource(boot,0,ddi,nullptr,panel) || !dpSourceUnchanged(io,boot,0,ddi,0,nullptr,panel) ||
       bridge.bootPort!=ddi || bridge.bootAdopted || !bridge.bootPGProtected || !bridge.bootDDIProtected ||
       !bridge.outputHeld(io,ddi,0) || !link.bootPending || link.owner || link.port!=ddi ||
       link.lease.owner!=&bridge.state || link.lease.domains!=dependencies(auxDomain(ddi)) ||
       !held(io,bridge.state,link.lease.domains))return r;
    link.owner=target;link.bootPending=false;r.status=Status::Complete;return r;
}
enum class LinearStatus : uint32_t {
    NotAdmitted, Ready, Success, NotSingleHDMI, NotLinearFHD, OtherPlaneActive,
    StateChanged, NoVblank, DisableTimeout, EnableTimeout, RollbackFailed, InvalidDPSource, InvalidTCSource
};
struct LinearPlan {
    uint32_t pipe {}, chicken {}, arb {}, control {}, stride {}, position {}, size {}, offset {}, surface {};
    XELPGeometry::Linear geometry;
};
struct LinearResult {
    uint32_t version {1};
    LinearStatus status {LinearStatus::NotAdmitted};
    uint32_t pipe {}, writes {}, framesBefore {}, framesAfter {}, restored {};
};

// Qualify a firmware pipe scaler separately from an unscaled linear GOP.
// PIPESRC is width:height minus one; scaler WIN_SZ is width:height, not
// minus one. Only one pipe-bound scaler covering the complete output is
// accepted. Plane-bound/planar/programmed-factor scaling is not this contract.
inline bool bootGeometry(const DisplayReadout &r,unsigned pipe,const XELPGeometry::Linear &geometry,
                         uint32_t ht,uint32_t vt) {
    if(XELPGeometry::timing(geometry,ht,vt))return true;
    if(!geometry.valid || pipe>=3 || ht==UINT32_MAX || vt==UINT32_MAX ||
       (ht&0xC000C000) || (vt&0xC000C000))return false;
    uint32_t src=0;
    if(!value(r,0x6001C+0x1000*pipe,src) ||
       src!=(((geometry.width-1)<<16)|(geometry.height-1)))return false;
    const auto width=(ht&0x3FFF)+1,height=(vt&0x3FFF)+1;
    if(((ht>>16)&0x3FFF)+1<width || ((vt>>16)&0x3FFF)+1<height ||
       width<geometry.width || height<geometry.height)return false;
    unsigned active=0;
    for(unsigned scaler=0;scaler<2;++scaler) {
        const auto base=0x68170+0x800*pipe+0x100*scaler;
        uint32_t ctl=0,pos=0,size=0;
        if(!value(r,base+0x10,ctl) || !value(r,base,pos) || !value(r,base+4,size))return false;
        if(!(ctl&0x80000000U))continue;
        if((ctl&((7U<<25)|(1U<<29)|(1U<<19))) || pos || size!=((width<<16)|height))return false;
        ++active;
    }
    return active==1;
}
template<class IO> bool bootGeometryUnchanged(IO &io,const DisplayReadout &r,unsigned pipe) {
    // The accepted scaler stays live while the primary plane changes GGTT mode.
    // Any captured source/timing/scaler drift invalidates the handoff.
    const uint32_t regs[] {0x60000+0x1000*pipe,0x6000C+0x1000*pipe,0x6001C+0x1000*pipe,
        0x68170+0x800*pipe,0x68174+0x800*pipe,0x68180+0x800*pipe,
        0x68270+0x800*pipe,0x68274+0x800*pipe,0x68280+0x800*pipe};
    for(auto reg:regs) {uint32_t v=0;if(value(r,reg,v) && io.read(reg)!=v)return false;}
    return true;
}

// First Display 13 operation: reinitialize an existing linear RGB primary
// plane on the explicitly selected Combo, or fixed TC with a live link
// certificate. The default remains B for
// the original standalone diagnostics. No new PLL/PHY/power-well cold
// initialization, a new GGTT mapping, full modeset or accelerated scanout.
inline LinearStatus linearPlan(const DisplayReadout &r, LinearPlan &p,unsigned ddi=1,GOPProtocol protocol=GOPProtocol::TMDS,
                               const Display13TC::Boot *tc=nullptr) {
    p={};
    if (ddi>=7 || r.status!=ReadoutStatus::Complete || !r.activePipes || (r.activePipes&~7U) ||
        (r.activePipes & (r.activePipes-1)) || (r.activePipes&~r.poweredPipes) ||
        (protocol!=GOPProtocol::TMDS && protocol!=GOPProtocol::DP && protocol!=GOPProtocol::EDP) ||
        (protocol==GOPProtocol::TMDS && ddi==1 && r.activePipes!=r.hdmiBPipes))
        return LinearStatus::NotSingleHDMI;
    while (!(r.activePipes & (1U<<p.pipe))) ++p.pipe;
    if (p.pipe>=3) return LinearStatus::NotSingleHDMI;
    if(ddi>=3 && (!tc || !tc->valid() || tc->port.ddi!=ddi || tc->pipe!=p.pipe ||
       (protocol!=GOPProtocol::DP && tc->ownershipOptional)))
        return LinearStatus::InvalidTCSource;
    const uint32_t delta=0x1000*p.pipe;
    uint32_t trans {}, cursor {}, surfLive {}, plane {}, pipeConf {}, htotal {}, vtotal {};
    if (!value(r,0x42084,p.chicken) || !value(r,0x70028+delta,p.arb) ||
        !value(r,0x70180+delta,p.control) || !value(r,0x70188+delta,p.stride) ||
        !value(r,0x7018C+delta,p.position) || !value(r,0x70190+delta,p.size) ||
        !value(r,0x701A4+delta,p.offset) || !value(r,0x7019C+delta,p.surface) ||
        !value(r,0x701AC+delta,surfLive) || !value(r,0x60400+delta,trans) ||
        !value(r,0x70008+delta,pipeConf) || !value(r,0x60000+delta,htotal) || !value(r,0x6000C+delta,vtotal))
        return LinearStatus::NotLinearFHD;
    if(!(trans&(1U<<31)) || ((trans>>27)&15)!=ddi+1 ||
       (protocol==GOPProtocol::TMDS && ((trans>>24)&7)>1))
        return LinearStatus::NotSingleHDMI;
    if(protocol!=GOPProtocol::TMDS && !dpSource(r,p.pipe,ddi,tc,protocol==GOPProtocol::EDP))return LinearStatus::InvalidDPSource;
    p.geometry=XELPGeometry::linear(p.control,p.stride,p.size,p.position,p.offset);
    if(!p.geometry.valid || (p.surface&4095) || p.surface!=surfLive ||
       !XELPGeometry::rgbLinkDepth(trans) || (pipeConf&0xC0000000)!=0xC0000000 ||
       !bootGeometry(r,p.pipe,p.geometry,htotal,vtotal))return LinearStatus::NotLinearFHD;
    // DPT disable is global on Display 13. Exclude all other active planes
    // and cursors, including planes on powered but inactive pipes.
    for (unsigned pipe=0;pipe<3;++pipe) {
        if (!(r.poweredPipes & (1U<<pipe))) continue;
        const auto d=0x1000*pipe;
        if (!value(r,0x70080+d,cursor) || (cursor & 0x3f))
            return LinearStatus::OtherPlaneActive;
        if (pipe!=p.pipe && (!value(r,0x70180+d,plane) || (plane & 0x80000000)))
            return LinearStatus::OtherPlaneActive;
        for (unsigned n=2;n<=7;++n)
            if (!value(r,0x70080+0x100*n+d,plane) || (plane & 0x80000000))
                return LinearStatus::OtherPlaneActive;
    }
    return LinearStatus::Ready;
}

template<class IO> LinearResult reinitializeLinear(IO &io, const DisplayReadout &r, bool admitted,unsigned ddi=1,
                                                  GOPProtocol protocol=GOPProtocol::TMDS,const Display13TC::Boot *tc=nullptr) {
    LinearResult out;
    if (!admitted) return out;
    LinearPlan p;
    out.status=linearPlan(r,p,ddi,protocol,tc);
    if(out.status!=LinearStatus::Ready)return out;
    out.pipe=p.pipe;
    const auto d=0x1000*p.pipe;
    const uint32_t ctl=0x70180+d, surf=0x7019C+d, frame=0x70040+d;
    const auto write=[&](uint32_t reg,uint32_t v){io.write(reg,v);++out.writes;io.read(reg);};
    bool linkChanged=false;
    const auto linkStable=[&] {
        if(!bootGeometryUnchanged(io,r,p.pipe))linkChanged=true;
        if(protocol!=GOPProtocol::TMDS && !dpSourceUnchanged(io,r,p.pipe,ddi,0,tc,protocol==GOPProtocol::EDP))linkChanged=true;
        if(ddi>=3 && (!tc || !tc->unchanged(io)))linkChanged=true;
        return !linkChanged;
    };
    // Check all captured words used by the transaction before the first write.
    uint32_t trans=0;value(r,0x60400+d,trans);
    const Register expected[] {{0x60400+d,trans},{0x42084,p.chicken},{0x70028+d,p.arb},{ctl,p.control},
        {0x70188+d,p.stride},{0x7018C+d,p.position},{0x70190+d,p.size},
        {0x701A4+d,p.offset},{surf,p.surface}};
    for(auto pair:expected)if(io.read(pair.offset)!=pair.value){out.status=LinearStatus::StateChanged;return out;}
    if(!linkStable()){out.status=LinearStatus::StateChanged;return out;}
    const auto waitFrames=[&](uint32_t start) {
        for(unsigned i=0;i<1000;++i) {
            if(!linkStable())return false;
            const auto now=io.read(frame);
            if(now==UINT32_MAX)return false;
            if(uint32_t(now-start)>=2 && uint32_t(now-start)<0x10000000)return true;
            io.delay(); // 100us; at most100ms at the qualified FHD60 timing.
        }
        return false;
    };
    out.framesBefore=io.read(frame);
    if(out.framesBefore==UINT32_MAX || !waitFrames(out.framesBefore)) {
        out.status=linkChanged ? LinearStatus::StateChanged : LinearStatus::NoVblank;return out;
    }
    const auto rollback=[&] {
        if(!linkStable()){out.status=LinearStatus::StateChanged;return;}
        write(ctl,p.control & ~0x80000000U);write(surf,p.surface);
        const bool stopped=waitFrames(io.read(frame));
        if(linkChanged){out.status=LinearStatus::StateChanged;return;}
        write(0x42084,p.chicken);write(0x70028+d,p.arb);
        write(ctl,p.control);write(surf,p.surface);
        const bool running=waitFrames(io.read(frame));
        if(linkChanged){out.status=LinearStatus::StateChanged;return;}
        out.restored=stopped && running && io.read(ctl)==p.control && io.read(surf)==p.surface &&
            io.read(0x42084)==p.chicken && io.read(0x70028+d)==p.arb;
        if(!out.restored)out.status=LinearStatus::RollbackFailed;
    };
    write(ctl,p.control & ~0x80000000U);write(surf,p.surface);
    if(!waitFrames(io.read(frame)) || (io.read(ctl)&0x80000000U)) {
        out.status=LinearStatus::DisableTimeout;rollback();return out;
    }
    // Linux v6.12 intel_dpt_configure / Wa_22012358565 / plane_ctl_icl.
    write(0x42084,p.chicken | (1U<<30));
    write(0x70028+d,p.arb | (1U<<13));
    write(0x70188+d,p.stride);write(0x7018C+d,p.position);
    write(0x70190+d,p.size);write(0x701A4+d,p.offset);
    const auto control=(p.control & ~(7U<<28)) | (1U<<28);
    write(ctl,control);write(surf,p.surface);
    if(!waitFrames(io.read(frame)) || io.read(ctl)!=control || io.read(0x701AC+d)!=p.surface ||
        io.read(0x42084)!=(p.chicken|(1U<<30)) || io.read(0x70028+d)!=(p.arb|(1U<<13))) {
        out.status=LinearStatus::EnableTimeout;rollback();return out;
    }
    out.framesAfter=io.read(frame);out.status=LinearStatus::Success;
    return out;
}

struct GGTTBlock {
    uint64_t first {}, last {}, zero {}, present {}, other {};
};
struct GGTTReadout {
    uint64_t version {1}, status {}, pages {}, zero {}, present {}, other {}, zeroStart {}, zeroPages {};
    GGTTBlock blocks[32] {}; // 8MiB aperture regions, not the whole PTE table.
};
static_assert(sizeof(GGTTReadout)==168*8,"AlderLakeN GGTT readout layout");

// Diagnostic only: nonzero/scratch entries never authorize a reservation.
template<class IO> GGTTReadout readGGTT(IO &io, bool admitted) {
    GGTTReadout out;
    if(!admitted)return out;
    out.status=2;
    constexpr uint32_t pages=0x10000000/4096;
    if(!io.readable(0x800000+pages*8-1))return out;
    uint64_t run=0,start=0;
    for(uint32_t i=0;i<pages;++i) {
        const auto pte=io.readPTE(0x800000+8*i);
        if(pte==UINT64_MAX)return out;
        auto &block=out.blocks[i/2048];
        if(i%2048==0)block.first=pte;
        block.last=pte;++out.pages;
        if(!pte) {
            ++out.zero;++block.zero;
            if(!run)start=uint64_t(i)*4096;
            if(++run>out.zeroPages) {out.zeroStart=start;out.zeroPages=run;}
        } else {
            run=0;
            if(pte&1) {++out.present;++block.present;}
            else {++out.other;++block.other;}
        }
    }
    out.status=1;return out;
}

constexpr uint32_t ScanoutBytes = 0x800000; // Rounded FHD XRGB8888 buffer.
constexpr uint32_t ScanoutSlot = 0x04000000; // Temporary firmware lease, away from GOP and GTT storage.
constexpr uint32_t ScanoutPages = 2 * ScanoutBytes / 4096;
struct GGTTLease {
    uint64_t version {1}, status {}, slot {ScanoutSlot}, bytes {2*ScanoutBytes};
    uint64_t stolenBase {}, verifiedPages {}, gopEnd {}, first {}, last {};
};
static_assert(sizeof(GGTTLease)==9*8,"AlderLakeN GGTT lease layout");

// This exact pre-native-driver firmware lease is not a free-space allocator.
// Verify the physical stolen mapping in full, exclude the visible GOP
// allocation and GGC-reported GTT reserve, and save every leased
// PTE. Uninitialized aperture tail or generic scratch entries are not leased.
template<class IO> GGTTLease leaseGOPGGTT(IO &io, const DisplayReadout &r,
                                       uint64_t *predecessor, bool admitted,const XELPMemory::Layout &memory) {
    GGTTLease out;
    if(!admitted)return out;
    out.status=2;
    LinearPlan p;
    if(!predecessor || linearPlan(r,p)!=LinearStatus::Ready)return out;
    // Pattern diagnostics deliberately retain their FHD payload layout.
    if(p.geometry.width!=1920 || p.geometry.height!=1080 || p.geometry.pitch!=7680)return out;
    out.gopEnd=uint64_t(p.surface)+p.geometry.bytes;
    if(out.gopEnd>ScanoutSlot || !memory.range(ScanoutSlot,2*ScanoutBytes,true) ||
       !io.readable(0x800000+(memory.bytes/4096)*8-1))return out;
    const uint64_t base=memory.base;
    out.stolenBase=base;out.status=3;
    for(uint32_t i=0;i<memory.bytes/4096;++i) {
        const auto expected=(base+uint64_t(i)*4096)|1;
        if(io.readPTE(0x800000+8*i)!=expected)return out;
        ++out.verifiedPages;
        if(i>=ScanoutSlot/4096 && i<(ScanoutSlot+2*ScanoutBytes)/4096)
            predecessor[i-ScanoutSlot/4096]=expected;
    }
    out.first=predecessor[0];out.last=predecessor[ScanoutPages-1];out.status=1;
    return out;
}
enum class ScanoutStatus : uint64_t {
    NotAdmitted, BadMemory, BusyGGTT, InvalidDMA, StateChanged, PTEReadback,
    ApertureMismatch, FlipTimeout, RestoreFailed, Success, AllocationFailed
};
struct ScanoutResult {
    uint64_t version {1}, status {}, slot {ScanoutSlot}, bytes {ScanoutBytes}, pages {ScanoutPages};
    uint64_t pteWrites {}, pteErrors {}, apertureSamples {}, apertureErrors {}, flips {};
    uint64_t framesBefore {}, framesAfter {}, originalSurface {}, planeRestored {}, ptesRestored {}, retained {};
    uint64_t dmaFirst[2] {}, dmaLast[2] {}, pciCommand {}, descriptorOptions {}, holdMilliseconds {}, buffers {};
};
static_assert(sizeof(ScanoutResult)==24*8,"AlderLakeN scanout property layout");

inline uint32_t patternPixel(unsigned buffer, uint32_t index) {
    if(index>=1920*1080)return 0xFF595959;
    const uint32_t x=index%1920,y=index/1920;
    const uint32_t bars[] {0xFFFFFF,0xFFFF00,0x00FFFF,0x00FF00,0xFF00FF,0xFF0000,0x0000FF,0};
    // Full-intensity primaries qualify geometry independently of inherited
    // GOP midtone CSC/gamma. Midtone color accuracy needs its own pipeline test.
    return 0xFF000000U | (buffer ? (((x/64)^(y/64))&1 ? 0x00FF00 : 0x0000FF) : bars[x/240]);
}

// The caller pins DMA memory, checks PCI/aperture and serializes before other
// GPU drivers. A nonzero predecessor requires the exact firmware lease above;
// arbitrary valid/scratch PTEs never constitute free space. Restore the GOP
// surface before restoring the saved PTEs or removing a zero-PTE reservation.
template<class IO> ScanoutResult testScanout(IO &io, const DisplayReadout &r,
                                           const uint64_t *dma, bool admitted,
                                           const uint64_t *predecessor=nullptr) {
    ScanoutResult out;
    if(!admitted)return out;
    LinearPlan p;
    if(linearPlan(r,p)!=LinearStatus::Ready || !dma) {
        out.status=uint64_t(ScanoutStatus::BadMemory);return out;
    }
    const auto delta=0x1000*p.pipe;
    const uint32_t surf=0x7019C+delta,live=0x701AC+delta,frame=0x70040+delta;
    const uint32_t pteBase=0x800000+(ScanoutSlot>>9);
    out.originalSurface=p.surface;
    if(p.surface>=ScanoutSlot || !io.readable(pteBase+ScanoutPages*8-1)) {
        out.status=uint64_t(ScanoutStatus::BadMemory);return out;
    }
    for(uint32_t i=0;i<ScanoutPages;++i) {
        if(io.readPTE(pteBase+8*i)!=(predecessor ? predecessor[i] : 0)) {
            out.status=uint64_t(ScanoutStatus::BusyGGTT);return out;
        }
        if(!dma[i] || (dma[i]&4095) || dma[i]>0x7FFFFFF000ULL) {
            out.status=uint64_t(ScanoutStatus::InvalidDMA);return out;
        }
    }
    for(unsigned b=0;b<2;++b) {
        out.dmaFirst[b]=dma[b*ScanoutPages/2];out.dmaLast[b]=dma[(b+1)*ScanoutPages/2-1];
    }
    if(io.read(surf)!=p.surface || io.read(live)!=p.surface || io.read(0x70180+delta)!=p.control) {
        out.status=uint64_t(ScanoutStatus::StateChanged);return out;
    }
    const auto waitFrames=[&](uint32_t start) {
        for(unsigned n=0;n<1000;++n) {
            const auto now=io.read(frame);
            if(now==UINT32_MAX)return false;
            if(uint32_t(now-start)>=2 && uint32_t(now-start)<0x10000000)return true;
            io.delay();
        }
        return false;
    };
    out.framesBefore=io.read(frame);
    if(out.framesBefore==UINT32_MAX || !waitFrames(uint32_t(out.framesBefore))) {
        out.status=uint64_t(ScanoutStatus::FlipTimeout);return out;
    }
    // Gen12 integrated GGTT uses DMA address | PRESENT, not PPGTT flags.
    // UC 64-bit stores/readbacks (Linux gen8_ggtt_pte_encode/set_pte) suffice;
    // the Gen8 invalidate write is only for platforms with a WC GGTT mapping.
    for(uint32_t i=0;i<ScanoutPages;++i) {
        io.writePTE(pteBase+8*i,dma[i]|1);++out.pteWrites;
        if(io.readPTE(pteBase+8*i)!=(dma[i]|1))++out.pteErrors;
    }
    io.fence();
    bool planeTouched=false;
    out.status=uint64_t(ScanoutStatus::PTEReadback);
    if(!out.pteErrors) {
        out.status=uint64_t(ScanoutStatus::ApertureMismatch);
        // First/last dword of every owned page proves both mappings and CPU
        // visibility through the GPU aperture before any plane write.
        const uint32_t sampleOffsets[] {0,4092};
        for(uint32_t i=0;i<ScanoutPages;++i)for(auto off:sampleOffsets) {
            const auto byte=i*4096+off;
            const auto expected=patternPixel(byte/ScanoutBytes,(byte%ScanoutBytes)/4);
            ++out.apertureSamples;
            if(io.apertureRead(byte)!=expected)++out.apertureErrors;
        }
        if(!out.apertureErrors) {
            out.status=uint64_t(ScanoutStatus::FlipTimeout);
            const unsigned sequence[] {0,1,0};
            for(auto buffer:sequence) {
                const auto address=ScanoutSlot+buffer*ScanoutBytes;
                io.write(surf,address);io.read(surf);planeTouched=true;
                if(!waitFrames(io.read(frame)) || io.read(live)!=address)break;
                ++out.flips;io.hold(3000);out.holdMilliseconds+=3000;
            }
            if(out.flips==3)out.status=uint64_t(ScanoutStatus::Success);
        }
    }
    if(planeTouched) {
        io.write(surf,p.surface);io.read(surf);
        out.planeRestored=waitFrames(io.read(frame)) && io.read(live)==p.surface;
    } else out.planeRestored=1;
    // A failed restore keeps allocations and PTEs pinned until reboot.
    if(!out.planeRestored) {
        out.status=uint64_t(ScanoutStatus::RestoreFailed);out.retained=1;return out;
    }
    for(uint32_t i=0;i<ScanoutPages;++i) {
        io.writePTE(pteBase+8*i,predecessor ? predecessor[i] : 0);++out.pteWrites;
    }
    io.fence();out.ptesRestored=1;
    for(uint32_t i=0;i<ScanoutPages;++i)
        if(io.readPTE(pteBase+8*i)!=(predecessor ? predecessor[i] : 0))out.ptesRestored=0;
    if(!out.ptesRestored) {out.status=uint64_t(ScanoutStatus::RestoreFailed);out.retained=1;}
    out.framesAfter=io.read(frame);
    return out;
}
}
#endif
