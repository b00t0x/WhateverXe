// Host fixtures for the measured profile and Display 13 powered-pipe boundary.
#include <cassert>
#include <map>
#include <vector>
#include <cstdio>
#include "kern_alderlake_n.hpp"
#include "kern_alderlake_n_gpu.hpp"
struct IO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<uint32_t> reads;
    uint32_t limit=0x1000000;
    bool changePower=false;
    bool readable(uint32_t offset) const { return offset <= limit-4; }
    uint32_t read(uint32_t offset) {
        // No TGL FIA or Dekel register may enter this backend.
        assert(offset < 0x163000 || offset >= 0x164000);
        reads.push_back(offset);
        if(changePower && offset==0x45404 && reads.size()>1)return 0;
        return regs[offset];
    }
};
struct LinearIO : IO {
    std::vector<AlderLakeN::Register> writes;
    unsigned ticks=0;
    bool noFrames=false, failEnableOnce=false;
    uint32_t read(uint32_t offset) {
        if(offset==0x70040)return ticks;
        return IO::read(offset);
    }
    void write(uint32_t offset,uint32_t v) {
        writes.push_back({offset,v});
        if(failEnableOnce && offset==0x70180 && v==0x94000008) {failEnableOnce=false;return;}
        regs[offset]=v;
    }
    void delay() { if(!noFrames)++ticks; }
};
struct GTIO : IO {
    std::vector<AlderLakeN::Register> writes;
    bool failGT=false,failReleaseGT=false;
    uint32_t read(uint32_t offset) {
        if(offset!=0xA188 && offset!=0xA278 && offset!=0x130044 && offset!=0xD84)
            assert((regs[0x130044]&1) && (regs[0xD84]&1));
        return IO::read(offset);
    }
    void write(uint32_t offset,uint32_t value) {
        assert(offset==0xA188 || offset==0xA278);writes.push_back({offset,value});
        const uint32_t mask=value>>16;
        regs[offset]=((regs[offset]&0xFFFF)&~mask)|(value&mask)|(mask<<16);
        const auto ack=offset==0xA188 ? 0x130044 : 0xD84;
        if((value&1) && !(offset==0xA188 && failGT))regs[ack]|=1;
        if(!(value&1) && !(offset==0xA188 && failReleaseGT))regs[ack]&=~1U;
    }
    void delay() {}
};
static GTIO firmwareGT() {
    GTIO io;io.regs={{0x9138,1},{0x913c,3},{0x9134,0x88},{0x9118,0xf0},{0x9140,0xe00fe}};
    return io;
}
static LinearIO firmware() {
    LinearIO io;io.regs={{0x45404,0xfc0f},{0x60400,0x90030100},{0x60000,0x0897077f},
        {0x6000c,0x04640437},{0x70008,0xc0000000},{0x70028,0xa0f},
        {0x70180,0x84000008},{0x70188,120},{0x70190,0x0437077f}};
    return io;
}
struct ScanIO : LinearIO {
    std::map<uint32_t,uint64_t> ptes;
    unsigned pteWrites=0,planeWrites=0;
    bool corruptPixels=false,failPTE=false,failFlip=false,failRestore=false,failRestorePTE=false;
    uint32_t pending=0,at=0;
    uint64_t readPTE(uint32_t off) {return ptes[off];}
    void writePTE(uint32_t off,uint64_t v) {
        ++pteWrites;
        if(failPTE && v) {failPTE=false;return;}
        if(failRestorePTE && pteWrites>AlderLakeN::ScanoutPages && v>=0x7A800001 && v<0x80800000) {
            failRestorePTE=false;return;
        }
        ptes[off]=v;
    }
    void write(uint32_t off,uint32_t v) {
        LinearIO::write(off,v);
        if(off==0x7019c) {
            ++planeWrites;pending=v;at=ticks+2;
            if((failFlip && v) || (failRestore && !v))at=UINT32_MAX;
        }
    }
    void delay() {
        LinearIO::delay();
        if(ticks>=at && at!=UINT32_MAX)regs[0x701ac]=pending;
    }
    uint32_t apertureRead(uint32_t off) {
        assert(ptes[0x800000+(((AlderLakeN::ScanoutSlot+off)&~4095U)>>9)]&1);
        return AlderLakeN::patternPixel(off/AlderLakeN::ScanoutBytes,(off%AlderLakeN::ScanoutBytes)/4) ^ (corruptPixels?1:0);
    }
    void fence() {}
    void hold(uint32_t ms) {ticks+=ms/16;}
};
static ScanIO scanFirmware() {
    ScanIO io;static_cast<LinearIO&>(io)=firmware();
    io.regs[0x42084]=0x40000000;io.regs[0x70180]=0x94000008;io.regs[0x70028]=0x2a0f;
    return io;
}
struct GPUIO : ScanIO {
    uint8_t *memory=nullptr;
    bool execute=true;
    unsigned loads=0, flushes=0;
    void flushMemory(void *,uint32_t) {++flushes;}
    uint32_t apertureRead(uint32_t off) {return *reinterpret_cast<uint32_t *>(memory+off);}
    void write(uint32_t off,uint32_t value) {
        if(off==0xA188) {
            const auto mask=value>>16;
            regs[off]=(mask<<16)|((regs[off]&0xffff)&~mask)|(value&mask);
            regs[0x130044]=value&1;return;
        }
        assert(regs[0x130044]&1);
        if(off==0x2209c || off==0x2229c) {
            auto mask=value>>16;regs[off]=(regs[off]&~mask)|(value&mask);return;
        }
        regs[off]=value;
        if(off==0x22550) {
            ++loads;assert(value==1);
            for(unsigned slot=1;slot<8;++slot)assert(!(regs[0x22510+slot*8]&1));
            if(!execute)return;
            auto ring=reinterpret_cast<uint32_t *>(memory+3*4096);
            // Interpret the two actual packets, including opcode/address space
            // and ordered completion location; never use cpu-created results.
            assert(ring[0]==0x10400002 && ring[4]==0x13004002);
            assert(ring[1]==AlderLakeN::GPUBase+AlderLakeN::GPUResultOffset && ring[2]==0);
            assert(ring[5]==((ring[1]+8)|4) && ring[6]==0);
            *reinterpret_cast<uint32_t *>(memory+ring[1]-AlderLakeN::GPUBase)=ring[3];
            *reinterpret_cast<uint32_t *>(memory+(ring[5]&~7U)-AlderLakeN::GPUBase)=ring[7];
            regs[0x22034]=regs[0x22030]=32;regs[0x2209c]=0x200;
            reinterpret_cast<uint32_t *>(memory)[0x2f]=1;
            reinterpret_cast<uint64_t *>(memory+0x40)[0]=1;
        }
    }
};
static GPUIO gpuFirmware(uint8_t *memory) {
    GPUIO io;io.memory=memory;io.regs[0x2209c]=0x200;io.regs[0x22234]=1;
    for(unsigned p=0;p<AlderLakeN::GPUPages;++p)
        io.ptes[0x800000+(AlderLakeN::GPUBase>>9)+p*8]=(0x7A800000ULL+AlderLakeN::GPUBase+p*4096)|1;
    return io;
}
int main() {
    std::vector<uint8_t> gpuMemory(AlderLakeN::GPUBytes);
    uint64_t gpuDMA[AlderLakeN::GPUPages],gpuPrevious[AlderLakeN::GPUPages];
    for(unsigned p=0;p<AlderLakeN::GPUPages;++p) {
        gpuDMA[p]=0x100000000ULL+p*8192;gpuPrevious[p]=(0x7A800000ULL+AlderLakeN::GPUBase+p*4096)|1;
    }
    auto gpu=gpuFirmware(gpuMemory.data());
    auto completed=AlderLakeN::testGPU(gpu,gpuMemory.data(),gpuDMA,gpuPrevious,true);
    assert(completed.status==uint64_t(AlderLakeN::GPUStatus::Complete) && completed.retained && completed.fwReleased);
    assert(completed.result==AlderLakeN::GPUValue && completed.token==AlderLakeN::GPUToken && gpu.loads==1 && completed.head==32);
    auto cpuOnly=gpuFirmware(gpuMemory.data());cpuOnly.execute=false;
    auto timeoutGPU=AlderLakeN::testGPU(cpuOnly,gpuMemory.data(),gpuDMA,gpuPrevious,true);
    assert(timeoutGPU.status==uint64_t(AlderLakeN::GPUStatus::TimedOut) && timeoutGPU.retained && !timeoutGPU.result && !timeoutGPU.token);
    auto occupied=gpuFirmware(gpuMemory.data());occupied.regs[0x22510+7*8]=1;
    auto refusal=AlderLakeN::testGPU(occupied,gpuMemory.data(),gpuDMA,gpuPrevious,true);
    assert(refusal.status==uint64_t(AlderLakeN::GPUStatus::EngineBusy) && !refusal.engineWrites && !refusal.pteWrites && refusal.fwReleased);
    auto irqOwned=gpuFirmware(gpuMemory.data());irqOwned.regs[0x190030]=1;
    assert(AlderLakeN::testGPU(irqOwned,gpuMemory.data(),gpuDMA,gpuPrevious,true).status==uint64_t(AlderLakeN::GPUStatus::EngineBusy));
    auto invalidDMA=gpuFirmware(gpuMemory.data());gpuDMA[0]=1;
    assert(AlderLakeN::testGPU(invalidDMA,gpuMemory.data(),gpuDMA,gpuPrevious,true).status==uint64_t(AlderLakeN::GPUStatus::BadMemory) && invalidDMA.reads.empty());
    gpuDMA[0]=0x100000000ULL;
    auto noGPU=gpuFirmware(gpuMemory.data());
    assert(AlderLakeN::testGPU(noGPU,gpuMemory.data(),gpuDMA,gpuPrevious,false).status==0 && noGPU.reads.empty());
    std::puts("AlderLakeN BCS context/packet encoding, CPU-only timeout, ownership refusal and retained memory fixtures passed");
    auto gt=firmwareGT();gt.regs[0xA188]=8;gt.regs[0x130044]=8;
    auto ready=AlderLakeN::readGT(gt,true);
    assert(ready.status==AlderLakeN::GTStatus::Complete && ready.writes==4 && ready.requestedMask==3 && ready.releasedMask==3);
    assert(ready.resources.eus==24 && ready.resources.l3Banks==4 && ready.count==16 && (gt.regs[0xA188]&0xFFFF)==8);
    // T8 Pro Ubuntu readout: 46D2/rev0, one DSS with all eight EU pairs.
    // Verify the full fuse-read path, including balanced forcewake release.
    auto t8=firmwareGT();t8.regs[0x913c]=1;t8.regs[0x9134]=0;
    auto t8Ready=AlderLakeN::readGT(t8,AlderLakeN::matches(0x46d28086,0));
    assert(t8Ready.status==AlderLakeN::GTStatus::Complete && t8Ready.requestedMask==3 && t8Ready.releasedMask==3);
    assert(t8Ready.resources.slices==1 && t8Ready.resources.dss==1 && t8Ready.resources.nativeSubslices==2);
    assert(t8Ready.resources.eus==16 && t8Ready.resources.l3Banks==4 && t8Ready.resources.urbKiB==256);
    assert(t8Ready.fuses[4]==0xe00fe && !(t8.regs[0x130044]&1) && !(t8.regs[0xD84]&1));
    // ADL-P shares forcewake/readout, not the small-GT/media capacity rule.
    for(unsigned failure=0;failure<5;++failure) {
        auto p=firmwareGT();p.regs[0x913C]=0x3f;p.regs[0x9134]=0;p.regs[0x9118]=0;p.regs[0x9140]=0xe00fa;
        if(failure==1)p.failGT=true;
        if(failure==2)p.failReleaseGT=true;
        if(failure==3)p.regs[0x913C]=UINT32_MAX;
        if(failure==4)p.regs[0x9140]=0xe00ff;
        auto a=AlderLakeN::readGT(p,true,nullptr,XELP::decodeMobileGT);
        using S=AlderLakeN::GTStatus;
        const S expected[]={S::Complete,S::AckTimeout,S::ReleaseFailed,S::Unavailable,S::ProfileMismatch};
        assert(a.status==expected[failure]);
        if(!failure)assert(a.resources.eus==96 && a.resources.nativeSubslices==12 &&
            a.resources.l3Banks==8 && a.resources.urbKiB==512 && a.releasedMask==3);
    }
    auto excludedGT=firmwareGT();assert(AlderLakeN::readGT(excludedGT,false).status==AlderLakeN::GTStatus::NotAdmitted && excludedGT.reads.empty());
    auto busyGT=firmwareGT();busyGT.regs[0xA188]=1;
    assert(AlderLakeN::readGT(busyGT,true).status==AlderLakeN::GTStatus::ExistingOwner && busyGT.writes.empty());
    auto unavailableGT=firmwareGT();unavailableGT.regs[0x130044]=UINT32_MAX;
    assert(AlderLakeN::readGT(unavailableGT,true).status==AlderLakeN::GTStatus::Unavailable && unavailableGT.writes.empty());
    auto timedGT=firmwareGT();timedGT.failGT=true;
    auto timeout=AlderLakeN::readGT(timedGT,true);
    assert(timeout.status==AlderLakeN::GTStatus::AckTimeout && timeout.releasedMask==3 && !timeout.count);
    auto wrongGT=firmwareGT();wrongGT.regs[0x9134]=0xff;
    assert(AlderLakeN::readGT(wrongGT,true).status==AlderLakeN::GTStatus::ProfileMismatch && (wrongGT.regs[0xA188]&0xFFFF)==0);
    auto stuckGT=firmwareGT();stuckGT.failReleaseGT=true;
    auto stuck=AlderLakeN::readGT(stuckGT,true);
    assert(stuck.status==AlderLakeN::GTStatus::ReleaseFailed && stuck.releaseFailedMask==1 && stuck.releasedMask==2);
    std::puts("AlderLakeN GT forcewake ownership, ACK failure, fuse profile and release fixtures passed");
    assert(AlderLakeN::matches(0x46d18086,0));
    for(unsigned id=0x46d0;id<=0x46d4;++id)for(unsigned rev=0;rev<256;++rev)
        assert(AlderLakeN::matches((id<<16)|0x8086,rev));
    assert(!AlderLakeN::family(0x46cf8086) && !AlderLakeN::family(0x46d58086) && !AlderLakeN::family(0x46d11234));
    assert(AlderLakeN::matches(0x46d18086,1));
    assert(!AlderLakeN::matches(0x9a498086,0));
    AlderLakeN::Resources res;
    assert(AlderLakeN::resources(1,3,0x88,0xf0,res));
    assert(res.dss==2 && res.nativeSubslices==4 && res.eus==24 && res.l3Banks==4 && res.urbKiB==256);
    for(auto bad : {0U,1U,0xffU}) {
        assert(!AlderLakeN::resources(1,3,0x88,bad,res));
        assert(res.eus==0 && res.l3Banks==0);
    }
    unsigned profiles=0;
    for(unsigned dss=1;dss<64;++dss)for(unsigned disabled=0;disabled<256;++disabled) {
        unsigned n=__builtin_popcount(dss),eus=2*n*(8-__builtin_popcount(disabled));
        bool expected=n<=2 && (eus==16 || eus==24 || eus==32);
        assert(AlderLakeN::resources(1,dss,disabled,0xf0,res)==expected);
        if(expected) {++profiles;assert(res.eus==eus && res.nativeSubslices==2*n && res.l3Banks==4);}
        else assert(!res.eus);
    }
    assert(profiles>0);
    assert(!AlderLakeN::resources(1,0x40,0,0xf0,res) && !AlderLakeN::resources(1,3,0x100,0xf0,res));
    IO absent;assert(AlderLakeN::readDisplay(absent,false).status==AlderLakeN::ReadoutStatus::NotAdmitted);
    assert(absent.reads.empty());
    // Firmware may drive port B through pipe A; port != pipe is intentional.
    IO a;a.regs[0x45404]=0x00000405;a.regs[0x60400]=0x90000000;
    a.regs[0x45400]=0x802;a.regs[0x45408]=0x2000;a.regs[0x4540C]=0x8000;
    a.regs[0x45450]=8;a.regs[0x45454]=0xC;a.regs[0x4545C]=2;
    auto ra=AlderLakeN::readDisplay(a,true);
    assert(ra.status==AlderLakeN::ReadoutStatus::Complete && ra.poweredPipes==1 && ra.hdmiBPipes==1);
    uint32_t owner=0;
    for(auto reg:{0x45400U,0x45408U,0x4540CU,0x45450U,0x45454U,0x4545CU})
        assert(AlderLakeN::value(ra,reg,owner) && owner==a.regs[reg]);
    for(auto offset:a.reads)for(auto pipe:{1U,2U}) {
        for(auto reg:{0x60400U,0x60404U,0x60540U,0x60030U,0x60034U,0x60040U,0x60044U})
            assert(offset!=reg+0x1000*pipe);
        assert(offset!=0x46140+4*pipe && offset!=0x78000+0x200*pipe && offset!=0x78004+0x200*pipe);
    }
    for(auto offset:a.reads)assert(offset!=0x60038 && offset!=0x6003C && offset!=0x60048 && offset!=0x6004C);
    // Linux reference: pipe B -> Combo B, with the ADL-N PW_B at index 6.
    IO b;b.regs[0x45404]=0x1005;b.regs[0x61400]=0x90000000;
    auto rb=AlderLakeN::readDisplay(b,true);
    assert(rb.status==AlderLakeN::ReadoutStatus::Complete && rb.activePipes==2 && rb.hdmiBPipes==2);
    for(auto offset:b.reads)assert(offset!=0x60400 && offset!=0x62400);
    // Bootstrap reuses one snapshot. A new pipe, rejected admission, and an
    // unreadable BAR must each discard the preceding certificate completely.
    auto reused=ra;
    AlderLakeN::readDisplay(b,true,reused);
    assert(reused.version==2 && reused.status==rb.status && reused.count==rb.count);
    assert(reused.poweredPipes==2 && reused.activePipes==2 && reused.hdmiBPipes==2);
    for(unsigned i=0;i<160;++i) {
        assert(reused.registers[i].offset==rb.registers[i].offset);
        assert(reused.registers[i].value==rb.registers[i].value);
    }
    AlderLakeN::readDisplay(absent,false,reused);
    assert(absent.reads.empty() && reused.status==AlderLakeN::ReadoutStatus::NotAdmitted);
    assert(!reused.count && !reused.poweredPipes && !reused.activePipes && !reused.hdmiBPipes);
    for(auto reg:reused.registers)assert(!reg.offset && !reg.value);
    AlderLakeN::readDisplay(a,true,reused);
    IO badReuse;badReuse.regs[0x45404]=UINT32_MAX;
    AlderLakeN::readDisplay(badReuse,true,reused);
    assert(reused.status==AlderLakeN::ReadoutStatus::Unavailable && reused.count==1);
    assert(!reused.poweredPipes && !reused.activePipes && !reused.hdmiBPipes);
    for(unsigned i=1;i<160;++i)assert(!reused.registers[i].offset && !reused.registers[i].value);
    IO dp;dp.regs[0x45404]=0x1005;dp.regs[0x61400]=0x92000000;
    assert(AlderLakeN::readDisplay(dp,true).hdmiBPipes==0);
    IO unavailable;unavailable.regs[0x45404]=UINT32_MAX;
    assert(AlderLakeN::readDisplay(unavailable,true).status==AlderLakeN::ReadoutStatus::Unavailable);
    assert(unavailable.reads.size()==1);
    IO shortMap;shortMap.limit=0x40000;
    assert(AlderLakeN::readDisplay(shortMap,true).status==AlderLakeN::ReadoutStatus::Unavailable);
    assert(shortMap.reads.empty());
    for(auto reg:{0x60540U,0x60030U,0x60034U,0x60040U,0x60044U,0x46140U,0x78000U,0x78004U}) {
        auto bad=a;bad.regs[reg]=UINT32_MAX;
        assert(AlderLakeN::readDisplay(bad,true).status==AlderLakeN::ReadoutStatus::Unavailable);
    }
    a.changePower=true;a.reads.clear();
    assert(AlderLakeN::readDisplay(a,true).status==AlderLakeN::ReadoutStatus::PowerChanged);
    auto live=firmware();auto snapshot=AlderLakeN::readDisplay(live,true);AlderLakeN::LinearPlan plan;
    assert(AlderLakeN::linearPlan(snapshot,plan)==AlderLakeN::LinearStatus::Ready);
    auto result=AlderLakeN::reinitializeLinear(live,snapshot,true);
    assert(result.status==AlderLakeN::LinearStatus::Success && result.writes==10 && result.framesAfter>result.framesBefore);
    assert(live.regs[0x42084]==0x40000000 && live.regs[0x70028]==0x2a0f && live.regs[0x70180]==0x94000008);
    assert(live.writes[0].offset==0x70180 && live.writes[0].value==0x04000008);
    assert(live.writes[1].offset==0x7019c && live.writes.back().offset==0x7019c);
    // Native bootstrap receives physical DDI from the board before FB start.
    // A must not fail the old hdmiBPipes gate; B diagnostics remain unchanged.
    auto comboA=firmware();comboA.regs[0x60400]=0x88030100;
    const auto aState=AlderLakeN::readDisplay(comboA,true);
    assert(aState.activePipes==1 && !aState.hdmiBPipes);
    assert(AlderLakeN::linearPlan(aState,plan)==AlderLakeN::LinearStatus::NotSingleHDMI);
    assert(AlderLakeN::linearPlan(aState,plan,0)==AlderLakeN::LinearStatus::Ready && plan.pipe==0);
    auto aResult=AlderLakeN::reinitializeLinear(comboA,aState,true,0);
    assert(aResult.status==AlderLakeN::LinearStatus::Success && aResult.writes==result.writes);
    assert(comboA.writes.size()==live.writes.size());
    for(unsigned i=0;i<live.writes.size();++i)
        assert(comboA.writes[i].offset==live.writes[i].offset && comboA.writes[i].value==live.writes[i].value);
    auto wrongPort=firmware();const auto bState=AlderLakeN::readDisplay(wrongPort,true);
    assert(AlderLakeN::reinitializeLinear(wrongPort,bState,true,0).status==AlderLakeN::LinearStatus::NotSingleHDMI && wrongPort.writes.empty());
    for(auto ddi:{2U,UINT32_MAX})
        assert(AlderLakeN::reinitializeLinear(wrongPort,bState,true,ddi).status==AlderLakeN::LinearStatus::NotSingleHDMI && wrongPort.writes.empty());
    auto aDP=firmware();aDP.regs[0x60400]=0x8A030100;
    assert(AlderLakeN::reinitializeLinear(aDP,AlderLakeN::readDisplay(aDP,true),true,0).status==AlderLakeN::LinearStatus::NotSingleHDMI && aDP.writes.empty());
    auto twoHeads=firmware();twoHeads.regs[0x61400]=0x90030100;
    assert(AlderLakeN::reinitializeLinear(twoHeads,AlderLakeN::readDisplay(twoHeads,true),true,0).status==AlderLakeN::LinearStatus::NotSingleHDMI && twoHeads.writes.empty());
    auto changedA=firmware();changedA.regs[0x60400]=0x90030100;
    assert(AlderLakeN::reinitializeLinear(changedA,aState,true,0).status==AlderLakeN::LinearStatus::StateChanged && changedA.writes.empty());
    // Invalid/changed ownership or layout must cause zero MMIO writes.
    for(auto reg:{0x70080U,0x70280U,0x71280U,0x70188U,0x701acU,0x60000U}) {
        auto bad=firmware();bad.regs[reg]=(reg==0x70188?119:reg==0x70080?0x27:0x80000000);
        auto state=AlderLakeN::readDisplay(bad,true);auto rr=AlderLakeN::reinitializeLinear(bad,state,true);
        assert(rr.status!=AlderLakeN::LinearStatus::Success && bad.writes.empty());
    }
    auto changed=firmware();changed.regs[0x70188]=121;
    assert(AlderLakeN::reinitializeLinear(changed,snapshot,true).status==AlderLakeN::LinearStatus::StateChanged && changed.writes.empty());
    auto stopped=firmware();stopped.noFrames=true;
    assert(AlderLakeN::reinitializeLinear(stopped,snapshot,true).status==AlderLakeN::LinearStatus::NoVblank && stopped.writes.empty());
    auto fail=firmware();fail.failEnableOnce=true;
    auto failure=AlderLakeN::reinitializeLinear(fail,snapshot,true);
    assert(failure.status==AlderLakeN::LinearStatus::EnableTimeout && failure.restored);
    assert(fail.regs[0x70180]==0x84000008 && fail.regs[0x42084]==0 && fail.regs[0x70028]==0xa0f);
    std::puts("AlderLakeN identity/resource and powered Display13 readout fixtures passed");
    std::puts("Display13 linear reinitialization, exclusion, vblank and rollback fixtures passed");
    std::vector<uint64_t> dma(AlderLakeN::ScanoutPages);
    for(unsigned i=0;i<dma.size();++i)dma[i]=0x100000000ULL+i*4096;
    auto scan=scanFirmware();auto state=AlderLakeN::readDisplay(scan,true);
    auto mapped=scanFirmware();mapped.ptes[0x800000]=1;mapped.ptes[0x800008]=2;
    auto ggtt=AlderLakeN::readGGTT(mapped,true);
    assert(ggtt.status==1 && ggtt.pages==65536 && ggtt.zero==65534 && ggtt.present==1 && ggtt.other==1);
    assert(ggtt.zeroStart==8192 && ggtt.zeroPages==65534 && !mapped.pteWrites && !mapped.planeWrites);
    assert(ggtt.blocks[0].first==1 && ggtt.blocks[0].zero==2046 && ggtt.blocks[31].zero==2048);
    auto excluded=scanFirmware();assert(AlderLakeN::readGGTT(excluded,false).status==0 && excluded.ptes.empty());
    auto truncated=scanFirmware();truncated.limit=0x800000;
    assert(AlderLakeN::readGGTT(truncated,true).status==2 && truncated.ptes.empty());
    auto unavailableGGTT=scanFirmware();unavailableGGTT.ptes[0x800000]=UINT64_MAX;
    assert(AlderLakeN::readGGTT(unavailableGGTT,true).status==2 && !unavailableGGTT.pteWrites);
    auto success=AlderLakeN::testScanout(scan,state,dma.data(),true);
    assert(success.status==uint64_t(AlderLakeN::ScanoutStatus::Success));
    assert(success.flips==3 && success.planeRestored && success.ptesRestored && !success.retained);
    assert(success.apertureSamples==8192 && !success.apertureErrors && success.pteWrites==8192);
    assert(scan.planeWrites==4 && success.holdMilliseconds==9000);
    for(auto pte:scan.ptes)assert(!pte.second);
    auto absentScan=scanFirmware();
    assert(AlderLakeN::testScanout(absentScan,state,dma.data(),false).status==0 && !absentScan.pteWrites);
    auto busy=scanFirmware();busy.ptes[0x800000+(AlderLakeN::ScanoutSlot>>9)]=1;
    assert(AlderLakeN::testScanout(busy,state,dma.data(),true).status==uint64_t(AlderLakeN::ScanoutStatus::BusyGGTT) && !busy.pteWrites);
    auto invalid=scanFirmware();dma.back()|=1;
    assert(AlderLakeN::testScanout(invalid,state,dma.data(),true).status==uint64_t(AlderLakeN::ScanoutStatus::InvalidDMA) && !invalid.pteWrites);
    dma.back()&=~1ULL;
    auto changedScan=scanFirmware();changedScan.regs[0x7019c]=4096;
    assert(AlderLakeN::testScanout(changedScan,state,dma.data(),true).status==uint64_t(AlderLakeN::ScanoutStatus::StateChanged) && !changedScan.pteWrites);
    auto frozen=scanFirmware();frozen.noFrames=true;
    assert(AlderLakeN::testScanout(frozen,state,dma.data(),true).status==uint64_t(AlderLakeN::ScanoutStatus::FlipTimeout) && !frozen.pteWrites);
    for(unsigned fault=0;fault<3;++fault) {
        auto failed=scanFirmware();failed.corruptPixels=fault==0;failed.failPTE=fault==1;failed.failFlip=fault==2;
        auto result=AlderLakeN::testScanout(failed,state,dma.data(),true);
        assert(result.status==uint64_t(fault==0?AlderLakeN::ScanoutStatus::ApertureMismatch:fault==1?AlderLakeN::ScanoutStatus::PTEReadback:AlderLakeN::ScanoutStatus::FlipTimeout));
        assert(result.planeRestored && result.ptesRestored && !result.retained);
        for(auto pte:failed.ptes)assert(!pte.second);
        if(fault<2)assert(!failed.planeWrites);
    }
    auto pinned=scanFirmware();pinned.failRestore=true;
    auto unsafe=AlderLakeN::testScanout(pinned,state,dma.data(),true);
    assert(unsafe.status==uint64_t(AlderLakeN::ScanoutStatus::RestoreFailed) && unsafe.retained && !unsafe.ptesRestored);
    for(auto pte:pinned.ptes)assert(pte.second);
    auto leased=scanFirmware();
    for(unsigned i=0;i<0x6000000/4096;++i)leased.ptes[0x800000+8*i]=(0x7A800000ULL+4096*i)|1;
    auto baseline=leased.ptes;
    std::vector<uint64_t> predecessor(AlderLakeN::ScanoutPages);
    auto lease=AlderLakeN::leaseGOPGGTT(leased,state,predecessor.data(),true,XELPMemory::decode(0x03C1,0x7A800001,0));
    assert(lease.status==1 && lease.verifiedPages==24576 && lease.gopEnd==7680ULL*1080);
    assert(predecessor.front()==0x7E800001 && predecessor.back()==0x7F7FF001 && !leased.pteWrites);
    auto restored=AlderLakeN::testScanout(leased,state,dma.data(),true,predecessor.data());
    assert(restored.status==uint64_t(AlderLakeN::ScanoutStatus::Success) && restored.ptesRestored && leased.ptes==baseline);
    auto altered=leased;altered.ptes[0x800000+8*24000]^=4096;altered.pteWrites=0;
    assert(AlderLakeN::leaseGOPGGTT(altered,state,predecessor.data(),true,XELPMemory::decode(0x03C1,0x7A800001,0)).status==3 && !altered.pteWrites);
    auto overlapping=state;
    for(unsigned i=0;i<overlapping.count;++i)
        if(overlapping.registers[i].offset==0x7019c || overlapping.registers[i].offset==0x701ac)
            overlapping.registers[i].value=AlderLakeN::ScanoutSlot;
    assert(AlderLakeN::leaseGOPGGTT(leased,overlapping,predecessor.data(),true,XELPMemory::decode(0x03C1,0x7A800001,0)).status==2);
    auto cannotRestore=leased;cannotRestore.pteWrites=0;cannotRestore.failRestorePTE=true;
    auto retained=AlderLakeN::testScanout(cannotRestore,state,dma.data(),true,predecessor.data());
    assert(retained.status==uint64_t(AlderLakeN::ScanoutStatus::RestoreFailed) && retained.planeRestored &&
           !retained.ptesRestored && retained.retained);
    std::puts("AlderLakeN firmware lease qualification, GOP exclusion and exact predecessor restoration fixtures passed");
    std::puts("AlderLakeN owned-GGTT scanout, DMA admission, aperture/flip failures and pinned restore fixtures passed");
}
