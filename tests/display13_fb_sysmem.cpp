#include "kern_display13_fb_sysmem.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace Display13FBSysMem;
constexpr uint64_t Scratch=0x200000001ULL;
struct IO {
    Display13FBMemory::PoolLayout layout;
    std::vector<uint64_t> ptes,physicalPages,workspace;
    unsigned slot,reads {},writes {},physicalCalls {},loseOwnerReadAfter {UINT32_MAX};
    bool owned {true},active {};
    uint64_t ownedEnd {0xE195000};
    unsigned badPhysical {UINT32_MAX},shortPhysical {UINT32_MAX},rejectWrite {UINT32_MAX},loseOwnerAfter {UINT32_MAX};
    unsigned changePhysicalAfter {UINT32_MAX},foreignAfter {UINT32_MAX},startConsumerAfter {UINT32_MAX};
    IO(unsigned selected=1) : slot(selected),ptes(0x4000),physicalPages(0x4000),workspace(0x4000) {
        layout.valid=true;layout.count=3;layout.nativePrefix=0x6195000;layout.fixedPoolEnd=0xE195000;
        layout.slots[0]={1,0,0,0x4000000,0x4000,0x7A800000,0};
        for(unsigned s=1;s<3;++s) {
            const uint32_t base=0x6195000+(s-1)*0x4000000;
            layout.slots[s]={0,base,base/4096,0x4000000,0x4000,0,0};
        }
        // Scatter pages deliberately, rather than assuming contiguous RAM.
        for(unsigned i=0;i<physicalPages.size();++i)physicalPages[i]=0x100000000ULL+uint64_t(i)*8192;
    }
    unsigned index(uint32_t address) {
        const auto start=reg(layout.slots[slot],0);assert(address>=start && !((address-start)&7));
        const auto i=(address-start)/8;assert(i<ptes.size());return i;
    }
    uint64_t readPTE(uint32_t address) {++reads;return ptes[index(address)];}
    void writePTE(uint32_t address,uint64_t value) {
        const auto i=index(address);++writes;if(writes!=rejectWrite)ptes[i]=value;
        if(writes==foreignAfter)ptes[3]=0x300000001ULL;
    }
    uint64_t physical(uint64_t offset,uint64_t &span) {
        const auto i=offset/4096;assert(i<physicalPages.size());++physicalCalls;
        span=i==shortPhysical ? 2048 : 4096;
        return i==badPhysical ? 0 : physicalPages[i]+(physicalCalls>changePhysicalAfter ? 0x10000000 : 0);
    }
    bool ownsWindow(unsigned selected,const Display13FBMemory::Pool &p) {
        return owned && selected==slot && Display13FBMemory::same(p,layout.slots[slot]) &&
            uint64_t(p.base)+p.bytes<=ownedEnd && writes<loseOwnerAfter && reads<loseOwnerReadAfter;
    }
    bool consumersStopped(unsigned,const Display13FBMemory::Pool &) {return !active && writes<startConsumerAfter;}
    Result map() {return install(*this,layout,slot,workspace.data(),workspace.size(),Scratch);}
    void publish() {layout.slots[slot].state=3;layout.slots[slot].descriptor=0x123400;}
    Result unmap() {return remove(*this,layout,slot,workspace.data(),workspace.size(),Scratch);}
};
int main() {
    for(unsigned slot=1;slot<3;++slot) {
        IO io(slot);const auto a=io.map();assert(a.status==Status::Complete && a.checked==0x4000 &&
            a.written==0x4000 && a.verified==0x4000 && a.mustRetainBacking);
        for(unsigned i=0;i<io.ptes.size();++i)assert(io.ptes[i]==(io.physicalPages[i]|1) && !(io.ptes[i]&2));
        io.publish();io.writes=0;const auto b=io.unmap();assert(b.status==Status::Complete && !b.mustRetainBacking && b.verified==0x4000);
        assert(std::all_of(io.ptes.begin(),io.ptes.end(),[](uint64_t p){return p==Scratch;}));
    }
    {IO io;io.badPhysical=0x3FFF;const auto r=io.map();assert(r.status==Status::Physical && !r.written && !r.mustRetainBacking);}
    {IO io;io.shortPhysical=0x3FFF;assert(io.map().status==Status::Physical && !io.writes);}
    // Empty native slots can contain undefined firmware GGTT words. Include
    // the observed nonpresent value and present-looking residual entries.
    {IO io;for(unsigned i=0;i<io.ptes.size();++i)io.ptes[i]=0xD7DD8B56CBB99446ULL+(i&1);
     assert(io.map().status==Status::Complete);
     for(unsigned i=0;i<io.ptes.size();++i)assert(io.ptes[i]==(io.physicalPages[i]|1));}
    {IO io;io.ptes.back()=0x300000001ULL;io.owned=false;const auto before=io.ptes;
     assert(io.map().status==Status::Ownership && !io.writes && io.ptes==before);}
    {IO io;io.publish();const auto before=io.ptes;
     assert(io.map().status==Status::Layout && !io.writes && io.ptes==before);}
    {IO io;io.ownedEnd=0x6195000;assert(io.map().status==Status::Ownership && !io.reads && !io.writes);}
    {IO io;io.active=true;const auto r=io.map();assert(r.status==Status::Ownership && !r.written && !r.mustRetainBacking);}
    {IO io;io.startConsumerAfter=3;const auto r=io.map();assert(r.status==Status::Ownership && r.written==3 && r.mustRetainBacking);}
    {IO io;io.rejectWrite=7;const auto r=io.map();assert(r.status==Status::Readback && r.written==7 && r.mustRetainBacking);}
    {IO io;io.loseOwnerAfter=3;const auto r=io.map();assert(r.status==Status::Ownership && r.written==3 && r.mustRetainBacking);}
    {IO io;io.changePhysicalAfter=0x4000+3;const auto r=io.map();assert(r.status==Status::BackingChanged && r.written==3 && r.mustRetainBacking);}
    {IO io;const auto r=install(io,io.layout,0,io.workspace.data(),io.workspace.size(),Scratch);
     assert(r.status==Status::Layout && !io.reads && !io.writes);}
    {IO io;const auto r=install(io,io.layout,1,io.workspace.data(),1,Scratch);
     assert(r.status==Status::Layout && !io.reads && !io.writes);}
    {IO io;const auto r=install(io,io.layout,1,io.workspace.data(),io.workspace.size(),Scratch|2);
     assert(r.status==Status::Layout && !io.reads && !io.writes);}
    {IO io;assert(io.map().status==Status::Complete);io.publish();io.writes=0;io.active=true;
     const auto r=io.unmap();assert(r.status==Status::Ownership && !r.written && r.mustRetainBacking);}
    {IO io;assert(io.map().status==Status::Complete);io.publish();io.writes=0;io.ptes.back()=Scratch;
     const auto r=io.unmap();assert(r.status==Status::Occupied && !r.written && r.mustRetainBacking);}
    {IO io;assert(io.map().status==Status::Complete);io.publish();io.writes=0;io.rejectWrite=7;
     const auto r=io.unmap();assert(r.status==Status::Readback && r.written==7 && r.mustRetainBacking);}
    {IO io;assert(io.map().status==Status::Complete);io.publish();io.writes=0;io.foreignAfter=2;
     const auto r=io.unmap();assert(r.status==Status::Occupied && r.written==3 && r.mustRetainBacking);}
    for(unsigned slot=1;slot<3;++slot) {
        IO io(slot);assert(io.map().status==Status::Complete);io.publish();io.writes=0;io.active=true;
        auto verify=[&]() {return inspect(io,io.layout,slot,io.workspace.data(),io.workspace.size());};
        auto r=verify();assert(r.status==Status::Complete && r.verified==0x4000 && !r.written && !io.writes && r.mustRetainBacking);
        io.ptes.back()=Scratch;r=verify();assert(r.status==Status::Readback && r.firstBadPage==0x3FFF && !io.writes);
        io.ptes.back()=io.physicalPages.back()|1;io.changePhysicalAfter=0;
        assert(verify().status==Status::BackingChanged && !io.writes);io.changePhysicalAfter=UINT32_MAX;
        io.owned=false;io.reads=0;assert(verify().status==Status::Ownership && !io.reads && !io.writes);
    }
    {IO io;assert(io.map().status==Status::Complete);io.publish();io.writes=0;io.reads=0;io.loseOwnerReadAfter=0x4000;
     const auto r=inspect(io,io.layout,1,io.workspace.data(),io.workspace.size());
     assert(r.status==Status::Ownership && r.verified==0x4000 && !io.writes);}
    puts("PASS fixed SysMem GGTT: 2 scatter windows, all-page preflight before writes, actual ownership, empty-window initialization, live mapping preservation, bounded failures/pinning and stopped-consumer scratch retirement.");
}
