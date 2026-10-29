// GOP-to-native scanout transaction for the fixed DSI0/pipe-A mode. Register
// ordering follows Linux v6.12 icl_plane_update_noarm/arm. This owns no Apple
// objects: the adapter proves/pins actual GGTT backing, leases, IRQ and DDB,
// serializes native writers, and publishes the resulting native caches.
#ifndef KERN_DSI_SURFACE_HPP
#define KERN_DSI_SURFACE_HPP
#include "kern_dsi_session.hpp"
#include "kern_display13_wm.hpp"
#include "kern_display13_dbuf.hpp"
namespace XELP { namespace DSI { namespace Surface {
struct Plane {
    uint32_t control {},color {},stride {},size {},surface {},position {},offset {};
};
struct Backing {uint64_t handle {},gpu {},bytes {},pitch {};};
enum class Status : uint32_t {Input,Source,Watermark,Plane,Completion,Complete};
struct Result {
    Status status {Status::Input};uint32_t writes {};Ticket ticket;
    Display13WM::Program wm;Display13WM::Latch latch;
};
constexpr uint32_t Registers[]={0x70188,0x7018C,0x70190,0x701A4,0x701CC,0x70180,0x7019C};
inline void values(const Plane &p,uint32_t (&v)[7]) {
    v[0]=p.stride;v[1]=p.position;v[2]=p.size;v[3]=p.offset;v[4]=p.color;v[5]=p.control;v[6]=p.surface;
}
inline bool shape(const Plane &p,const Profile &profile) {
    // The existing Display13 WM backend admits linear and uncompressed Y.
    // Keep the P2 fixed RGB8888 geometry; rotation/scaling/color modes are not
    // implicitly implemented by copying the donor's register cache.
    return profile.valid() && profile.mode.valid() && profile.mode.width<=8192 && profile.mode.height<=8192 &&
        Display13WM::supportedRGBControl(p.control) && (p.control&(1U<<31)) &&
        ((p.control>>23)&31)==8 && !(p.control&((3U<<21)|(1U<<9)|(1U<<8))) &&
        !p.position && !p.offset && p.size==((uint32_t(profile.mode.height)-1)<<16 | (uint32_t(profile.mode.width)-1)) &&
        p.stride && p.stride<=2047 && !(p.surface&4095);
}
inline bool target(const Plane &p,const Profile &profile,const Backing &b) {
    if(!shape(p,profile) || !b.handle || !b.bytes || (b.bytes&4095) || b.gpu!=p.surface ||
       b.gpu>=0x100000000ULL || b.bytes>0x100000000ULL-b.gpu ||
       Display13WM::arbitrationControl(p.control)!=p.control)return false;
    const bool tiled=((p.control>>10)&7)==4;
    const auto pitch=uint64_t(p.stride)*(tiled?128:64);
    const auto rows=tiled?((profile.mode.height+31)&~31U):profile.mode.height;
    return b.pitch==pitch && pitch>=uint64_t(profile.mode.width)*4 && pitch*rows<=b.bytes;
}
template<class IO> bool word(IO &io,uint32_t reg,uint32_t mask,uint32_t expected) {
    if(!io.readable(reg))return false;const auto v=io.read(reg);
    return v!=UINT32_MAX && (v&mask)==expected;
}
template<class IO> bool matches(IO &io,const Plane &p,bool live) {
    uint32_t v[7];values(p,v);
    for(unsigned i=0;i<7;++i)if(!word(io,Registers[i],UINT32_MAX,v[i]))return false;
    return !live || word(io,0x701AC,UINT32_MAX,p.surface);
}
template<class IO> bool source(IO &io,const Readout &link,bool stoppingCursor=false) {
    if(link.identity.pipe!=0 || !sourceUnchanged(io,link) || !word(io,0x42084,1U<<30,1U<<30) ||
       !word(io,0x6001C,UINT32_MAX,((link.htotal&65535)<<16)|(link.vtotal&65535)) ||
       !word(io,0x6B010,UINT32_MAX,link.vtotal) || !word(io,0x6B028,UINT32_MAX,0) ||
       !word(io,0x60400,1U<<31,0) || !word(io,0x7B808,0xC0000000U,0) ||
       (!stoppingCursor && !word(io,0x70080,63,0)) ||
       !word(io,0x78000,0xF0300000U,0) || !word(io,0x78004,0x80008000U,0))return false;
    for(unsigned p=1;p<7;++p)if(!word(io,0x70180+0x100*p,1U<<31,0))return false;
    for(unsigned s=0;s<2;++s)if(!word(io,0x68180+0x100*s,1U<<31,0))return false;
    for(unsigned d=0;d<3;++d)if(!word(io,0x70B08+0x100*d,0x80000001U,0))return false;
    return true;
}
// IO::ready revalidates the adapter's actual object/backing/resource binding.
// Hold its mode serialization and power through the final latch; do not hold
// an IRQ spinlock while waiting. Failure after begin retains both backings and
// the lease in Session. This never calls cleanup or reports a successful stop.
template<class IO> Result change(IO &io,Session &session,const Readout &link,const Profile &profile,
                                const Plane &current,const Plane &next,const Backing &backing,
                                const Display13WM::Snapshot &wm) {
    Result r;
    if(!io.ready() || !link.matches(profile) || !session.identity.same(link.identity) ||
       !shape(current,profile) || !target(next,profile,backing) || next.surface==current.surface ||
       ((next.control^current.control)&~((7U<<28)|(7U<<10))) ||
       (next.color!=current.color && next.color!=(current.color|(1U<<13))) ||
       wm.pipeIndex!=0 || wm.transcoders.dsi0Pipe!=0 || wm.cursorActive || !wm.primary ||
       wm.primary->width!=profile.mode.width || wm.primary->height!=profile.mode.height ||
       wm.primary->htotal!=profile.mode.width+profile.mode.hblank ||
       wm.primary->pixelHz!=uint64_t(profile.mode.clockKHz)*1000 || wm.pipePixelHz!=wm.primary->pixelHz ||
       wm.primary->yTiled!=(((next.control>>10)&7)==4) || wm.primarySurface!=next.surface ||
       wm.nativeClockHz!=io.nativeClockHz() || !Display13WM::clockFits(wm,wm.nativeClockHz))return r;
    const auto ddb=Display13DBUF::decode(wm.primaryDDB);
    if(!Display13DBUF::valid(ddb) || Display13DBUF::encoded(ddb)!=wm.primaryDDB ||
       ddb.end-ddb.start!=wm.primary->allocation)return r;
    r.status=Status::Source;
    if(!source(io,link) || !matches(io,current,true) ||
       !word(io,0x7027C,UINT32_MAX,wm.primaryDDB) || !word(io,0x7017C,UINT32_MAX,wm.cursorDDB) ||
       !word(io,0x45004,UINT32_MAX,wm.ipc))return r;
    for(auto reg:Registers)if(!io.readable(reg))return r;
    if(!io.readable(0x45270) || !io.ready() || !session.beginSurface(backing.handle))return r;
    r.ticket=session.ticket();
    const auto fail=[&]() {session.abort(r.ticket,SessionError::Switch);return r;};
    r.status=Status::Watermark;
    r.wm=Display13WM::program(io,*wm.primary,false,wm.wm0Only,0);r.writes+=r.wm.writes;
    if(r.wm.status!=Display13WM::Status::Complete)return fail();
    io.write(0x45270,wm.pipeLineTime);++r.writes;
    if(!word(io,0x45270,UINT32_MAX,wm.pipeLineTime) || !source(io,link) || !matches(io,current,true) || !io.ready())return fail();
    r.status=Status::Plane;
    uint32_t v[7];values(next,v);
    // All non-arming words precede control/surface. The existing enabled
    // plane remains enabled; SURF is the final write that arms the change.
    for(unsigned i=0;i<7;++i) {
        io.write(Registers[i],v[i]);++r.writes;
        if(!word(io,Registers[i],UINT32_MAX,v[i]))return fail();
    }
    r.status=Status::Completion;r.latch=Display13WM::latch(io,wm);
    if(r.latch.status!=Display13WM::Status::Complete || !io.ready() || !source(io,link) || !matches(io,next,true))return fail();
    SurfaceProof proof {r.ticket,link.identity,backing.handle,true,true,true};
    if(!session.completeSurface(proof))return fail();
    r.status=Status::Complete;return r;
}
}}}
#endif
