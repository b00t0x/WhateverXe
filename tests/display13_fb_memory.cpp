#include "kern_display13_fb_memory.hpp"
#include <assert.h>
#include <stdio.h>
#include <map>
#include <vector>
using namespace Display13FBMemory;
static const auto memory=XELPMemory::decode(0x03C1,0x7A800001,0);
template<class T> void put(void *p,size_t offset,T value) {memcpy(static_cast<uint8_t *>(p)+offset,&value,sizeof(value));}
struct Fixture {
    uint8_t controller[0x1C20] {},framebuffer[0x4B90] {};
    std::map<uint32_t,uint32_t> registers;
    std::vector<uint64_t> ptes;
    unsigned reads {};
    bool changePool {},changePlane {},missingRegister {};
    bool deadPower {},changeOwner {};
    uint32_t changeRegister {};
    Fixture() : ptes(0x3000000/4096) {
        put(framebuffer,0x1D0,reinterpret_cast<uintptr_t>(controller));
        put(controller,0xD5C,uint32_t(3));put(controller,0xD68,reinterpret_cast<uintptr_t>(framebuffer));
        put(framebuffer,0x1DC,uint32_t(1));
        put(controller,0xDF5,uint8_t(3));put(controller,0xDF4,uint8_t(1));put(controller,0xDF0,uint32_t(1));
        put(controller,0x1A10,uint32_t(1));put(controller,0x1A28,uint64_t(0x7A800000));
        put(controller,0x1A30,uint32_t(0x3000000));put(controller,0x1A34,uint32_t(ptes.size()));
        registers={{0x45404,0xFC0F},{0x42084,0x40000000},{0x60400,0x90030100},{0x70008,0xC0000000},
            {0x70180,0x94000008},{0x70188,120},{0x7018C,0},{0x70190,0x0437077F},
            {0x701A4,0},{0x7019C,0},{0x701AC,0}};
        for(unsigned i=0;i<ptes.size();++i)ptes[i]=(0x7A800000ULL+4096*i)|1;
    }
    bool readable(uint32_t r) {return !(missingRegister && r==0x701AC) && r<0x1000000;}
    uint32_t read(uint32_t r) {assert(registers.count(r));return registers[r];}
    uint64_t readPTE(uint32_t r) {
        assert(r>=0x800000 && !(r&7));const auto page=(r-0x800000)/8;assert(page<ptes.size());++reads;
        if(page==2 && changeOwner)put(controller,0xC0,uintptr_t(0));
        if(page==2 && changePool)put(controller,0x1A30,uint32_t(0x1000000));
        if(page==2 && changePlane)registers[0x7019C]=4096;
        if(page==2 && changeRegister)registers[changeRegister]=0;
        if(page==2 && deadPower)registers[0x45404]=UINT32_MAX;
        return ptes[page];
    }
    Readout inspect(unsigned ddi=1,const Display13TC::Boot *tc=nullptr) {return Display13FBMemory::inspect(*this,controller,sizeof(controller),framebuffer,sizeof(framebuffer),0x10000000,memory,true,0,ddi,tc);}
};
static void testDSIBacking() {
    const auto setup=[](Fixture &f) {
        const std::map<uint32_t,uint32_t> source {
            {0x7B008,0xC0000000},{0x6B400,0x80030006},{0x70008,0},{0x60400,0},
            {0x6B030,0x20130300},{0x6B094,1},{0x6B404,0},{0x6B034,0},{0x64000,0x80000000},
            {0x164280,0x1E07801},{0x46014,0xCC000000},{0x16428C,0x3001FB},{0x164290,0xE84},
            {0x6B000,0x04650437},{0x6B008,0x044F044D},{0x6B00C,0x07D1077F},{0x6B014,0x07A907A7},
            {0x51004,0x40000000},{0x42084,0},{0x70188,68},{0x70190,0x077F0437}};
        for(auto entry:source)f.registers[entry.first]=entry.second;
        return XELP::DSI::decodeReadout([&](uint32_t reg,uint32_t &v) {
            auto i=f.registers.find(reg);if(i==f.registers.end())return false;v=i->second;return true;
        },38400);
    };
    const auto inspect=[](Fixture &f,const XELP::DSI::Readout &source) {
        return Display13FBMemory::inspect(f,f.controller,sizeof(f.controller),f.framebuffer,sizeof(f.framebuffer),
            0x10000000,memory,true,0,0,nullptr,nullptr,false,&source);
    };
    Fixture good;const auto source=setup(good);assert(source.complete());
    auto result=inspect(good,source);assert(result.status==Status::Complete && result.verifiedPages==good.ptes.size());
    // The GOP has global DPT enabled, but its linear plane still addresses GGTT.
    // No acceptance of tiled/rotated geometry or a shortened pool follows.
    for(auto pair:std::map<uint32_t,uint32_t>{{0x70180,0x94000408},{0x70188,67},
            {0x70190,0x0437077F},{0x42084,UINT32_MAX},{0x60400,0x80000000}}) {
        Fixture bad;setup(bad);bad.registers[pair.first]=pair.second;
        assert(inspect(bad,source).status==Status::DisplayState && !bad.reads);
    }
    for(auto reg:{0x7B008U,0x6B400U,0x6B000U,0x16428CU,0x51004U}) {
        Fixture changed;setup(changed);changed.changeRegister=reg;
        assert(inspect(changed,source).status==Status::StateChanged);
    }
    Fixture pte;setup(pte);pte.ptes[7]^=2;assert(inspect(pte,source).status==Status::PTE);
    Fixture lost;setup(lost);lost.deadPower=true;assert(inspect(lost,source).status==Status::StateChanged);
    auto wrong=source;wrong.identity.pipe=1;assert(inspect(good,wrong).status==Status::DisplayState);
}
static void testRelocatedPools() {
    for(auto ggc : {0xFEC1U,0x02C1U,0x03C1U,0x04C1U}) {
        Fixture f;const auto moved=XELPMemory::decode(ggc,0x91000001,1);assert(moved.valid);
        put(f.controller,0x1A28,moved.base);
        for(unsigned i=0;i<f.ptes.size();++i)f.ptes[i]=moved.pte(4096ULL*i);
        assert(Display13FBMemory::inspect(f,f.controller,sizeof(f.controller),f.framebuffer,sizeof(f.framebuffer),0x10000000,moved).status==Status::Complete);
        assert(Display13FBMemory::inspect(f,f.controller,sizeof(f.controller),f.framebuffer,sizeof(f.framebuffer),0x10000000,memory).status==Status::PoolRange);
        f.ptes.back()^=2;
        assert(Display13FBMemory::inspect(f,f.controller,sizeof(f.controller),f.framebuffer,sizeof(f.framebuffer),0x10000000,moved).status==Status::PTE);
    }
}
static void testSlotClaims() {
    uint8_t controller[0x1C20] {},fb[3][0x4B90] {};
    put(controller,0xD5C,uint32_t(3));put(controller,0xDF5,uint8_t(3));
    put(controller,0xDF4,uint8_t(2));put(controller,0xDF0,uint32_t(3));
    for(unsigned i=0;i<3;++i) {
        put(fb[i],0x1DC,uint32_t(i));put(fb[i],0x1D0,reinterpret_cast<uintptr_t>(controller));
        put(fb[i],0x4288,uint8_t(i==2 ? 0xFF : 1-i));
    }
    struct IO {
        uint8_t (*fb)[0x4B90];unsigned missing{UINT32_MAX};
        const void *framebuffer(unsigned i) {return i==missing ? nullptr : fb[i];}
    } io {fb};
    // FB0 owns slot1 and FB1 owns slot0. Index/slot identity is not implied.
    assert(slotClaim(io,controller,1)==reinterpret_cast<uintptr_t>(fb[0]));
    assert(!slotClaim(io,controller,0) && !slotClaim(io,controller,2));
    put(fb[2],0x4288,uint8_t(1));assert(!slotClaim(io,controller,1));
    put(fb[2],0x4288,uint8_t(0xFF));put(fb[2],0x1D0,uintptr_t(0));assert(!slotClaim(io,controller,1));
    put(fb[2],0x1D0,reinterpret_cast<uintptr_t>(controller));put(fb[2],0x1DC,uint32_t(0));assert(!slotClaim(io,controller,1));
    put(fb[2],0x1DC,uint32_t(2));io.missing=2;assert(!slotClaim(io,controller,1));io.missing=UINT32_MAX;
    put(controller,0xDF4,uint8_t(1));assert(!slotClaim(io,controller,1));
    put(controller,0xDF4,uint8_t(2));put(controller,0xDF0,uint32_t(0xB));assert(!slotClaim(io,controller,1));
    put(controller,0xDF0,uint32_t(3));assert(slotClaim(io,controller,1));
}

