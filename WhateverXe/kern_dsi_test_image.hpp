// P2 identification image for an uncompressed Y-tiled RGB8888 allocation.
// Fill every 4 KiB tile with one color, avoiding assumptions about its internal
// pixel swizzle. The last tiles include the padding beyond the visible mode.
#ifndef KERN_DSI_TEST_IMAGE_HPP
#define KERN_DSI_TEST_IMAGE_HPP
#include <stdint.h>
#include <stddef.h>
namespace XELP { namespace DSI { namespace TestImage {
struct Layout {
    uint32_t columns {},rows {};uint64_t bytes {};
    bool valid() const {return columns>=2 && columns<=256 && rows>=2 && rows<=256 &&
        bytes==uint64_t(columns)*rows*4096;}
};
inline Layout layout(uint32_t width,uint32_t height,uint32_t pitch,uint64_t bytes) {
    Layout result;
    if(width<33 || height<33 || width>8192 || height>8192 || pitch!=((width*4+127)&~127U))return result;
    result={(width+31)/32,(height+31)/32,bytes};return result.valid()?result:Layout{};
}
// Phase changes all cells. Red/green vertical edges and blue/yellow horizontal
// edges identify the portrait orientation; four distinct corners identify crop.
inline uint32_t color(const Layout &l,uint32_t x,uint32_t y,unsigned phase) {
    uint32_t rgb=(((x/3+y/3)&1)?0x204060U:0xC08020U);
    if(y==0)rgb=0x2040FF;
    if(y+1==l.rows)rgb=0xFFFF20;
    if(x==0)rgb=0xFF2020;
    if(x+1==l.columns)rgb=0x20FF20;
    if(x==0 && y==0)rgb=0xFFFFFF;
    if(x+1==l.columns && y==0)rgb=0xFF20FF;
    if(x==0 && y+1==l.rows)rgb=0x20FFFF;
    if(x+1==l.columns && y+1==l.rows)rgb=0x808080;
    return 0xFF000000U|(phase ? rgb^0xFFFFFFU : rgb);
}
struct Result {bool complete {};uint64_t bytes {};uint32_t digest {2166136261U};};
// The adapter maps the verified SysMemory descriptor, owns GPU serialization,
// and flushes CPU cache lines before scanout. This core never owns a mapping.
inline Result paint(volatile uint32_t *memory,uint64_t length,const Layout &l,unsigned phase) {
    Result r;if(!memory || (reinterpret_cast<uintptr_t>(memory)&3) || !l.valid() || length<l.bytes || phase>1)return r;
    for(uint32_t y=0;y<l.rows;++y)for(uint32_t x=0;x<l.columns;++x) {
        const auto value=color(l,x,y,phase);const auto start=(uint64_t(y)*l.columns+x)*1024;
        for(unsigned word=0;word<1024;++word)memory[start+word]=value;
    }
    // Read back every word, including edge-tile padding. Success is CPU memory
    // evidence only; the adapter must separately prove the live plane/latch.
    for(uint32_t y=0;y<l.rows;++y)for(uint32_t x=0;x<l.columns;++x) {
        const auto expected=color(l,x,y,phase);const auto start=(uint64_t(y)*l.columns+x)*1024;
        for(unsigned word=0;word<1024;++word) {
            const auto actual=memory[start+word];if(actual!=expected)return r;
            r.digest=(r.digest^actual)*16777619U;r.bytes+=4;
        }
    }
    r.complete=r.bytes==l.bytes;return r;
}
}}}
#endif
