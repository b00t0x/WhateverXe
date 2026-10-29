// Exact donor boot/active cache view. This is not a register-address translator or a
// restore/programming image: DSI mode/stop must use their own adapter. DP M/N,
// MSA, normal DDI function and unsupported HBLANK have no DSI counterpart here.
#ifndef KERN_DSI_NATIVE_CACHE_HPP
#define KERN_DSI_NATIVE_CACHE_HPP
#include "kern_dsi_boot.hpp"
namespace XELP { namespace DSI { namespace NativeCache {
struct View {
    uint32_t timing[6] {},source {},conf {},misc {},shift {},csc {},cursor {};
    uint32_t plane[6] {},scaler[3] {};
};
constexpr uint32_t PlaneRegisters[]={0x701A4,0x70180,0x701CC,0x70190,0x70188,0x7019C};
constexpr uint32_t PlaneAddressFields[]={0xA8,0x98,0x9C,0xB4,0xB0,0xB8};
constexpr uint32_t PlaneCacheFields[]={0x110,0x100,0x104,0x11C,0x118,0x120};
// Caller owns the FB and shared power locks. Take two bounded snapshots and
// publish no CPU cache field until both agree and the GOP certificate holds.
template<class IO> bool read(IO &io,const Boot &boot,const XELPMemory::Layout &memory,uint32_t scaler,View &out) {
    if(!boot.complete() || boot.source.identity.pipe!=0 || (scaler && scaler!=0x68180 && scaler!=0x68280))return false;
    const auto capture=[&](View &v) {
        if(!bootUnchanged(io,boot,memory))return false;
        const auto word=[&](uint32_t reg,uint32_t &value) {
            if(!io.readable(reg))return false;value=io.read(reg);return value!=UINT32_MAX;
        };
        // HBLANK is absent in video DSI. Fixed-mode geometry comes from the
        // retained IODetailedTiming array, not this native restore cache.
        const uint32_t timing[]={0x6B000,0,0x6B008,0x6B00C,0x6B010,0x6B014};
        for(unsigned i=0;i<6;++i)if(timing[i] && !word(timing[i],v.timing[i]))return false;
        if(!word(0x6001C,v.source) || !word(0x7B008,v.conf) || !word(0x70030,v.misc) ||
           !word(0x6B028,v.shift) || !word(0x4A480,v.csc) || !word(0x70080,v.cursor))return false;
        for(unsigned i=0;i<6;++i)if(!word(PlaneRegisters[i],v.plane[i]))return false;
        if(scaler && (!word(scaler,v.scaler[0]) || !word(scaler-0x10,v.scaler[1]) || !word(scaler-0xC,v.scaler[2])))return false;
        return v.timing[0]==boot.source.htotal && v.timing[2]==boot.source.hsync &&
            v.timing[3]==boot.source.vtotal && v.timing[4]==v.timing[3] && v.timing[5]==boot.source.vsync &&
            v.source==((boot.source.htotal&65535)<<16 | (boot.source.vtotal&65535)) &&
            v.conf==boot.source.conf && !(v.cursor&63) &&
            v.plane[0]==0 && v.plane[1]==boot.control && v.plane[3]==boot.size &&
            v.plane[4]==boot.stride && v.plane[5]==boot.surface && !(v.scaler[0]&0x80000000U) &&
            bootUnchanged(io,boot,memory);
    };
    View a,b;if(!capture(a) || !capture(b) || memcmp(&a,&b,sizeof(a)))return false;
    out=a;return true;
}
// Once native backing is public, plane surfaces and cursor/color caches
// belong to ordinary native transactions. Check the retained DSI link and
// fixed source geometry, but do not require the obsolete GOP plane image.
template<class IO> bool readActive(IO &io,const Boot &boot,uint32_t scaler,View &out) {
    if(!boot.complete() || boot.source.identity.pipe!=0 || (scaler && scaler!=0x68180 && scaler!=0x68280))return false;
    const auto capture=[&](View &v) {
        if(!sourceUnchanged(io,boot.source))return false;
        const auto word=[&](uint32_t r,uint32_t &value) {
            if(!io.readable(r))return false;value=io.read(r);return value!=UINT32_MAX;
        };
        const uint32_t timing[]={0x6B000,0,0x6B008,0x6B00C,0x6B010,0x6B014};
        for(unsigned i=0;i<6;++i)if(timing[i] && !word(timing[i],v.timing[i]))return false;
        if(!word(0x6001C,v.source) || !word(0x7B008,v.conf) || !word(0x70030,v.misc) ||
           !word(0x6B028,v.shift) || !word(0x4A480,v.csc) || !word(0x70080,v.cursor))return false;
        for(unsigned i=0;i<6;++i)if(!word(PlaneRegisters[i],v.plane[i]))return false;
        if(scaler && (!word(scaler,v.scaler[0]) || !word(scaler-0x10,v.scaler[1]) || !word(scaler-0xC,v.scaler[2])))return false;
        return v.timing[0]==boot.source.htotal && v.timing[2]==boot.source.hsync &&
            v.timing[3]==boot.source.vtotal && v.timing[4]==v.timing[3] && v.timing[5]==boot.source.vsync &&
            v.source==((boot.source.htotal&65535)<<16 | (boot.source.vtotal&65535)) &&
            v.conf==boot.source.conf && sourceUnchanged(io,boot.source);
    };
    View a,b;if(!capture(a) || !capture(b) || memcmp(&a,&b,sizeof(a)))return false;
    out=a;return true;
}
// Class sizes and actual path/plane/scaler associations are checked by the
// kernel adapter. Only cache words change; register addresses/ownership don't.
inline void publish(const View &v,void *path,void *plane,void *scaler) {
    const auto put=[](void *object,uint32_t offset,uint32_t value){memcpy(static_cast<uint8_t *>(object)+offset,&value,4);};
    for(unsigned i=0;i<6;++i)put(path,0x138+4*i,v.timing[i]);
    const uint32_t absent[]={0x12C,0x130,0x134,0x160,0x164,0x168,0x16C};
    for(auto offset:absent)put(path,offset,0);
    put(path,0x150,v.source);put(path,0x154,v.conf);put(path,0x158,v.misc);put(path,0x15C,v.shift);
    put(path,0x18C,v.csc);put(path,0x170,v.cursor&~0x8000U);
    for(unsigned i=0;i<6;++i)put(plane,PlaneCacheFields[i],v.plane[i]);
    if(scaler)for(unsigned i=0;i<3;++i)put(scaler,0x44+4*i,v.scaler[i]);
}
}}}
#endif