int main() {
    testDSIBacking();
    for(unsigned ddi=3;ddi<=6;++ddi) {
        Fixture f;const auto tc=ddi-3;
        f.registers[0x45454]=3U<<(2*ddi);f.registers[0x161500+4*tc]=0xC;
        f.registers[0x64000+0x100*ddi]=0x80000040;f.registers[0x46100+4*ddi]=0x80000000;
        f.registers[0x46038+8*tc]=0xCC000000;f.registers[0x46140]=(ddi+3)<<28;
        f.registers[0x60400]=0x80030100|((ddi+1)<<27);
        const auto boot=Display13TC::Boot::capture(f,true,ddi,0);
        assert(boot.valid());
        assert(f.inspect(ddi).status==Status::DisplayState && !f.reads);
        assert(f.inspect(ddi,&boot).status==Status::Complete);
        f.changeRegister=0x46038+8*tc;
        assert(f.inspect(ddi,&boot).status==Status::StateChanged);
    }
    uint64_t platform[0x1C0/8],savedPlatform[0x1C0/8];uint8_t poolController[0x1C20] {};
    memset(platform,0x5A,sizeof(platform));put(platform,0x14,uint32_t(0x4000000));
    memcpy(savedPlatform,platform,sizeof(platform));put(poolController,0xDE8,uint32_t(0x4000000));
    assert(setDisplayPool(poolController,platform,sizeof(platform),0x3C00000));
    assert(field<uint32_t>(platform,0x14)==0x3C00000 && field<uint32_t>(poolController,0xDE8)==0x3C00000);
    put(savedPlatform,0x14,uint32_t(0x3C00000));assert(!memcmp(platform,savedPlatform,sizeof(platform)));
    assert(!setDisplayPool(poolController,platform,sizeof(platform),0x4000000));
    assert(!setDisplayPool(poolController,platform,sizeof(platform),17));
    assert(!setDisplayPool(poolController,platform,0x1BF,0x2000000));
    put(poolController,0xDE8,uint32_t(0x4000000));
    assert(!setDisplayPool(poolController,platform,sizeof(platform),0x2000000));
    assert(!memcmp(platform,savedPlatform,sizeof(platform)));
    testRelocatedPools();
    // Native transition contract: an existing aperture is kept without an
    // accelerator and in server states0/2; an active server replaces it.
    assert(!setupTransition(false,false,1,false).replace);
    assert(!setupTransition(false,true,0,false).replace);
    assert(!setupTransition(false,true,2,false).replace);
    assert(setupTransition(false,true,1,false).replace);
    assert(setupTransition(false,true,UINT32_MAX,false).replace);
    assert(!setupTransition(false,true,1,true).replace);
    assert(setupTransition(false,true,2,true).replace);
    assert(!setupTransition(true,false,0,true).aperture);
    assert(setupTransition(true,false,0,false).replace);
    for(auto width:{1280U,1920U,2560U,3840U})for(auto height:{720U,1080U,1440U,2160U}) {
        Fixture f;f.registers[0x70188]=(width*4+63)/64;f.registers[0x70190]=((height-1)<<16)|(width-1);
        assert(f.inspect().status==Status::Complete);
        f.registers[0x70188]=width*4/64-1;assert(f.inspect().status==Status::DisplayState);
    }
    testSlotClaims();
    for(unsigned slot=0;slot<3;++slot) {
        assert(slotReturned(3,slot,3,7,2,7&~(1U<<slot),0xFF));
        assert(!slotReturned(3,slot,3,7,2,7&~(1U<<((slot+1)%3)),0xFF));
        assert(!slotReturned(3,slot,3,7,3,7,0xFF));
        assert(!slotReturned(3,slot,3,7,2,7&~(1U<<slot),slot));
    }
    assert(slotReturned(3,0xFF,2,5,2,5,0xFF));
    assert(!slotReturned(3,0xFF,2,5,1,1,0xFF));
    assert(!slotReturned(3,0xFF,1,5,1,5,0xFF));
    assert(!slotReturned(3,0xFF,0,8,0,8,0xFF));

    {Pool a {3,0x6195000,0x6195,0x4000000,0x4000,0,0x123400};auto b=a;b.state=0;b.descriptor=0;
     assert(poolReturned(a,b,1));b.bytes-=4096;assert(!poolReturned(a,b,1));b=a;assert(!poolReturned(a,b,1));
     a.state=1;a.base=0;a.firstPage=0;a.descriptor=0;a.physical=0x7A800000;
     assert(poolReturned(a,a,0));b=a;b.descriptor=1;assert(!poolReturned(a,b,0));}
    Fixture paths;
    put(paths.framebuffer,0x4A00,uint32_t(1));put(paths.framebuffer,0x4A08,uintptr_t(0x1110));
    put(paths.framebuffer,0x49E8,uint32_t(1));put(paths.framebuffer,0x49F0,uintptr_t(0x2220));
    assert(singlePath<uintptr_t>(paths.framebuffer)==0x1110);
    put(paths.framebuffer,0x3FD8,int32_t(-1));
    assert(singlePath<uintptr_t>(paths.framebuffer)==0x2220);
    put(paths.framebuffer,0x49E8,uint32_t(0));assert(!singlePath<uintptr_t>(paths.framebuffer));
    put(paths.framebuffer,0x49E8,uint32_t(2));assert(!singlePath<uintptr_t>(paths.framebuffer));
    put(paths.framebuffer,0x3FD8,int32_t(1));assert(!singlePath<uintptr_t>(paths.framebuffer));
    Fixture good;const auto qualified=good.inspect();assert(qualified.status==Status::Complete);
    assert(qualified.verifiedPages==12288 && qualified.pool.physical==0x7A800000 && qualified.surface==0);
    {Fixture slots;assert(allocatedSlot(slots.controller,slots.framebuffer)==0);
     // FB index1 can own slot2. The indexed pool is a CPU readout only;
     // it does not pass the still slot0-only stolen backing inspector.
     put(slots.framebuffer,0x4288,uint8_t(2));put(slots.controller,0xDF0,uint32_t(5));put(slots.controller,0xDF4,uint8_t(2));
     const auto base=0x1A10+40*2;
     put(slots.controller,base,uint32_t(3));put(slots.controller,base+4,uint32_t(0x0A000000));
     put(slots.controller,base+8,uint32_t(0xA000));put(slots.controller,base+0x10,uint64_t(0x12345678));
     put(slots.controller,base+0x18,uint64_t(0));put(slots.controller,base+0x20,uint32_t(0x100000));put(slots.controller,base+0x24,uint32_t(256));
     assert(allocatedSlot(slots.controller,slots.framebuffer)==2);
     const auto p=pool(slots.controller,2);assert(p.state==3 && p.base==0x0A000000 && p.firstPage==0xA000 && p.descriptor==0x12345678 && p.bytes==0x100000 && p.pages==256);
     assert(same(pool(slots.controller),pool(slots.controller,0)) && !pool(slots.controller,3).state && !pool(nullptr).state);
     assert(slots.inspect().status==Status::UnsupportedPool);
     put(slots.controller,0xDF4,uint8_t(1));assert(allocatedSlot(slots.controller,slots.framebuffer)==UINT32_MAX);
     put(slots.controller,0xDF4,uint8_t(2));put(slots.controller,0xDF0,uint32_t(9));assert(allocatedSlot(slots.controller,slots.framebuffer)==UINT32_MAX);
     put(slots.controller,0xDF0,uint32_t(5));put(slots.framebuffer,0x4288,uint8_t(0xFF));assert(allocatedSlot(slots.controller,slots.framebuffer)==UINT32_MAX);}
    // Fixed native SysMem windows include currently unallocated slots. They
    // need allocator ownership separately; a layout grants no mapping rights.
    {Fixture layout;
     put(layout.controller,0xDE4,uint32_t(0x06000000));put(layout.controller,0xDC8,uint32_t(0x180000));
     put(layout.controller,0xDCC,uint32_t(0x12000));put(layout.controller,0xDD0,uint64_t(0x6192000));
     put(layout.controller,0xDD8,uint32_t(0x3000));put(layout.controller,0xDF8,uint32_t(0x6195000));
     put(layout.controller,0xDE8,uint32_t(0x04000000));
     for(unsigned slot=0;slot<3;++slot) {
      const auto o=0x1A10+40*slot;const uint32_t base=slot?0x6195000+(slot-1)*0x04000000:0;
      put(layout.controller,o+4,base);put(layout.controller,o+8,base/4096);
      put(layout.controller,o+0x20,uint32_t(0x04000000));put(layout.controller,o+0x24,uint32_t(0x4000));
     }
     auto r=poolLayout(layout.controller,sizeof(layout.controller),0x10000000);
     assert(r.valid && r.count==3 && r.nativePrefix==0x6195000 && r.fixedPoolEnd==0xE195000);
     assert(r.slots[1].base==r.nativePrefix && r.slots[2].base==0xA195000);
     auto protection=sharedReservation(r,0x10000000,true);
     assert(protection.valid && protection.reservedPrefix==0xE195000 && protection.freeApertureBytes==0x1E6B000);
     uint32_t info[3] {0x6195000,0x40000000,0x9A490000};
     assert(publishSharedReservation(protection,info,sizeof(info),true));
     assert(info[0]==0xE195000 && info[1]==0x40000000 && info[2]==0x9A490000);
     const uint64_t accepted[] {0,0xE195000,0xE195000,0x1E6B000};
     assert(sharedReservationAccepted(protection,0xE195000,accepted));
     const uint64_t oldRanges[] {0,0x6195000,0x6195000,0x9E6B000};
     assert(!sharedReservationAccepted(protection,0x6195000,oldRanges));
     assert(!sharedReservationAccepted(protection,0x6195000,accepted));
     // Failed/native-foreign replies remain byte-for-byte unchanged. The
     // original unified-memory size/platform ID are never overwritten.
     info[0]=0x6195000;assert(!publishSharedReservation(protection,info,sizeof(info),false) && info[0]==0x6195000);
     assert(!publishSharedReservation(protection,info,8,true) && info[0]==0x6195000);
     info[0]=0x6000000;assert(!publishSharedReservation(protection,info,sizeof(info),true) && info[0]==0x6000000);
     const auto single=sharedReservation(r,0x10000000,false);assert(single.valid && single.reservedPrefix==0x6195000);
     info[0]=0x6195000;assert(publishSharedReservation(single,info,sizeof(info),true) && info[0]==0x6195000);
     assert(sharedReservationAccepted(single,0x6195000,oldRanges));
     assert(!sharedReservationAccepted(single,0xE195000,accepted));
     assert(single.freeApertureBytes==0x10000000-0x06195000);
     assert(single.freeApertureBytes-protection.freeApertureBytes==0x08000000);
     auto occupied=r;occupied.slots[1].state=3;occupied.slots[1].descriptor=0x1234000;
     assert(!sharedReservation(occupied,0x10000000,false).valid);
     assert(sharedReservation(occupied,0x10000000,true).valid);
     occupied=r;occupied.slots[2].state=2;
     assert(!sharedReservation(occupied,0x10000000,false).valid);
     occupied=r;occupied.slots[2].descriptor=0x1234000;
     assert(!sharedReservation(occupied,0x10000000,false).valid);
     assert(!sharedReservation(r,0xE195000,true).valid);
     for(unsigned count=1;count<=3;++count) {
      put(layout.controller,0xDF5,uint8_t(count));r=poolLayout(layout.controller,sizeof(layout.controller),0x10000000);
      assert(r.valid && r.fixedPoolEnd==0x6195000+(count-1)*0x04000000);
     }
     put(layout.controller,0xDF5,uint8_t(3));
     assert(!poolLayout(layout.controller,sizeof(layout.controller),0xE194FFF).valid);
     assert(!poolLayout(layout.controller,sizeof(layout.controller)-1,0x10000000).valid);
     auto bad=layout;put(bad.controller,0x1A3C,uint32_t(0x6194000));
     assert(!poolLayout(bad.controller,sizeof(bad.controller),0x10000000).valid);
     bad=layout;put(bad.controller,0x1A20,uint64_t(1));assert(!poolLayout(bad.controller,sizeof(bad.controller),0x10000000).valid);
     bad=layout;put(bad.controller,0xDD8,uint32_t(0));assert(!poolLayout(bad.controller,sizeof(bad.controller),0x10000000).valid);
     bad=layout;put(bad.controller,0x1A38,uint32_t(1));assert(!poolLayout(bad.controller,sizeof(bad.controller),0x10000000).valid);
     bad=layout;put(bad.controller,0x1A38,uint32_t(3));put(bad.controller,0x1A48,uint64_t(0x123400));
     assert(poolLayout(bad.controller,sizeof(bad.controller),0x10000000).valid);
     put(bad.controller,0x1A48,uint64_t(0));assert(!poolLayout(bad.controller,sizeof(bad.controller),0x10000000).valid);
     bad=layout;put(bad.controller,0xDF5,uint8_t(4));assert(!poolLayout(bad.controller,sizeof(bad.controller),0x10000000).valid);
     bad=layout;put(bad.controller,0x1A84,uint32_t(1));assert(!poolLayout(bad.controller,sizeof(bad.controller),0x10000000).valid);
    }
    // Two FBs can share the same accelerator/controller but must use their
    // own independently published backing. No slot0 fallback in hard IRQ.
    {Fixture binding;uint8_t second[0x4B90];memcpy(second,binding.framebuffer,sizeof(second));
     put(second,0x1DC,uint32_t(2));put(binding.controller,0xD70,reinterpret_cast<uintptr_t>(second));
     const uintptr_t owner=0x123400;put(binding.controller,0xC0,owner);
     put(binding.framebuffer,0x4348,uintptr_t(0x111000));put(second,0x4348,uintptr_t(0x222000));
     assert(associations(binding.controller,sizeof(binding.controller),binding.framebuffer,sizeof(binding.framebuffer),owner,true));
     assert(associations(binding.controller,sizeof(binding.controller),second,sizeof(second),owner,true));
     struct Pipe {const void *memoryFramebuffer {};uintptr_t publishedScanout {};};
     Pipe pipes[3] {{binding.framebuffer,0x111000},{second,0x222000},{}};
     Pipe staging[3] {};
     assert(!publishedSurface(staging,binding.framebuffer,0x111000));
     __atomic_store_n(&staging[0].memoryFramebuffer,static_cast<const void *>(binding.framebuffer),__ATOMIC_RELEASE);
     assert(!publishedSurface(staging,binding.framebuffer,0x111000));
     __atomic_store_n(&staging[0].publishedScanout,uintptr_t(0x111000),__ATOMIC_RELEASE);
     assert(publishedSurface(staging,binding.framebuffer,0x111000));
     assert(identityAvailable(pipes,binding.framebuffer,0) && identityAvailable(pipes,second,1));
     assert(!identityAvailable(pipes,binding.framebuffer,1) && !identityAvailable(pipes,second,0));
     auto third=reinterpret_cast<void *>(0x3330);
     assert(identityAvailable(pipes,third,2) && !identityAvailable(pipes,third,0));
     assert(!identityAvailable(pipes,nullptr,2) && !identityAvailable(pipes,third,3));
     assert(boundPipe(pipes,binding.framebuffer)==0 && boundPipe(pipes,second)==1);
     assert(publishedSurface(pipes,binding.framebuffer,0x111000) && publishedSurface(pipes,second,0x222000));
     assert(!publishedSurface(pipes,binding.framebuffer,0x222000) && !publishedSurface(pipes,second,0x111000));
     assert(boundPipe(pipes,nullptr)==UINT32_MAX && boundPipe(pipes,reinterpret_cast<void *>(1))==UINT32_MAX);
     assert(!publishedSurface(pipes,reinterpret_cast<void *>(1),0x111000));
     pipes[2]={binding.framebuffer,0x111000};
     assert(!identityAvailable(pipes,binding.framebuffer,0) && !identityAvailable(pipes,binding.framebuffer,2));
     assert(boundPipe(pipes,binding.framebuffer)==UINT32_MAX && !publishedSurface(pipes,binding.framebuffer,0x111000));
     pipes[2]={};pipes[0].publishedScanout=0;
     assert(!publishedSurface(pipes,binding.framebuffer,0x111000) && publishedSurface(pipes,second,0x222000));}
    assert(apertureRange(qualified.pool,0,0x800000,0x10000000));
    assert(!apertureRange(qualified.pool,0,0,0x10000000));
    assert(!apertureRange(qualified.pool,1,4096,0x10000000));
    assert(!apertureRange(qualified.pool,0,0x3000001,0x10000000));
    assert(!apertureRange(qualified.pool,0x3000000,4096,0x10000000));
    assert(!apertureRange(qualified.pool,0,0x800000,0x400000));
    Fixture accelerator;put(accelerator.controller,0xC0,uintptr_t(1));
    assert(accelerator.inspect().status==Status::Association && !accelerator.reads);
    // Explicit native owner admits only the original stolen pool. A different
    // owner, disappearance during PTE reads, or an IOSurface still fails.
    const uintptr_t owner=0x123400;
    Fixture shared;put(shared.controller,0xC0,owner);
    assert(Display13FBMemory::inspect(shared,shared.controller,sizeof(shared.controller),shared.framebuffer,sizeof(shared.framebuffer),0x10000000,memory,true,owner).status==Status::Complete);
    assert(!associations(shared.controller,sizeof(shared.controller),shared.framebuffer,sizeof(shared.framebuffer),owner+8));
    put(shared.framebuffer,0x4348,uintptr_t(1));
    assert(!associations(shared.controller,sizeof(shared.controller),shared.framebuffer,sizeof(shared.framebuffer),owner));
    put(shared.framebuffer,0x4340,uintptr_t(0x543200));
    assert(!associations(shared.controller,sizeof(shared.controller),shared.framebuffer,sizeof(shared.framebuffer),owner,true));
    assert(!associations(shared.controller,sizeof(shared.controller),shared.framebuffer,sizeof(shared.framebuffer),owner,true,0x543208));
    assert(associations(shared.controller,sizeof(shared.controller),shared.framebuffer,sizeof(shared.framebuffer),owner,true,0x543200));
    assert(!associations(shared.controller,sizeof(shared.controller),shared.framebuffer,sizeof(shared.framebuffer),0,true,0x543200));
    Fixture changedOwner;put(changedOwner.controller,0xC0,owner);changedOwner.changeOwner=true;
    assert(Display13FBMemory::inspect(changedOwner,changedOwner.controller,sizeof(changedOwner.controller),changedOwner.framebuffer,sizeof(changedOwner.framebuffer),0x10000000,memory,true,owner).status==Status::StateChanged);
    Fixture sysmem;put(sysmem.controller,0xE49,uint8_t(1));assert(sysmem.inspect().status==Status::Association);
    Fixture association;put(association.controller,0xD68,uintptr_t(0));assert(association.inspect().status==Status::Association);
    Fixture badIndex;put(badIndex.framebuffer,0x1DC,uint32_t(100));assert(badIndex.inspect().status==Status::Association);
    Fixture alternateSurface;put(alternateSurface.framebuffer,0x4340,uintptr_t(1));assert(alternateSurface.inspect().status==Status::Association);
    Fixture exhausted;put(exhausted.framebuffer,0x4288,uint8_t(0xFF));assert(exhausted.inspect().status==Status::UnsupportedPool);
    Fixture notOwned;put(notOwned.controller,0xDF0,uint32_t(0));assert(notOwned.inspect().status==Status::UnsupportedPool);
    Fixture tooSmall;put(tooSmall.controller,0x1A30,uint32_t(0x1000));put(tooSmall.controller,0x1A34,uint32_t(1));
    assert(tooSmall.inspect().status==Status::DisplayState && !tooSmall.reads);
    Fixture beyondStolen;put(beyondStolen.controller,0x1A30,uint32_t(0x8000000));assert(beyondStolen.inspect().status==Status::PoolRange);
    Fixture overlapsGTT;put(overlapsGTT.controller,0x1A30,uint32_t(0x06000000));put(overlapsGTT.controller,0x1A34,uint32_t(0x6000));
    assert(overlapsGTT.inspect().status==Status::PoolRange && !overlapsGTT.reads);
    Fixture wrongBacking;put(wrongBacking.controller,0x1A28,uint64_t(0x7A801000));assert(wrongBacking.inspect().status==Status::PoolRange);
    Fixture noDPTDisable;noDPTDisable.registers[0x42084]=0;assert(noDPTDisable.inspect().status==Status::DisplayState);
    Fixture unpowered;unpowered.registers[0x45404]=3;assert(unpowered.inspect().status==Status::DisplayState && !unpowered.reads);
    Fixture tiled;tiled.registers[0x70180]|=1U<<10;assert(tiled.inspect().status==Status::DisplayState);
    Fixture portA;portA.registers[0x60400]=0x88030100;assert(portA.inspect().status==Status::DisplayState);
    assert(portA.inspect(0).status==Status::Complete && portA.reads==portA.ptes.size());
    Fixture mismatchedPort;assert(mismatchedPort.inspect(0).status==Status::DisplayState && !mismatchedPort.reads);
    for(auto port:{2U,UINT32_MAX}) {
        Fixture invalidPort;assert(invalidPort.inspect(port).status==Status::DisplayState && !invalidPort.reads);
    }
    Fixture changedA;changedA.registers[0x60400]=0x88030100;changedA.changeRegister=0x60400;
    assert(changedA.inspect(0).status==Status::StateChanged);
    Fixture flip;flip.registers[0x701AC]=4096;assert(flip.inspect().status==Status::DisplayState);
    Fixture tail;tail.ptes.back()=0;const auto partial=tail.inspect();
    assert(partial.status==Status::PTE && partial.verifiedPages==12287);
    Fixture scratch;scratch.ptes[17]=scratch.ptes[0];assert(scratch.inspect().status==Status::PTE);
    Fixture local;
    local.ptes[17]|=2;assert(local.inspect().status==Status::PTE);
    Fixture mutablePool;mutablePool.changePool=true;assert(mutablePool.inspect().status==Status::StateChanged);
    Fixture mutablePlane;mutablePlane.changePlane=true;assert(mutablePlane.inspect().status==Status::StateChanged);
    for(auto reg : {0x45404U,0x42084U,0x60400U,0x70008U}) {
        Fixture changed;changed.changeRegister=reg;assert(changed.inspect().status==Status::StateChanged);
    }
    Fixture missing;missing.missingRegister=true;assert(missing.inspect().status==Status::Unavailable && !missing.reads);
    // Cold allocation owns slot0/PW1 and the identical backing, while every
    // pipe bank can be off. No pipe register read or active-display claim.
    Fixture cold;cold.registers={{0x45404,3}};cold.missingRegister=true;
    const auto backing=Display13FBMemory::inspect(cold,cold.controller,sizeof(cold.controller),cold.framebuffer,sizeof(cold.framebuffer),0x10000000,memory,false);
    assert(backing.status==Status::Complete && backing.verifiedPages==cold.ptes.size() && !backing.control && !backing.surface && !backing.size);
    Fixture freed;put(freed.framebuffer,0x4288,uint8_t(0xFF));put(freed.controller,0xDF0,uint32_t(0));
    assert(Display13FBMemory::inspect(freed,freed.controller,sizeof(freed.controller),freed.framebuffer,sizeof(freed.framebuffer),0x10000000,memory,false).status==Status::UnsupportedPool);
    put(freed.framebuffer,0x4288,uint8_t(0));put(freed.controller,0xDF0,uint32_t(1));freed.registers={{0x45404,3}};
    assert(Display13FBMemory::inspect(freed,freed.controller,sizeof(freed.controller),freed.framebuffer,sizeof(freed.framebuffer),0x10000000,memory,false).status==Status::Complete);
    Fixture coldPTE;coldPTE.registers={{0x45404,3}};coldPTE.ptes.back()=0;
    assert(Display13FBMemory::inspect(coldPTE,coldPTE.controller,sizeof(coldPTE.controller),coldPTE.framebuffer,sizeof(coldPTE.framebuffer),0x10000000,memory,false).status==Status::PTE);
    Fixture dead;dead.deadPower=true;assert(dead.inspect().status==Status::StateChanged);
    assert(!associations(good.controller,0x1C1F,good.framebuffer,sizeof(good.framebuffer)));
    assert(!associations(good.controller,sizeof(good.controller),good.framebuffer,0x4B8F));
    puts("AlderLakeN native stolen-pool/aperture fixtures passed");
}
