// Native TGL framebuffer aperture ABI, using physical Display 13 boot
// display. This borrows an existing stolen pool; it never allocates or writes
// GGTT entries and is not an accelerator memory manager.
#ifndef KERN_DISPLAY13_FB_MEMORY_HPP
#define KERN_DISPLAY13_FB_MEMORY_HPP
#include "kern_dsi_readout.hpp"

#include <stdint.h>
#include "kern_xelp_memory_layout.hpp"
#include "kern_xelp_geometry.hpp"
#include "kern_display13_tc.hpp"
#include "kern_alderlake_n.hpp"
#include <stddef.h>
#include <string.h>

namespace Display13FBMemory {
// Four RGB pixels at a Y-tile origin (32 pixels x 32 lines), or a
// 16-byte aligned linear position. Sample origins avoid guessing the tile's
// internal swizzle. Stride units are tiles for Y and 64 bytes for linear.
inline bool scanoutSampleOffset(bool yTiled,uint32_t stride,uint32_t width,uint32_t height,
                               uint32_t origin,uint32_t x,uint32_t y,uint64_t &offset) {
    if(!stride || stride>4095 || !width || !height || width>8192 || height>8192 ||
       x>=width || width-x<4 || y>=height)return false;
    const uint64_t sx=uint64_t(origin&65535)+x,sy=uint64_t(origin>>16)+y;
    const uint64_t pitch=uint64_t(stride)*(yTiled ? 128 : 64);
    if((sx+4)*4>pitch)return false;
    if(yTiled) {
        if((sx&31) || (sy&31))return false;
        offset=((sy/32)*stride+sx/32)*4096;
    } else {
        if(sx&3)return false;
        offset=sy*pitch+sx*4;
    }
    return true;
}
struct SetupTransition {bool aperture {},replace {};};
// Exact donor isApertureMemoryRequired/setupScanoutMemory control flow.
// This predicts whether the original will free/allocate; it is not a backing
// or stop proof and never replaces the native operation's result.
inline SetupTransition setupTransition(bool apertureDisabled,bool accelerator,uint32_t serverState,bool surface) {
    const bool aperture=!apertureDisabled && (!accelerator || serverState==0 || serverState==2);
    return {aperture,aperture ? surface : !surface};
}
enum class Status : uint32_t {
    Unavailable, Complete, Association, UnsupportedPool, PoolRange,
    DisplayState, PTE, StateChanged, ApertureRange
};
struct Pool {
    uint32_t state {}, base {}, firstPage {}, bytes {}, pages {};
    uint64_t physical {}, descriptor {};
};
struct Readout {
    uint32_t version {1};
    Status status {Status::Unavailable};
    Pool pool;
    uint32_t surface {}, control {}, stride {}, size {}, verifiedPages {};
};
template<class T> inline T field(const void *object, size_t offset) {
    T v; memcpy(&v,static_cast<const uint8_t *>(object)+offset,sizeof(v));return v;
}
// Read-only hard-IRQ identity lookup. Callers retain FBs and publish each
// identity before its validated surface. No native map/GGTT traversal here.
// A duplicated association is ambiguous, never permission to use slot0.
template<class Pipe> unsigned boundPipe(const Pipe (&pipes)[3],const void *framebuffer) {
    unsigned found=UINT32_MAX;
    if(!framebuffer)return found;
    for(unsigned pipe=0;pipe<3;++pipe)if(__atomic_load_n(&pipes[pipe].memoryFramebuffer,__ATOMIC_ACQUIRE)==framebuffer) {
        if(found<3)return UINT32_MAX;
        found=pipe;
    }
    return found;
}
// Caller holds the owner lock. Existing identities remain retained; a path
// moving to a different pipe must be stopped/withdrawn before re-publication.
template<class Pipe> bool identityAvailable(const Pipe (&pipes)[3],const void *framebuffer,unsigned selected) {
    if(!framebuffer || selected>=3)return false;
    for(unsigned pipe=0;pipe<3;++pipe) {
        const auto fb=__atomic_load_n(&pipes[pipe].memoryFramebuffer,__ATOMIC_ACQUIRE);
        if((pipe==selected && fb && fb!=framebuffer) || (pipe!=selected && fb==framebuffer))return false;
    }
    return true;
}

template<class Pipe> bool publishedSurface(const Pipe (&pipes)[3],const void *framebuffer,uintptr_t surface) {
    if(!surface)return true;
    const auto pipe=boundPipe(pipes,framebuffer);
    return pipe<3 && surface==__atomic_load_n(&pipes[pipe].publishedScanout,__ATOMIC_ACQUIRE);
}
// The donor selects its base path list when joined index is -1. Reconnect
// rebuilds that list; the initial boot handoff used the joined-index-zero list.
// Keep the single-path restriction and never fall back to a stale other list.
inline size_t pathCountOffset(const void *framebuffer) {
    const auto index=field<int32_t>(framebuffer,0x3FD8);
    return index==-1 ? 0x49E8 : index==0 ? 0x4A00 : 0;
}
template<class T> inline T singlePath(const void *framebuffer) {
    const auto offset=pathCountOffset(framebuffer);
    return offset && field<uint32_t>(framebuffer,offset)==1 ? field<T>(framebuffer,offset+8) : T {};
}
// Class sizes are checked by the kernel caller, before interpreting a donor
// private object. Pool zero is the donor's pre-existing stolen allocation.
inline bool associations(const void *controller, size_t controllerBytes,
                         const void *framebuffer, size_t framebufferBytes, uintptr_t expectedOwner=0,
                         bool nativeSurface=false,uintptr_t logicalSurface=0) {
    if (!controller || !framebuffer || controllerBytes<0x1C20 || framebufferBytes<0x4B90 ||
        field<uintptr_t>(framebuffer,0x1D0)!=reinterpret_cast<uintptr_t>(controller) ||
        field<uintptr_t>(controller,0xC0)!=expectedOwner || field<uint8_t>(controller,0xE49) ||
        field<uintptr_t>(framebuffer,0x4340)!=logicalSurface || (logicalSurface && !expectedOwner) ||
        (field<uintptr_t>(framebuffer,0x4348) && (!nativeSurface || !expectedOwner))) return false;
    const auto index=field<uint32_t>(framebuffer,0x1DC),count=field<uint32_t>(controller,0xD5C);
    return count && count<=3 && index<count &&
        field<uintptr_t>(controller,0xD60+8*index)==reinterpret_cast<uintptr_t>(framebuffer);
}
// Exact donor FBMemMgr records are 40 bytes. Slot selection is independent
// of framebuffer/pipe identity; reading another slot does not authorize it.
inline Pool pool(const void *controller,unsigned slot=0) {
    if(!controller || slot>=3)return {};
    const auto base=0x1A10+40*slot;
    return {field<uint32_t>(controller,base),field<uint32_t>(controller,base+4),
        field<uint32_t>(controller,base+8),field<uint32_t>(controller,base+0x20),
        field<uint32_t>(controller,base+0x24),field<uint64_t>(controller,base+0x18),
        field<uint64_t>(controller,base+0x10)};
}
// Platform storage is uint64_t-aligned in XE; all donor offsets here are
// bytes. Validate the native copied value before changing either location.
inline bool setDisplayPool(void *controller,void *platform,size_t bytes,uint32_t pool) {
    if(!controller || !platform || bytes<0x1C0 || !pool || (pool&4095))return false;
    const auto native=field<uint32_t>(controller,0xDE8);
    if(native!=field<uint32_t>(platform,0x14) || pool>native)return false;
    memcpy(static_cast<uint8_t *>(platform)+0x14,&pool,sizeof(pool));
    memcpy(static_cast<uint8_t *>(controller)+0xDE8,&pool,sizeof(pool));
    return true;
}
// Caller validates class sizes/associations first. setupFBMemory picks a free
// bitmap slot and maintains its count; the FB index is not the chosen slot.
inline unsigned allocatedSlot(const void *controller,const void *framebuffer) {
    if(!controller || !framebuffer)return UINT32_MAX;
    const auto count=field<uint8_t>(controller,0xDF5),used=field<uint8_t>(controller,0xDF4);
    const auto slot=field<uint8_t>(framebuffer,0x4288);
    const auto mask=field<uint32_t>(controller,0xDF0);
    if(!count || count>3 || !used || used>count || slot>=count || (mask&~((1U<<count)-1)) || !(mask&(1U<<slot)))return UINT32_MAX;
    unsigned bits=0;for(unsigned i=0;i<count;++i)if(mask&(1U<<i))++bits;
    return bits==used ? slot : UINT32_MAX;
}
// Native free returns one bitmap slot, or no slot for a scaled surface.
// Validate the complete before/after bitmap counts, preserving peer claims.
inline bool bitmapValid(unsigned count,unsigned used,uint32_t mask) {
    if(!count || count>3 || used>count || (mask&~((1U<<count)-1)))return false;
    unsigned bits=0;for(unsigned i=0;i<count;++i)if(mask&(1U<<i))++bits;
    return bits==used;
}
inline bool slotReturned(unsigned count,unsigned slot,unsigned beforeUsed,uint32_t beforeMask,
                         unsigned afterUsed,uint32_t afterMask,unsigned afterSlot) {
    if(afterSlot!=0xFF || !bitmapValid(count,beforeUsed,beforeMask) || !bitmapValid(count,afterUsed,afterMask))return false;
    if(slot==0xFF)return beforeUsed==afterUsed && beforeMask==afterMask;
    return slot<count && beforeUsed && (beforeMask&(1U<<slot)) && afterUsed==beforeUsed-1 &&
        afterMask==(beforeMask&~(1U<<slot));
}
// IO exposes only class-size-qualified native FB array entries. Validate the
// complete array: a duplicate slot claim or foreign provider is ambiguous.
template<class IO> uintptr_t slotClaim(IO &io,const void *controller,unsigned slot) {
    if(!controller || !slot || slot>=3)return 0;
    const auto count=field<uint32_t>(controller,0xD5C);
    if(!count || count>3)return 0;
    uintptr_t found=0;
    for(unsigned index=0;index<count;++index) {
        const auto fb=io.framebuffer(index);
        if(!fb || field<uint32_t>(fb,0x1DC)!=index ||
            field<uintptr_t>(fb,0x1D0)!=reinterpret_cast<uintptr_t>(controller))return 0;
        if(allocatedSlot(controller,fb)==slot) {
            if(found)return 0;
            found=reinterpret_cast<uintptr_t>(fb);
        }
    }
    return found;
}

inline bool same(const Pool &a,const Pool &b) {
    return a.state==b.state && a.base==b.base && a.firstPage==b.firstPage &&
        a.bytes==b.bytes && a.pages==b.pages && a.physical==b.physical && a.descriptor==b.descriptor;
}
// FreeSysMem changes only state/descriptor, never the fixed window geometry.
inline bool poolReturned(const Pool &before,const Pool &after,unsigned slot) {
    if(!slot)return before.state==1 && !before.descriptor && same(before,after);
    if(slot>=3 || before.state!=3 || !before.descriptor || after.state || after.descriptor)return false;
    auto expected=before;expected.state=0;expected.descriptor=0;return same(expected,after);
}
// Exact donor FBMemMgr_Init (42E2E..42E58, 43028..43074) describes fixed
// SysMem GGTT windows after the stolen/cursor/DSB/guard prefix. Those windows
// are not included in RegisterAccelerator's original DF8 reservation. Compute
// their extent from the native records to audit allocator ownership;
// a valid layout is not permission to map pages or claim they are reserved.
struct PoolLayout {
    bool valid {};
    uint32_t count {},nativePrefix {},fixedPoolEnd {};
    Pool slots[3] {};
};
inline PoolLayout poolLayout(const void *controller,size_t controllerBytes,uint64_t gpuLimit) {
    PoolLayout r;
    if(!controller || controllerBytes<0x1C20)return r;
    r.count=field<uint8_t>(controller,0xDF5);
    r.nativePrefix=field<uint32_t>(controller,0xDF8);
    const uint64_t stolen=field<uint32_t>(controller,0xDE4);
    const uint64_t cursor=field<uint32_t>(controller,0xDC8);
    const uint64_t dsb=field<uint32_t>(controller,0xDCC);
    const uint64_t auxiliary=field<uint64_t>(controller,0xDD0);
    const uint64_t guard=field<uint32_t>(controller,0xDD8);
    const uint32_t bytes=field<uint32_t>(controller,0xDE8);
    const auto framebuffers=field<uint32_t>(controller,0xD5C);
    if(!r.count || r.count>3 || !framebuffers || framebuffers>3 ||
        !bytes || (bytes&4095) || !stolen || (stolen&4095) ||
        (cursor&4095) || (dsb&4095) || auxiliary!=stolen+cursor+dsb ||
        guard!=uint64_t(framebuffers)*4096 || auxiliary+guard!=r.nativePrefix ||
        (r.nativePrefix&4095) || r.nativePrefix>gpuLimit)return r;
    uint64_t end=r.nativePrefix;
    for(unsigned slot=0;slot<r.count;++slot) {
        const auto p=pool(controller,slot);r.slots[slot]=p;
        const uint64_t base=slot ? uint64_t(r.nativePrefix)+uint64_t(slot-1)*bytes : 0;
        if(base>UINT32_MAX || p.base!=base || p.firstPage!=base/4096 ||
            p.bytes!=bytes || p.pages!=bytes/4096)return r;
        if(!slot) {
            if(p.state!=1 || p.descriptor || !p.physical || (p.physical&4095) || bytes>stolen)return r;
        } else {
            // 0 is unallocated; 2/3 retain a native descriptor. Slot selection
            // and FB/pipe/DDI ownership remain separate checks at the caller.
            if(p.physical || (p.state!=0 && p.state!=2 && p.state!=3) ||
                (p.state==0 ? p.descriptor!=0 : p.descriptor==0))return r;
            end=base+bytes;
            if(end>gpuLimit || end>UINT32_MAX)return r;
        }
    }
    r.fixedPoolEnd=static_cast<uint32_t>(end);r.valid=true;return r;
}
// Native RegisterAccelerator returns a three-word IntelSharedMemInfo. In
// attached mode, IGMemoryManager::initSegments uses its first word for BOTH
// the protected GGTT prefix and the start of every task's aperture allocator.
// This declares address ownership; it says nothing about SysMem PTE backing.
struct SharedReservation {
    bool valid {};
    uint32_t nativePrefix {},reservedPrefix {};
    uint64_t apertureBytes {},freeApertureBytes {};
};
inline SharedReservation sharedReservation(const PoolLayout &layout,uint64_t apertureBytes,bool fixedPools) {
    SharedReservation r;r.nativePrefix=layout.nativePrefix;r.apertureBytes=apertureBytes;
    if(!layout.valid || !layout.count || layout.count>3)return r;
    // Without fixed backing support, no descriptor may already occupy the
    // ranges that will be returned to the native task allocator.
    if(!fixedPools)for(unsigned slot=1;slot<layout.count;++slot)
        if(layout.slots[slot].state || layout.slots[slot].descriptor)return r;
    r.reservedPrefix=fixedPools ? layout.fixedPoolEnd : layout.nativePrefix;
    if(!r.reservedPrefix || (r.reservedPrefix&4095) || r.reservedPrefix<r.nativePrefix ||
        r.reservedPrefix>=apertureBytes)return r;
    r.freeApertureBytes=apertureBytes-r.reservedPrefix;r.valid=true;return r;
}
inline bool publishSharedReservation(const SharedReservation &r,void *info,size_t bytes,bool nativeSuccess) {
    if(!r.valid || !nativeSuccess || !info || bytes<12 || field<uint32_t>(info,0)!=r.nativePrefix)return false;
    memcpy(info,&r.reservedPrefix,sizeof(r.reservedPrefix));return true;
}
inline bool sharedReservationAccepted(uint32_t prefix,uint64_t apertureBytes,uint64_t managerPrefix,const uint64_t (&ranges)[4]) {
    return prefix && !(prefix&4095) && prefix<apertureBytes && managerPrefix==prefix && ranges[0]==0 &&
        ranges[1]==prefix && ranges[2]==prefix && ranges[3]==apertureBytes-prefix;
}
inline bool sharedReservationAccepted(const SharedReservation &r,uint64_t managerPrefix,const uint64_t (&ranges)[4]) {
    return r.valid && sharedReservationAccepted(r.reservedPrefix,r.apertureBytes,managerPrefix,ranges);
}
// The aperture's physical address is the PCI BAR, not the backing RAM address.
// A valid subrange alone does not prove the GGTT's mapping; inspect() does that.
// bootScanout=false verifies backing/ownership for a cold allocation without
// reading any pipe bank or treating the allocation as an active display.
inline bool apertureRange(const Pool &p,uint32_t base,uint32_t bytes,uint64_t apertureBytes) {
    return bytes && !(base&4095) && !(bytes&4095) && base>=p.base &&
        uint64_t(base)+bytes<=uint64_t(p.base)+p.bytes && uint64_t(base)+bytes<=apertureBytes;
}
// Native IntelScaledModeData is a 64-byte CPU allocation. Its map/memory
// references are owned by the display pipe until native deallocateScanoutMemory.
// This readout neither maps pages nor extends their lifetime. The caller holds
// the native FB mutex and validates the real map/memory classes and owner.
enum class SurfaceStatus : uint64_t {Unavailable,Complete,Association,Shape,Range,PTE,StateChanged};
struct SurfaceReadout {
    uint64_t version {1};SurfaceStatus status {SurfaceStatus::Unavailable};
    uint64_t record {},map {},memory {},gpu {},bytes {},pitch {},refs {},kind {},flags {};
    uint64_t verifiedPages {},firstBadGPU {},expectedPhysical {},actualPTE {},reserve {};
};
// Caller pins the record and native memory/map through the PTE walk. A pending
// record need not be published into FB+4348; its existing pointer is checked
// separately so publication during the inspection is detected.
template<class IO> SurfaceReadout inspectSurfaceRecord(IO &io,const void *controller,size_t controllerBytes,
                    const void *fb,size_t fbBytes,uintptr_t owner,uint64_t reserve,uint64_t gpuLimit,
                    const XELPMemory::Layout &memory,const void *surface,const void *attached,uintptr_t logicalSurface=0) {
    SurfaceReadout r;r.reserve=reserve;
    if(!owner || !associations(controller,controllerBytes,fb,fbBytes,owner,true,logicalSurface)) {
        r.status=SurfaceStatus::Association;return r;
    }
    if(field<const void *>(fb,0x4348)!=attached) {r.status=SurfaceStatus::Association;return r;}
    if(!surface)return r;
    uint8_t snapshot[64];memcpy(snapshot,surface,sizeof(snapshot));
    r.record=reinterpret_cast<uintptr_t>(surface);r.map=field<uintptr_t>(snapshot,0x10);
    r.memory=field<uintptr_t>(snapshot,0x18);r.gpu=field<uint64_t>(snapshot,0x20);
    r.pitch=field<uint32_t>(snapshot,0x2C);r.refs=field<uint32_t>(snapshot,0x38);
    r.kind=field<uint32_t>(snapshot,0x3C);r.flags=field<uint32_t>(snapshot,0xC);
    const auto columns=field<uint32_t>(snapshot,4),rows=field<uint32_t>(snapshot,8);
    if(!columns || !rows || columns>8192 || rows>8192) {r.status=SurfaceStatus::Shape;return r;}
    r.bytes=field<uint32_t>(snapshot,0x28);
    if(!r.bytes)r.bytes=uint64_t(columns)*rows*4096;
    if(!r.map || !r.memory || !columns || !rows || !r.refs || (r.kind!=1 && r.kind!=2) ||
       (r.flags&1) || !io.memoryReady(r.map,r.memory,owner,r.bytes)) {
        r.status=SurfaceStatus::Shape;return r;
    }
    // Keep the entire native FB/GOP/auxiliary prefix. Scanout is allocated by
    // the native global manager beyond that reservation. GPU scanout uses the full GGTT; the CPU
    // aperture BAR covers only a window and does not bound display addresses.
    if(!memory.valid || reserve<memory.bytes || (reserve&4095) || (r.gpu&4095) || !r.bytes ||
       (r.bytes&4095) || r.gpu<reserve || r.gpu>=gpuLimit ||
       r.bytes>gpuLimit-r.gpu || (r.pitch!=uint64_t(columns)*128 &&
       r.pitch!=uint64_t(columns)*512)) {r.status=SurfaceStatus::Range;return r;}
    for(uint64_t off=0;off<r.bytes;off+=4096) {
        uint64_t span=0;const auto physical=io.physical(r.memory,off,span);
        const auto pte=io.readPTE(uint32_t(0x800000+((r.gpu+off)>>9)));
        // PAT attributes are native allocator policy. Gen12 bit1 selects local
        // memory and must be clear for this system-memory allocation.
        if(!physical || (physical&4095) || physical>0x7FFFFFF000ULL || span<4096 ||
           !(pte&1) || (pte&2) || (pte&0x7FFFFFF000ULL)!=physical) {
            r.firstBadGPU=r.gpu+off;r.expectedPhysical=physical;r.actualPTE=pte;
            r.status=SurfaceStatus::PTE;return r;
        }
        ++r.verifiedPages;
    }
    if(!associations(controller,controllerBytes,fb,fbBytes,owner,true,logicalSurface) ||
       field<const void *>(fb,0x4348)!=attached || memcmp(snapshot,surface,sizeof(snapshot)) || !io.memoryReady(r.map,r.memory,owner,r.bytes)) {
        r.status=SurfaceStatus::StateChanged;return r;
    }
    r.status=SurfaceStatus::Complete;return r;
}
template<class IO> SurfaceReadout inspectSurface(IO &io,const void *controller,size_t controllerBytes,
                    const void *fb,size_t fbBytes,uintptr_t owner,uint64_t reserve,uint64_t gpuLimit,const XELPMemory::Layout &memory,
                    uintptr_t logicalSurface=0) {
    const auto surface=fb && fbBytes>=0x4B90 ? field<const void *>(fb,0x4348) : nullptr;
    return inspectSurfaceRecord(io,controller,controllerBytes,fb,fbBytes,owner,reserve,gpuLimit,memory,surface,surface,logicalSurface);
}
// First DSI handoff: the real FB still owns stolen slot zero while the GPU
// allocator owns a pending record beyond the reserved prefix. This proves
// backing, not display-pipe ownership, serialization, or permission to arm.
template<class IO> SurfaceReadout inspectPendingSurface(IO &io,const void *controller,size_t controllerBytes,
                    const void *fb,size_t fbBytes,uintptr_t owner,uint64_t reserve,uint64_t gpuLimit,
                    const XELPMemory::Layout &memory,const void *pending) {
    SurfaceReadout failure;failure.status=SurfaceStatus::Association;
    if(!pending || !owner || !associations(controller,controllerBytes,fb,fbBytes,owner) ||
       allocatedSlot(controller,fb)!=0)return failure;
    const auto before=pool(controller);
    const auto mask=field<uint32_t>(controller,0xDF0);
    const auto used=field<uint8_t>(controller,0xDF4),count=field<uint8_t>(controller,0xDF5);
    if(before.state!=1 || before.descriptor || before.base || !before.bytes || (before.bytes&4095) ||
       before.firstPage || before.pages!=before.bytes/4096 || !memory.valid ||
       !memory.range(before.base,before.bytes,true) || before.physical!=memory.base+before.base ||
       uint64_t(before.base)+before.bytes>reserve)return failure;
    auto r=inspectSurfaceRecord(io,controller,controllerBytes,fb,fbBytes,owner,reserve,gpuLimit,memory,pending,nullptr);
    if(r.status==SurfaceStatus::Complete && (allocatedSlot(controller,fb)!=0 ||
       field<uint32_t>(controller,0xDF0)!=mask || field<uint8_t>(controller,0xDF4)!=used ||
       field<uint8_t>(controller,0xDF5)!=count || !same(before,pool(controller))))r.status=SurfaceStatus::StateChanged;
    return r;
}
template<class IO> Readout inspect(IO &io,const void *controller,size_t controllerBytes,
                                  const void *framebuffer,size_t framebufferBytes,
                                  uint64_t apertureBytes,const XELPMemory::Layout &memory,bool bootScanout=true,uintptr_t expectedOwner=0,
                                  unsigned expectedDDI=1,const Display13TC::Boot *tc=nullptr,
                                  const AlderLakeN::DisplayReadout *dp=nullptr,bool panel=false,
                                  const XELP::DSI::Readout *dsi=nullptr) {
    Readout r;
    if (!associations(controller,controllerBytes,framebuffer,framebufferBytes,expectedOwner)) {
        r.status=Status::Association;return r;
    }
    // The first DSI GOP handoff is pipe A/PHY A. A validated description is
    // not backing ownership: revalidate the leased source on both sides of
    // the PTE walk below. Never combine it with DP/AUX/TC source semantics.
    if(dsi && (!bootScanout || expectedDDI || dp || tc || panel || dsi->identity.pipe!=0 ||
       !XELP::DSI::sourceUnchanged(io,*dsi))) {r.status=Status::DisplayState;return r;}
    if(bootScanout && (expectedDDI>=7 || (expectedDDI>=3 &&
       (!tc || !tc->valid() || tc->port.ddi!=expectedDDI || tc->pipe!=0 ||
        !(tc->buffer&64) || (!dp && tc->ownershipOptional) || !tc->unchanged(io))))) {
        r.status=Status::DisplayState;return r;
    }
    if(bootScanout && dp && (dp->activePipes!=1 ||
       !AlderLakeN::dpSourceUnchanged(io,*dp,0,expectedDDI,0,tc,panel))) {
        r.status=Status::DisplayState;return r;
    }
    r.pool=pool(controller);const auto &p=r.pool;
    if (field<uint8_t>(framebuffer,0x4288)!=0 || !field<uint8_t>(controller,0xDF5) ||
        field<uint8_t>(controller,0xDF5)>3 || !field<uint8_t>(controller,0xDF4) ||
        field<uint8_t>(controller,0xDF4)>field<uint8_t>(controller,0xDF5) ||
        !(field<uint32_t>(controller,0xDF0)&1) || p.state!=1 || p.descriptor) {
        r.status=Status::UnsupportedPool;return r;
    }
    // Compare native slot metadata and PTEs with the actual PCI memory layout.
    if (!p.bytes || (p.bytes&4095) || !memory.valid || (p.base&4095) ||
        p.firstPage!=p.base/4096 || p.pages!=p.bytes/4096 ||
        p.physical!=memory.base+p.base || !memory.range(p.base,p.bytes,true) ||
        uint64_t(p.base)+p.bytes>apertureBytes ||
        !io.readable(0x800000+8*(p.firstPage+p.pages)-1)) {
        r.status=Status::PoolRange;return r;
    }
    // Read only the powered boot pipe. Linear RGB uses byte GGTT offsets.
    // Linux intel_fb_modifier_uses_dpt excludes LINEAR even when the global
    // DPT-disable bit is clear, as in the DSI GOP. Keep the established ordinary
    // output contract; do not toggle the global bit to inspect DSI backing.
    constexpr uint32_t regs[]={0x45404,0x42084,0x60400,0x70008,0x70180,0x70188,
        0x7018C,0x70190,0x701A4,0x7019C,0x701AC};
    if(bootScanout)for(auto reg:regs)if(!io.readable(reg))return r;
    if(!io.readable(0x45404))return r;
    const auto wells=io.read(0x45404);
    if(wells==UINT32_MAX || (wells&(bootScanout ? 0x403U : 3U))!=(bootScanout ? 0x403U : 3U)) {r.status=Status::DisplayState;return r;}
    uint32_t dpt=0,trans=0,conf=0,position=0,offset=0;
    const uint32_t transReg=dsi?0x6B400U:0x60400U,confReg=dsi?0x7B008U:0x70008U;
    if(bootScanout) {
        dpt=io.read(0x42084);trans=io.read(transReg);conf=io.read(confReg);
        position=io.read(0x7018C);offset=io.read(0x701A4);
        r.surface=io.read(0x7019C);r.control=io.read(0x70180);r.stride=io.read(0x70188);r.size=io.read(0x70190);
        const auto geometry=XELPGeometry::linear(r.control,r.stride,r.size,position,offset);
        const bool source=dsi ? !(io.read(0x60400)&(1U<<31)) && XELPGeometry::timing(geometry,dsi->htotal,dsi->vtotal) :
            (dpt&(1U<<30)) && ((trans>>27)&15)==expectedDDI+1 &&
            (dp ? ((trans>>24)&7)==2 : ((trans>>24)&7)<=1) && !((trans>>20)&7);
        if (dpt==UINT32_MAX || !source || !(trans&(1U<<31)) || (conf&0xC0000000)!=0xC0000000 ||
            !geometry.valid ||
            position || offset || (r.surface&4095) ||
            r.surface!=io.read(0x701AC) || r.surface<p.base ||
            uint64_t(r.surface)+geometry.bytes>uint64_t(p.base)+p.bytes) {
            r.status=Status::DisplayState;return r;
        }
    }
    for(uint32_t i=0;i<p.pages;++i) {
        // Preserve firmware PTE flags too: Gen12 bit1 is local memory. Neither
        // scratch entries nor an unrelated valid mapping authorize this pool.
        if(io.readPTE(0x800000+8*(p.firstPage+i))!=((p.physical+uint64_t(i)*4096)|1)) {
            r.status=Status::PTE;return r;
        }
        ++r.verifiedPages;
    }
    // Check power first again: do not read a pipe bank after its well was
    // withdrawn while validating the GGTT. Then reject all layout changes,
    // including DPT/position changes with an unchanged surface address.
    const auto finalWells=io.read(0x45404);
    if(finalWells==UINT32_MAX || (finalWells&(bootScanout ? 0x403U : 3U))!=(bootScanout ? 0x403U : 3U) || !same(p,pool(controller)) ||
        !associations(controller,controllerBytes,framebuffer,framebufferBytes,expectedOwner) ||
        field<uint8_t>(framebuffer,0x4288)!=0 || !(field<uint32_t>(controller,0xDF0)&1) ||
        (bootScanout && (io.read(0x42084)!=dpt || io.read(transReg)!=trans || io.read(confReg)!=conf ||
        io.read(0x7018C)!=position || io.read(0x701A4)!=offset ||
        io.read(0x7019C)!=r.surface || io.read(0x701AC)!=r.surface ||
        io.read(0x70180)!=r.control || io.read(0x70188)!=r.stride || io.read(0x70190)!=r.size ||
        (expectedDDI>=3 && !tc->unchanged(io)) ||
        (dp && !AlderLakeN::dpSourceUnchanged(io,*dp,0,expectedDDI,0,tc,panel)) ||
        (dsi && (!XELP::DSI::sourceUnchanged(io,*dsi) || (io.read(0x60400)&(1U<<31))))))) {
        r.status=Status::StateChanged;return r;
    }
    r.status=Status::Complete;return r;
}
}
#endif
