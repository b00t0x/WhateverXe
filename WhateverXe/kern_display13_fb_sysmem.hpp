// Display13 fixed SysMem windows. The kernel caller supplies a wired descriptor,
// page workspace, native slot claim, stopped consumers and REAL allocator-owner
// checks through IO. This helper never creates an ownership claim or publishes
// a native pool descriptor/state. All pages are qualified before the first PTE.
#ifndef KERN_DISPLAY13_FB_SYSMEM_HPP
#define KERN_DISPLAY13_FB_SYSMEM_HPP
#include "kern_display13_fb_gtt.hpp"
#include "kern_display13_fb_memory.hpp"
namespace Display13FBSysMem {
enum class Status : uint32_t {Unavailable,Complete,Layout,Ownership,Physical,Occupied,Readback,BackingChanged};
struct Result {
    Status status {Status::Unavailable};
    uint32_t checked {},written {},verified {},firstBadPage {UINT32_MAX};
    uint64_t expected {},observed {};
    bool mustRetainBacking {};
};
inline bool scratch(uint64_t pte) {
    return pte && (pte&~Display13FBGTT::AddressMask)==1 &&
        Display13FBGTT::physicalRange(pte&Display13FBGTT::AddressMask,4096,4096);
}
inline bool window(const Display13FBMemory::PoolLayout &layout,unsigned slot) {
    if(!layout.valid || !slot || slot>=layout.count || slot>=3)return false;
    const auto &p=layout.slots[slot];const uint64_t bytes=layout.slots[0].bytes;
    const uint64_t base=uint64_t(layout.nativePrefix)+uint64_t(slot-1)*bytes;
    return bytes && !(bytes&4095) && p.base==base && p.bytes==bytes &&
        p.firstPage==base/4096 && p.pages==bytes/4096 && !p.physical &&
        base>=layout.nativePrefix && base+bytes<=layout.fixedPoolEnd &&
        layout.fixedPoolEnd<=Display13FBGTT::Aperture;
}
inline uint32_t reg(const Display13FBMemory::Pool &p,unsigned page) {
    return static_cast<uint32_t>(0x800000+((uint64_t(p.base)+uint64_t(page)*4096)>>9));
}
template<class IO> Result install(IO &io,const Display13FBMemory::PoolLayout &layout,unsigned slot,
                                 uint64_t *pages,uint32_t capacity,uint64_t scratchPTE) {
    Result r;
    if(!window(layout,slot) || !pages || capacity<layout.slots[slot].pages || !scratch(scratchPTE)) {
        r.status=Status::Layout;return r;
    }
    const auto &p=layout.slots[slot];
    if(p.state || p.descriptor) {r.status=Status::Layout;return r;}
    if(!io.ownsWindow(slot,p) || !io.consumersStopped(slot,p)) {r.status=Status::Ownership;return r;}
    // An unallocated window has no defined PTE contents: firmware may leave
    // arbitrary bits here. Ownership comes from the reserved allocator range
    // and the empty native slot, not from zero/scratch entries. Preflight all
    // physical pages, then initialize only that owned, stopped window.
    for(unsigned i=0;i<p.pages;++i) {
        uint64_t span=0;const auto physical=io.physical(uint64_t(i)*4096,span);
        r.firstBadPage=i;
        if(!Display13FBGTT::physicalRange(physical,span,4096)) {r.status=Status::Physical;return r;}
        pages[i]=physical;++r.checked;
    }
    for(unsigned i=0;i<p.pages;++i) {
        r.firstBadPage=i;
        if(!io.ownsWindow(slot,p) || !io.consumersStopped(slot,p)) {r.status=Status::Ownership;return r;}
        uint64_t span=0;const auto physical=io.physical(uint64_t(i)*4096,span);
        if(physical!=pages[i] || span<4096) {r.status=Status::BackingChanged;return r;}
        const auto address=reg(p,i);
        r.expected=Display13FBGTT::pte(pages[i],0);
        // A possibly delivered write requires backing retention even when
        // the posting read fails. The caller owns that reference through error.
        r.mustRetainBacking=true;io.writePTE(address,r.expected);++r.written;
        r.observed=io.readPTE(address);
        if(r.observed!=r.expected) {r.status=Status::Readback;return r;}
        ++r.verified;
    }
    if(!io.ownsWindow(slot,p) || !io.consumersStopped(slot,p)) {r.status=Status::Ownership;return r;}
    r.status=Status::Complete;r.firstBadPage=UINT32_MAX;return r;
}
// Read-only backing qualification for an already published pool. Display
// consumers may be active; no writes, stops, frees or success publication.
// The caller stabilizes descriptor/page-list lifetime and the native claim.
template<class IO> Result inspect(IO &io,const Display13FBMemory::PoolLayout &layout,unsigned slot,
                                 const uint64_t *pages,uint32_t count) {
    Result r;r.mustRetainBacking=true;
    if(!window(layout,slot) || !pages || count!=layout.slots[slot].pages) {r.status=Status::Layout;return r;}
    const auto &p=layout.slots[slot];
    if(p.state!=3 || !p.descriptor) {r.status=Status::Layout;return r;}
    if(!io.ownsWindow(slot,p)) {r.status=Status::Ownership;return r;}
    for(unsigned i=0;i<p.pages;++i) {
        r.firstBadPage=i;
        if(!Display13FBGTT::physicalRange(pages[i],4096,4096)) {r.status=Status::Physical;return r;}
        uint64_t span=0;const auto physical=io.physical(uint64_t(i)*4096,span);
        if(physical!=pages[i] || span<4096) {r.status=Status::BackingChanged;return r;}
        r.expected=Display13FBGTT::pte(pages[i],0);r.observed=io.readPTE(reg(p,i));++r.checked;
        if(r.expected!=r.observed) {r.status=Status::Readback;return r;}
        ++r.verified;
    }
    if(!io.ownsWindow(slot,p)) {r.status=Status::Ownership;return r;}
    r.status=Status::Complete;r.firstBadPage=UINT32_MAX;return r;
}

template<class IO> Result remove(IO &io,const Display13FBMemory::PoolLayout &layout,unsigned slot,
                                const uint64_t *pages,uint32_t count,uint64_t scratchPTE) {
    Result r;r.mustRetainBacking=true; // Existing mappings already expose RAM.
    if(!window(layout,slot) || !pages || count!=layout.slots[slot].pages || !scratch(scratchPTE)) {
        r.status=Status::Layout;return r;
    }
    const auto &p=layout.slots[slot];
    if(p.state!=3 || !p.descriptor) {r.status=Status::Layout;return r;}
    if(!io.ownsWindow(slot,p) || !io.consumersStopped(slot,p)) {r.status=Status::Ownership;return r;}
    for(unsigned i=0;i<p.pages;++i) {
        r.firstBadPage=i;r.expected=Display13FBGTT::pte(pages[i],0);
        if(!Display13FBGTT::physicalRange(pages[i],4096,4096)) {r.status=Status::Physical;return r;}
        uint64_t span=0;const auto physical=io.physical(uint64_t(i)*4096,span);
        if(physical!=pages[i] || span<4096) {r.status=Status::BackingChanged;return r;}
        r.observed=io.readPTE(reg(p,i));
        if(r.observed!=r.expected) {r.status=Status::Occupied;return r;}
        ++r.checked;
    }
    for(unsigned i=0;i<p.pages;++i) {
        r.firstBadPage=i;
        if(!io.ownsWindow(slot,p) || !io.consumersStopped(slot,p)) {r.status=Status::Ownership;return r;}
        const auto address=reg(p,i);r.expected=Display13FBGTT::pte(pages[i],0);r.observed=io.readPTE(address);
        if(r.observed!=r.expected) {r.status=Status::Occupied;return r;}
        r.expected=scratchPTE;
        io.writePTE(address,scratchPTE);++r.written;r.observed=io.readPTE(address);
        if(r.observed!=scratchPTE) {r.status=Status::Readback;return r;}
        ++r.verified;
    }
    if(!io.ownsWindow(slot,p) || !io.consumersStopped(slot,p)) {r.status=Status::Ownership;return r;}
    r.status=Status::Complete;r.firstBadPage=UINT32_MAX;r.mustRetainBacking=false;return r;
}
}
#endif
