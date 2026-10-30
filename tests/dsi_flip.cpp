#include "kern_dsi_flip.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace XELP::DSI::AppleAdapter;
int main() {
    for(unsigned n=1;n<=8192;++n) {
        float f=n;uint32_t bits;memcpy(&bits,&f,4);assert(flipDimensionBits(n)==bits);
    }
    uint8_t input[flipParamsBytes],copy[flipParamsBytes],original[flipParamsBytes];
    memset(input,0xA5,sizeof(input));input[0]=1;
    const float rects[]={0,0,1080,1920,0,0,1920,1080};
    memcpy(input+0x14,rects,sizeof(rects));memcpy(original,input,sizeof(input));
    assert(normalizeFullFrameFlip(input,copy,0x30,1080,1920));
    assert(!memcmp(input,original,sizeof(input)));
    memcpy(original+0x2C,input+0x1C,8);assert(!memcmp(copy,original,sizeof(copy)));
    // Already normalized, crops, offsets and invalid floating point values are
    // left to the native path. No partial writes on an unsupported request.
    assert(!normalizeFullFrameFlip(copy,original,0x30,1080,1920));
    for(unsigned off=0x14;off<=0x30;off+=4) {
        uint8_t malformed[flipParamsBytes];memcpy(malformed,input,sizeof(input));
        const uint32_t values[]={0x7FC00000,0x7F800000,0xBF800000,0x3F000000};
        for(auto bits:values) {
            memcpy(malformed+off,&bits,4);memset(copy,0x5A,sizeof(copy));
            assert(!normalizeFullFrameFlip(malformed,copy,0x30,1080,1920));
            for(auto byte:copy)assert(byte==0x5A);
        }
    }
    for(unsigned count:{0U,2U,255U}) {
        input[0]=count;assert(!normalizeFullFrameFlip(input,copy,0x30,1080,1920));
    }
    input[0]=1;
    assert(!normalizeFullFrameFlip(input,copy,1,1080,1920));
    assert(!normalizeFullFrameFlip(input,copy,0x30,0,1920));
    assert(!normalizeFullFrameFlip(input,copy,0x30,1080,8193));
    assert(!normalizeFullFrameFlip(nullptr,copy,0x30,1080,1920));
    puts("PASS full-frame DSI flip normalization, byte preservation, unsupported geometry passthrough");
}
