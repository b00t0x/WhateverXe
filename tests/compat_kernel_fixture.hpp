// Bounded IOKit/clock boundary for the complete Compat translation unit.
// Allocation failures/physical spans are injected; no GPU or mapper is emulated.
#ifndef COMPAT_KERNEL_FIXTURE_HPP
#define COMPAT_KERNEL_FIXTURE_HPP
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
struct Meta { unsigned size=0x1C20; unsigned getClassSize(){return size;} };
struct OSObject {
    virtual ~OSObject()=default;
    alignas(8) uint8_t storage[0x1C20] {};
    Meta meta; unsigned retains=0, releases=0;
    Meta *getMetaClass(){return &meta;}
    void retain(){++retains;}
    void release(){++releases;}
};
struct OSData:OSObject {uint32_t value{};size_t size=4;
    void *getBytesNoCopy(){return &value;} size_t getLength(){return size;}};
struct IORegistryEntry {
    OSData token;OSObject *activation=nullptr;bool bound=false;unsigned publications=0;
    static IORegistryEntry *getRegistryRoot(){static IORegistryEntry root;return &root;}
    OSObject *getProperty(const char *name){assert(!strcmp(name,"WhateverXeGraphicsABI"));return activation;}
    void setProperty(const char *name,void *bytes,unsigned size) {
        assert(!strcmp(name,"WhateverXeGraphicsABI") && size==4);
        memcpy(&token.value,bytes,4);token.size=size;activation=&token;
    }
    void setProperty(const char *name,bool value) {
        assert(!strcmp(name,"XELPSharedProbeBound"));bound=value;++publications;
    }
};
struct IOService:OSObject {OSObject *provider{}; OSObject *getProvider(){return provider;}};
struct IOPCIDevice:OSObject {uint32_t identity{};uint8_t revision{};OSObject *property{};
    OSObject *getProperty(const char *key){assert(!strcmp(key,"device-id"));return property;}
    uint32_t configRead32(unsigned r){assert(!r);return identity;}
    uint8_t configRead8(unsigned r){assert(r==8);return revision;}
};
struct IOMemoryMap:OSObject {uintptr_t address{};uint64_t bytes{};
    uintptr_t getVirtualAddress(){return address;} uint64_t getLength(){return bytes;}};
using IOByteCount=uint64_t;
using IOReturn=int32_t;
constexpr IOReturn kIOReturnSuccess=0,kIOReturnUnsupported=-1,kIOReturnBadArgument=-2,
    kIOReturnNoMemory=-3,kIOReturnNoResources=-4;
static unsigned page_size=4096;
struct Allocation {
    bool fail=false,nullBytes=false;
    uint64_t physical=0x12345000,span=0x12000;
    unsigned calls=0,options=0,alignment=0,segmentOptions=0;
    uint64_t bytes=0;
    void **publication=nullptr;
} allocation;
struct IOBufferMemoryDescriptor:OSObject {
    std::vector<uint8_t> bytes;
    explicit IOBufferMemoryDescriptor(size_t size):bytes(size,0xA5){}
    static std::vector<std::unique_ptr<IOBufferMemoryDescriptor>> owned;
    static IOBufferMemoryDescriptor *withOptions(unsigned options,uint64_t bytes,unsigned alignment) {
        ++allocation.calls;allocation.options=options;allocation.bytes=bytes;allocation.alignment=alignment;
        if(allocation.publication)assert(!*allocation.publication);
        if(allocation.fail)return nullptr;
        owned.emplace_back(new IOBufferMemoryDescriptor(bytes));return owned.back().get();
    }
    uint64_t getPhysicalSegment(uint64_t offset,IOByteCount *length,unsigned options) {
        assert(offset==0);allocation.segmentOptions=options;*length=allocation.span;
        if(allocation.publication)assert(!*allocation.publication);
        return allocation.physical;
    }
    void *getBytesNoCopy(){return allocation.nullBytes?nullptr:bytes.data();}
};
std::vector<std::unique_ptr<IOBufferMemoryDescriptor>> IOBufferMemoryDescriptor::owned;
static uint64_t clockTicks=0,clockScale=1;
inline void clock_get_uptime(uint64_t *ticks){*ticks=clockTicks;}
inline void absolutetime_to_nanoseconds(uint64_t ticks,uint64_t *ns){*ns=ticks*clockScale;}
using kern_return_t=int;
struct kmod_info_t {};
using kmod_start_func_t=kern_return_t(kmod_info_t *,void *);
using kmod_stop_func_t=kmod_start_func_t;
constexpr int KERN_SUCCESS=0,KERN_FAILURE=5;
#define KMOD_EXPLICIT_DECL(...)
#define OSDynamicCast(T,p) dynamic_cast<T *>(p)
#define IOLog(...) ((void)0)
#ifndef __APPLE_CC__
#define __APPLE_CC__ 0
#endif
#endif
