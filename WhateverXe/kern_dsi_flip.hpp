// Exact TGL FlipTransactionParams adapter. Wire timing and GPU mapping stay native.
#ifndef KERN_DSI_FLIP_HPP
#define KERN_DSI_FLIP_HPP
#include "kern_display13_fb_memory.hpp"
namespace XELP { namespace DSI { namespace AppleAdapter {
constexpr unsigned flipParamsBytes=0xB8;
// Integer-only IEEE754 encoding: kernel code need not borrow SIMD state.
inline uint32_t flipDimensionBits(uint32_t value) {
    if(!value || value>8192)return 0;
    unsigned exponent=0;for(auto n=value;n>1;n>>=1)++exponent;
    return ((127+exponent)<<23) | ((value<<(23-exponent))&0x7FFFFF);
}
// Source dimensions match physical coordinates; this does not prove that its
// pixels have been rotated. Adapt only the observed single-plane, full-screen
// mismatch while qualifying the compositor path; do not reinterpret arbitrary
// crops, scaling, multiple planes, or a destination already in physical space.
// Native FB copies all 0xB8 bytes before retaining transaction state.
inline bool normalizeFullFrameFlip(const void *params,void *copy,uint32_t transform,
                                   uint32_t physicalWidth,uint32_t physicalHeight) {
    using Display13FBMemory::field;
    if(!params || !copy || transform!=0x30 || physicalWidth==physicalHeight ||
       !physicalWidth || !physicalHeight || physicalWidth>8192 || physicalHeight>8192 ||
       field<uint8_t>(params,0)!=1)return false;
    const auto width=flipDimensionBits(physicalWidth),height=flipDimensionBits(physicalHeight);
    const auto args=static_cast<const uint8_t *>(params)+8;
    if((field<uint32_t>(args,0xC)&0x7FFFFFFF) || (field<uint32_t>(args,0x10)&0x7FFFFFFF) ||
       (field<uint32_t>(args,0x1C)&0x7FFFFFFF) || (field<uint32_t>(args,0x20)&0x7FFFFFFF) ||
       field<uint32_t>(args,0x14)!=width || field<uint32_t>(args,0x18)!=height ||
       field<uint32_t>(args,0x24)!=height || field<uint32_t>(args,0x28)!=width)return false;
    memcpy(copy,params,flipParamsBytes);
    memcpy(static_cast<uint8_t *>(copy)+8+0x24,&width,4);
    memcpy(static_cast<uint8_t *>(copy)+8+0x28,&height,4);
    return true;
}
}}}
#endif
