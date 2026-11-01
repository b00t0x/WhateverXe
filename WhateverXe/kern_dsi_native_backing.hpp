// Exact TGL IntelScaledModeData wire record for a pending DSI scanout.
// Allocation/free use the native allocscanmem/freescanmem platform functions.
// The adapter must retain the record and serialize the native display pipe;
// allocating also changes its memory/map references, before FB publication.
#ifndef KERN_DSI_NATIVE_BACKING_HPP
#define KERN_DSI_NATIVE_BACKING_HPP
#include "kern_dsi_profile.hpp"
#include "kern_display13_fb_memory.hpp"
#include <stddef.h>
namespace XELP { namespace DSI { namespace NativeBacking {
struct Record {
    uint32_t reserved {},columns {},rows {},flags {};
    uint64_t map {},memory {},gpu {};
    uint32_t bytes {},pitch {},reserved30 {},reserved34 {},refs {},kind {};
};
static_assert(sizeof(Record)==64 && offsetof(Record,map)==0x10 && offsetof(Record,refs)==0x38,"native scanout ABI");
// Native tile mode 3 gives a 128-byte pitch unit and 32-row tiles. This is
// uncompressed Y, the format admitted by the shared Display13 WM backend.
// Flags zero avoids the optional fence allocation/free path.
inline bool request(const Profile &p,uint32_t nativeTileMode,Record &out) {
    if(!p.valid() || !p.mode.valid() || p.mode.width>8192 || p.mode.height>8192 || nativeTileMode!=3)return false;
    Record r;r.columns=(uint32_t(p.mode.width)*4+127)/128;r.rows=(uint32_t(p.mode.height)+31)/32;r.kind=1;
    // Leave bytes zero as the native FB does: allocator computes tiles*4096.
    out=r;return true;
}
inline bool allocated(const Record &r,const Profile &p,const Display13FBMemory::SurfaceReadout &proof) {
    Record expected;if(!request(p,3,expected))return false;
    return r.columns==expected.columns && r.rows==expected.rows && !r.flags && r.kind==1 && r.refs==1 &&
        !r.reserved && !r.reserved30 && !r.reserved34 && !r.bytes &&
        proof.status==Display13FBMemory::SurfaceStatus::Complete &&
        proof.record==reinterpret_cast<uintptr_t>(&r) && proof.refs==r.refs && proof.kind==r.kind && proof.flags==r.flags &&
        proof.map==r.map && proof.memory==r.memory &&
        proof.gpu==r.gpu && proof.bytes==uint64_t(r.columns)*r.rows*4096 &&
        proof.pitch==r.pitch && r.pitch==r.columns*128 && proof.verifiedPages==uint64_t(r.columns)*r.rows;
}
}}}
#endif
