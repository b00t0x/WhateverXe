#include "kern_dsi_test_image.hpp"
#include "kern_display13_fb_memory.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
using namespace XELP::DSI::TestImage;
int main() {
    const auto l=layout(1080,1920,4352,8355840);
    assert(l.valid() && l.columns==34 && l.rows==60);
    std::vector<uint32_t> memory(l.bytes/4+2,0x12345678);
    uint32_t digests[2]{};
    for(unsigned phase=0;phase<2;++phase) {
        const auto result=paint(memory.data()+1,l.bytes,l,phase);
        assert(result.complete && result.bytes==l.bytes);digests[phase]=result.digest;
        assert(memory.front()==0x12345678 && memory.back()==0x12345678);
        // Cross-check tile addressing against the existing scanout sampler;
        // inspect both tile extremes and every tile at the visible edges.
        for(unsigned y=0;y<1920;y+=32)for(unsigned x=0;x<1080;x+=32) {
            uint64_t offset=0;
            assert(Display13FBMemory::scanoutSampleOffset(true,34,1080,1920,0,x,y,offset));
            const auto expected=color(l,x/32,y/32,phase);
            for(unsigned i=0;i<1024;++i)assert(memory[1+offset/4+i]==expected);
        }
    }
    assert(digests[0]!=digests[1]);
    const auto unchanged=memory;
    assert(!paint(memory.data()+1,l.bytes-4,l,0).complete && memory==unchanged);
    assert(!paint(memory.data()+1,l.bytes,l,2).complete && memory==unchanged);
    assert(!paint(nullptr,l.bytes,l,0).complete);
    assert(!layout(1080,1920,4320,8355840).valid());
    assert(!layout(1080,1920,4352,8351744).valid());
    assert(!layout(UINT32_MAX,1920,4352,8355840).valid());
    printf("PASS DSI full allocation image: 2040 tiles, edges, two phases, padding and bounds; digests %08x/%08x\n",digests[0],digests[1]);
}
