// Firmware linear RGB geometry. Preserve actual pitch/timing; no FHD or board
// address assumption. This is admission, not a new mode or PLL calculation.
#ifndef KERN_XELP_GEOMETRY_HPP
#define KERN_XELP_GEOMETRY_HPP
#include <stdint.h>
namespace XELPGeometry {
struct Linear {uint32_t width {},height {},pitch {};uint64_t bytes {};bool valid {};};
inline Linear linear(uint32_t control,uint32_t stride,uint32_t size,uint32_t position,uint32_t offset) {
    Linear r;
    const auto format=(control>>23)&31;
    const uint32_t forbidden=(1U<<15)|(7U<<10)|(1U<<4)|3U;
    if(control==UINT32_MAX || !(control&(1U<<31)) || (format!=4 && format!=8) ||
       (control&forbidden) || !stride || stride>4095 || (size&0xE000E000) || position || offset)return r;
    r.width=(size&8191)+1;r.height=((size>>16)&8191)+1;r.pitch=stride*64;
    if(r.pitch<r.width*4)return {};
    r.bytes=(uint64_t(r.pitch)*r.height+4095)&~4095ULL;r.valid=true;return r;
}
// A smaller firmware plane can remain unscaled in a full native pipe canvas.
inline bool insideCanvas(const Linear &g,uint32_t ht,uint32_t vt,uint32_t source) {
    return g.valid && ht!=UINT32_MAX && vt!=UINT32_MAX &&
        source==(((ht&0x3fff)<<16)|(vt&0x3fff)) &&
        g.width<=(ht&0x3fff)+1 && g.height<=(vt&0x3fff)+1;
}
inline bool timing(const Linear &r,uint32_t ht,uint32_t vt) {
    return r.valid && ht!=UINT32_MAX && vt!=UINT32_MAX && !(ht&0xC000C000) && !(vt&0xC000C000) &&
        (ht&0x3FFF)+1==r.width && (vt&0x3FFF)+1==r.height &&
        ((ht>>16)&0x3FFF)+1>=r.width && ((vt>>16)&0x3FFF)+1>=r.height;
}
}
#endif
