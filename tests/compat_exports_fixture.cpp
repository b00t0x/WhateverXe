#include <cstdio>
#include <climits>
#include <cstdlib>
#include <limits>
#include "compat_kernel_fixture.hpp"
extern "C" const char version[]="Darwin Kernel Version 25.6.9: synthetic; root:xnu-test/RELEASE_X86_64";
#include "AppleIntelTGLCompat/AppleIntelTGLCompat.cpp"

// No fake vtable selection: the real export must deliver the receiver,
// offset and value to the manager's DSB-aware virtual slot.
struct RawAccess {uintptr_t *vtable;alignas(8) uint8_t fields[0x70]{};};
static void *receiver;static unsigned calls;static unsigned long writtenOffset;static uint64_t writtenValue;
static void write32(void *a,unsigned long o,uint32_t v){receiver=a;++calls;writtenOffset=o;writtenValue=v;}
static void write64(void *a,unsigned long o,uint64_t v){receiver=a;++calls;writtenOffset=o;writtenValue=v;}
struct Machine {
    IOService controller;IOPCIDevice pci;RawAccess access{};
    uintptr_t slots[0x140/8]{};
    std::vector<uint64_t> memory;
    explicit Machine(size_t bytes=64):memory(bytes/8,0xA5A5A5A5A5A5A5A5ULL) {
        pci.identity=0x9A498086;controller.provider=&pci;
        slots[0x130/8]=reinterpret_cast<uintptr_t>(write32);slots[0x138/8]=reinterpret_cast<uintptr_t>(write64);
        access.vtable=slots;field<void *>(&controller,0xC40)=&access;
        field<void *>(&access,0x50)=memory.data();field<uint32_t>(&access,0x60)=bytes;
    }
    void setup(uint64_t ggtt,uint32_t pteBytes) {
        field<uint32_t>(&controller,0xDCC)=0x12000;
        field<uint64_t>(&controller,0xC50)=ggtt;
        field<void *>(&controller,0xCA0)=reinterpret_cast<uint8_t *>(memory.data())+0x800000;
        field<uint32_t>(&controller,0xCEC)=pteBytes;
        field<uint32_t>(&controller,0xCF0)=0x40000;
        allocation={};allocation.publication=&field<void *>(&controller,0xCD8);
    }
};
static void mmio32() {
    Machine m;auto p=reinterpret_cast<uint32_t *>(m.memory.data());p[0]=0x12345678;p[15]=0x76543210;
    assert(tglRead32(&m.controller,0)==p[0]);
    assert(tglRead32(&m.controller,60)==p[15]); // complete last DWORD belongs to the mapping
    for(auto size:{0U,1U,2U,3U}){field<uint32_t>(&m.access,0x60)=size;assert(!tglRead32(&m.controller,0));}
    field<uint32_t>(&m.access,0x60)=64;
    assert(!tglRead32(&m.controller,64));assert(!tglRead32(&m.controller,ULONG_MAX));
    tglWrite32(&m.controller,12,0xDEADBEEF);
    assert(calls==1 && receiver==&m.access && writtenOffset==12 && writtenValue==0xDEADBEEF);
    assert(p[3]==0xA5A5A5A5); // direct store would bypass DSB queueing
}
static void mmio64(bool external) {
    Machine m;uint64_t other[8]{};other[7]=0xFEDCBA9876543210ULL;
    auto base=external?other:m.memory.data();base[7]=other[7];
    assert(tglRead64(&m.controller,base,56)==0xFEDCBA9876543210ULL);
    tglWrite64(&m.controller,base,56,0x123456789ABCDEF0ULL);
    if(external){assert(base[7]==0x123456789ABCDEF0ULL && !calls);}
    else {assert(calls==1 && receiver==&m.access && writtenOffset==56 && writtenValue==0x123456789ABCDEF0ULL);}
    // Non-MMIO writes inherit the donor's manager-span bound; an arbitrary
    // external allocation's size remains the caller's responsibility.
    if(external){auto snapshot=other[7];tglWrite64(&m.controller,base,64,1);assert(other[7]==snapshot);}
    else assert(!tglRead64(&m.controller,base,64));
}
static void gates() {
    Machine m;m.pci.identity=0x46D18086;
    const auto before=m.memory;
    assert(!tglRead32(&m.controller,0));assert(!tglRead64(&m.controller,reinterpret_cast<void *>(1),0));
    tglWrite32(&m.controller,0,1);tglWrite64(&m.controller,reinterpret_cast<void *>(1),0,1);
    assert(tglSetupDSB(&m.controller)==kIOReturnUnsupported);
    assert(!calls && !allocation.calls && m.memory==before);
    // Display12 ADL-S uses the generic manager without acquiring TGL's
    // unbound port-probe fallback; a Display13 owner never reaches it.
    m.pci.identity=0x46908086;m.pci.revision=12;
    assert(tglRead32(&m.controller,0)==0xA5A5A5A5);
    tglWrite32(&m.controller,4,7);assert(calls==1 && receiver==&m.access);
}
static void dsbSuccess(uint64_t ggtt,uint32_t pteBytes) {
    Machine m(0x1000000);m.setup(ggtt,pteBytes);auto before=m.memory;
    assert(tglSetupDSB(&m.controller)==kIOReturnSuccess);
    auto buffer=field<IOBufferMemoryDescriptor *>(&m.controller,0xCD8);
    assert(buffer && !buffer->releases && allocation.calls==1);
    assert(allocation.bytes==0x12000 && allocation.options==0x90 && allocation.alignment==4096 && allocation.segmentOptions==0x40000);
    for(auto byte:buffer->bytes)assert(!byte);
    // One PTE per 4 KiB page, Gen12 physical address and valid/writable bits.
    // Check every other word too, so partial/wrong-base/extra writes cannot pass.
    const size_t first=(0x800000+ggtt/4096*8)/8;
    for(size_t i=0;i<m.memory.size();++i)
        assert(m.memory[i]==(i>=first && i<first+18 ? allocation.physical+(i-first)*4096+7 : before[i]));
    assert(tglSetupDSB(&m.controller)==kIOReturnSuccess && allocation.calls==1);
}
static void dsbFailure(const char *mode) {
    Machine m(0x1000000);m.setup(0x4000000,0x100000);
    IOReturn expected=kIOReturnNoResources;bool preflight=false;
    if(!strcmp(mode,"alloc")){allocation.fail=true;expected=kIOReturnNoMemory;}
    else if(!strcmp(mode,"short"))allocation.span=0x11FFF;
    else if(!strcmp(mode,"null-bytes"))allocation.nullBytes=true;
    else if(!strcmp(mode,"zero-physical"))allocation.physical=0;
    else if(!strcmp(mode,"unaligned"))allocation.physical++;
    else if(!strcmp(mode,"overflow"))allocation.physical=0xFFFFFFFFFFFF0000ULL;
    else if(!strcmp(mode,"pte-window")){field<uint64_t>(&m.controller,0xC50)=0x20000000;preflight=true;}
    else if(!strcmp(mode,"pte-base")){field<void *>(&m.controller,0xCA0)=reinterpret_cast<uint8_t *>(m.memory.data())+0x700000;preflight=true;}
    else if(!strcmp(mode,"ggtt-alignment")){field<uint64_t>(&m.controller,0xC50)++;preflight=true;}
    else if(!strcmp(mode,"fixed-size")){field<uint32_t>(&m.controller,0xDCC)=0x13000;preflight=true;}
    else {assert(!strcmp(mode,"page-size"));page_size=16384;preflight=true;}
    if(preflight)expected=kIOReturnBadArgument;
    auto before=m.memory;const auto owned=IOBufferMemoryDescriptor::owned.size();
    assert(tglSetupDSB(&m.controller)==expected);
    assert(!field<void *>(&m.controller,0xCD8) && m.memory==before);
    assert(allocation.calls==(preflight?0U:1U));
    if(!preflight && !allocation.fail)assert(IOBufferMemoryDescriptor::owned.at(owned)->releases==1);
    // A failed attempt neither publishes readiness nor blocks a valid retry.
    page_size=4096;m.setup(0x4000000,0x100000);
    assert(tglSetupDSB(&m.controller)==kIOReturnSuccess);
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"plugin-off")) {
        auto root=IORegistryEntry::getRegistryRoot();
        assert(!WEXGraphicsActivation::ready());
        assert(tglStart(nullptr,nullptr)==KERN_FAILURE);
        OSObject foreign;root->activation=&foreign;
        assert(tglStart(nullptr,nullptr)==KERN_FAILURE);
        root->activation=&root->token;
        for(unsigned abi:{0U,2U,0xFFFFFFFFU}) {
            root->token.value=abi;
            assert(tglStart(nullptr,nullptr)==KERN_FAILURE);
        }
        root->token.value=1;
        for(unsigned bytes:{0U,1U,3U,5U,8U}) {
            root->token.size=bytes;
            assert(tglStart(nullptr,nullptr)==KERN_FAILURE);
        }
        assert(!hostStarts);
        activateFixturePlugin();
        assert(hostStarts==1 && WEXGraphicsActivation::ready());
        assert(tglStart(nullptr,nullptr)==KERN_SUCCESS);
        return 0;
    }
    activateFixturePlugin();
    assert(argc==2);const char *mode=argv[1];
    if(!strcmp(mode,"mmio32"))mmio32();
    else if(!strcmp(mode,"mmio64"))mmio64(false);
    else if(!strcmp(mode,"external64"))mmio64(true);
    else if(!strcmp(mode,"gates"))gates();
    else if(!strcmp(mode,"dsb"))dsbSuccess(0x4000000,0x100000);
    else if(!strcmp(mode,"dsb-last"))dsbSuccess(0x100000000ULL-0x12000,0x800000);
    else if(!strcmp(mode,"dsb-last-small"))dsbSuccess(0x20000000ULL-0x12000,0x100000);
    else if(!strcmp(mode,"clock")){
        clockTicks=100;clockScale=7;assert(tglPMT(nullptr)==700);
        assert(tglStart(nullptr,nullptr)==KERN_SUCCESS && tglStop(nullptr,nullptr)==KERN_FAILURE);
        static_assert(sizeof(tglDSBBusy)==12, "native three uint32 DSB engines");
        for(auto busy:tglDSBBusy)assert(!busy);
    } else dsbFailure(mode);
    printf("PASS Compat export %s: actual entry and memory/dispatch/ownership effect. CPU only.\n",mode);
}
