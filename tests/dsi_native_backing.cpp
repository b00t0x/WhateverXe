#include "kern_dsi_native_backing.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace XELP::DSI;
using namespace Display13FBMemory;
template<class T> void put(void *p,unsigned off,T v){memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
struct Fixture {
    alignas(8) uint8_t controller[0x1C20]{},fb[0x4B90]{};
    Profile profile;NativeBacking::Record record;
    XELPMemory::Layout layout=XELPMemory::decode(0x03C1,0x7A800001,0);
    uintptr_t owner=0x1000;uint64_t reserve=0x06192000;
    unsigned drift=0,reads=0;bool badPage=false;
    Fixture(){
        profile.status=ProfileStatus::Valid;profile.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
        assert(NativeBacking::request(profile,3,record));
        record.map=0x2000;record.memory=0x3000;record.gpu=0x10000000;record.pitch=4352;record.refs=1;
        put(controller,0xC0,owner);put(controller,0xD5C,1U);put(controller,0xD60,fb);
        put(fb,0x1D0,controller);put(controller,0xDF0,1U);put(controller,0xDF4,uint8_t(1));put(controller,0xDF5,uint8_t(3));
        put(controller,0x1A10,1U);put(controller,0x1A28,layout.base);
        put(controller,0x1A30,0x800000U);put(controller,0x1A34,0x800U);
    }
    bool memoryReady(uintptr_t map,uintptr_t memory,uintptr_t o,uint64_t n){
        return map==record.map && memory==record.memory && o==owner && n==8355840;
    }
    uint64_t physical(uintptr_t,uint64_t off,uint64_t &span){span=4096;return 0x180000000ULL+off;}
    uint64_t readPTE(uint32_t reg){
        const uint64_t off=(uint64_t(reg)-0x800000)*512-record.gpu;
        assert(off<8355840);++reads;
        if(reads==2)switch(drift){
            case 1:put(fb,0x4348,&record);break;
            case 2:put(controller,0xDF0,3U);put(controller,0xDF4,uint8_t(2));break;
            case 3:put(controller,0x1A30,0x400000U);break;
            case 4:record.pitch+=128;break;
            case 5:put(controller,0xC0,uintptr_t(0));break;
            case 6:put(fb,0x4288,uint8_t(0xFF));break;
        }
        return (0x180000000ULL+off+(badPage && reads==3?4096:0))|0x99;
    }
    SurfaceReadout inspect(){return inspectPendingSurface(*this,controller,sizeof(controller),fb,sizeof(fb),owner,reserve,0x100000000ULL,layout,&record);}
};
int main(){
    Fixture f;const auto proof=f.inspect();assert(proof.status==SurfaceStatus::Complete);
    assert(NativeBacking::allocated(f.record,f.profile,proof) && f.reads==2040);
    assert(!field<uintptr_t>(f.fb,0x4348) && allocatedSlot(f.controller,f.fb)==0);
    assert(inspectSurface(f,f.controller,sizeof(f.controller),f.fb,sizeof(f.fb),f.owner,f.reserve,0x100000000ULL,f.layout).status==SurfaceStatus::Unavailable);
    for(unsigned drift=1;drift<=6;++drift){Fixture changed;changed.drift=drift;assert(changed.inspect().status==SurfaceStatus::StateChanged);}
    for(unsigned fault=0;fault<6;++fault){Fixture bad;
        switch(fault){case 0:put(bad.fb,0x4348,&bad.record);break;case 1:put(bad.fb,0x4288,uint8_t(1));break;
        case 2:put(bad.controller,0x1A28,uint64_t(0));break;case 3:bad.record.gpu=0;break;
        case 4:bad.record.gpu=0x100000000ULL-4096;break;case 5:bad.badPage=true;break;}
        assert(bad.inspect().status!=SurfaceStatus::Complete);
    }
    // A validated record is transferred only in exactly the requested shape.
    for(unsigned fault=0;fault<5;++fault){auto r=f.record;
        switch(fault){case 0:r.refs=2;break;case 1:r.flags=4;break;case 2:r.kind=2;break;case 3:r.bytes=8355840;break;case 4:r.rows=61;break;}
        auto p=proof;p.record=reinterpret_cast<uintptr_t>(&r);assert(!NativeBacking::allocated(r,f.profile,p));
    }
    NativeBacking::Record request;
    assert(NativeBacking::request(f.profile,3,request) && request.columns==34 && request.rows==60 && !request.bytes);
    const auto previous=request;assert(!NativeBacking::request(f.profile,2,request) && !memcmp(&request,&previous,sizeof(request)));
    f.profile.mode.height=0;assert(!NativeBacking::request(f.profile,3,request));
    puts("PASS pending native DSI backing: GOP slot preserved, exact Y request, PTE proof and publication/ownership drift");
}
