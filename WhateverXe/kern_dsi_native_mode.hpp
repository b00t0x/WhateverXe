// CPU-only fixed-mode preparation for the exact TGL framebuffer donor.
// Host programming, backing publication and transaction admission are separate.
#ifndef KERN_DSI_NATIVE_MODE_HPP
#define KERN_DSI_NATIVE_MODE_HPP
#include "kern_dsi_apple_timing.hpp"
#include "kern_display13_fb_memory.hpp"
namespace XELP { namespace DSI { namespace AppleAdapter {
// The caller holds the FB mutex and validates object classes/ownership. No
// wire timing changes are admitted. GPU presentation preparation retains the physical
// scanout; its separate logical buffer must be retired by the caller first.
template<class Compute> bool prepareGeometry(void *fb,void *path,const Profile &profile,
                                             const Timing &timing,Compute compute,bool presentation=false) {
    using Display13FBMemory::field;
    Timing expected;
    if(!makeTiming(profile,expected))return false;
    const bool scaled=presentation && (timing.horizontalScaled || timing.verticalScaled);
    if(presentation) {
        expected.scalerFlags=timing.scalerFlags;
        expected.horizontalScaled=timing.horizontalScaled;expected.verticalScaled=timing.verticalScaled;
        expected.horizontalInset=timing.horizontalInset;expected.verticalInset=timing.verticalInset;
    }
    if(!fb || !path || memcmp(&expected,&timing,sizeof(timing)) ||
       Display13FBMemory::singlePath<void *>(fb)!=path || field<uintptr_t>(fb,0x4340) ||
       (!presentation && field<uintptr_t>(fb,0x4348)))return false;
    // Preserve the native CPU state if the donor produces an unsupported
    // transform. The helper writes only these geometry fields for this mode.
    uint8_t savedFB[0x30],savedPath[0x20];
    memcpy(savedFB,static_cast<uint8_t *>(fb)+0x430C,sizeof(savedFB));
    memcpy(savedPath,static_cast<uint8_t *>(path)+0xF0,sizeof(savedPath));
    const auto bpp=field<uint32_t>(fb,0x4350);const auto tiled=field<uint8_t>(fb,0x4A18);
    const auto put=[](void *o,unsigned offset,uint32_t v){memcpy(static_cast<uint8_t *>(o)+offset,&v,4);};
    put(fb,0x4350,4);*(static_cast<uint8_t *>(fb)+0x4A18)=1;
    compute(fb,&timing);
    const auto &m=profile.mode;const uint32_t width=scaled?timing.horizontalScaled:m.width,height=scaled?timing.verticalScaled:m.height;
    // Check the allocated geometry, not an OS rotation/mode whitelist. This
    // transaction retains the scanout and can own a separate GPU source only.
    const uint32_t pitch=((width*4+127)/128)*128;
    const auto transform=field<uint32_t>(fb,0x4338);
    const bool gpuTransform=(transform&0x30) && !(transform&~0x30U);
    const bool valid=width && height && uint64_t(width)*4+127<=UINT32_MAX &&
        uint64_t(pitch)*height<=UINT32_MAX && field<uint32_t>(fb,0x430C)==width && field<uint32_t>(fb,0x4310)==height &&
        field<uint32_t>(fb,0x4314)==pitch*m.height && field<uint32_t>(fb,0x4320)==m.width &&
        field<uint32_t>(fb,0x4324)==m.height && field<uint32_t>(fb,0x4328)==m.width &&
        field<uint32_t>(fb,0x432C)==m.height && field<uint32_t>(fb,0x4334)==pitch &&
        (gpuTransform || (transform==1 && width==m.width && height==m.height)) && !field<uint64_t>(path,0xF0) &&
        field<uint32_t>(path,0xF8)==m.width && field<uint32_t>(path,0xFC)==m.height &&
        !field<uint64_t>(path,0x100) && field<uint32_t>(path,0x108)==m.width &&
        field<uint32_t>(path,0x10C)==m.height;
    if(!valid) {
        memcpy(static_cast<uint8_t *>(fb)+0x430C,savedFB,sizeof(savedFB));
        memcpy(static_cast<uint8_t *>(path)+0xF0,savedPath,sizeof(savedPath));
        put(fb,0x4350,bpp);*(static_cast<uint8_t *>(fb)+0x4A18)=tiled;
    }
    return valid;
}
}}}
#endif
