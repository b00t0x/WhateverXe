// Execute the real donor CPU helper when its exact binary is supplied.
#include "kern_dsi_native_mode.hpp"
#include "kern_dsi_native_backing.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
using namespace XELP::DSI;
using Display13FBMemory::field;
template<class T>void put(void *p,unsigned offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
AppleAdapter::Timing pixelTiming;
uint32_t validatePixelMode(void *,int32_t,const void **description,const void **timing) {
    if(description)*description=nullptr;if(timing)*timing=&pixelTiming;return 0;
}
size_t copyPixelFormat(char *out,const char *in,size_t size,size_t extent) {
    assert(size<=extent);const auto n=strlen(in);assert(n<size);memcpy(out,in,n+1);return n;
}
void planeBoundary(void *,void *) {}
unsigned notifications=0;
bool noPanelFitter(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint64_t,uint32_t,bool){return false;}
void notify(void *){++notifications;}
void zero(void *p,size_t n){memset(p,0,n);}
unsigned logicalAllocations=0,logicalFrees=0,recordAllocations=0,recordFrees=0;
uint32_t logicalResult=0;
bool failRecord=false;
void *allocateRecord(size_t bytes) {
    assert(bytes==64);if(failRecord)return nullptr;
    ++recordAllocations;return malloc(bytes);
}
void freeRecord(void *p,size_t bytes) {assert(p && bytes==64);++recordFrees;free(p);}
uint32_t logicalCallback(void *,const void *selector,void *,void *raw,void *,void *,void *) {
    auto &r=*static_cast<NativeBacking::Record *>(raw);
    if(selector==reinterpret_cast<void *>(1)) {
        ++logicalAllocations;assert(r.kind==2 && r.columns && r.rows);
        if(logicalResult)return logicalResult;
        r.pitch=r.columns*128;r.gpu=0x100000;r.map=0x200000;r.memory=0x300000;
    } else {assert(selector==reinterpret_cast<void *>(2));++logicalFrees;}
    return 0;
}
void patchCall(uint8_t *image,unsigned call,uintptr_t function) {
    static unsigned nextStub=0x200000;
    const unsigned stub=nextStub;nextStub+=16;assert(nextStub<0x201000);
    image[stub]=0x48;image[stub+1]=0xB8;
    memcpy(image+stub+2,&function,8);image[stub+10]=0xFF;image[stub+11]=0xE0;
    const int32_t delta=stub-call-5;assert(image[call]==0xE8);memcpy(image+call+1,&delta,4);
}
int main(int argc,char **argv) {
    alignas(8) uint8_t fb[0x4B90]{},controller[0x1C20]{},path[0x3730]{};
    put(fb,0x1D0,controller);put(fb,0x4A00,1U);put(fb,0x4A08,path);
    Profile p;p.status=ProfileStatus::Valid;p.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
    AppleAdapter::Timing t;assert(AppleAdapter::makeTiming(p,t));
    unsigned calls=0;
    auto unsupported=[&](void *f,const AppleAdapter::Timing*) {
        ++calls;memset(static_cast<uint8_t*>(f)+0x430C,0x55,0x30);memset(path+0xF0,0x33,0x20);
    };
    // The rejected helper must leave its complete CPU geometry unchanged.
    put(fb,0x4350,8U);put(path,0xF8,640U);
    const std::vector<uint8_t> savedFB(fb,fb+sizeof(fb)),savedPath(path,path+sizeof(path));
    assert(!AppleAdapter::prepareGeometry(fb,path,p,t,unsupported));assert(calls==1);
    assert(!memcmp(fb,savedFB.data(),sizeof(fb)) && !memcmp(path,savedPath.data(),sizeof(path)));
    auto scaled=t;scaled.horizontalScaled=720;
    assert(!AppleAdapter::prepareGeometry(fb,path,p,scaled,unsupported));assert(calls==1);
    put(fb,0x4348,uintptr_t(1));assert(!AppleAdapter::prepareGeometry(fb,path,p,t,unsupported));assert(calls==1);
    put(fb,0x4348,uintptr_t(0));put(fb,0x4A00,2U);
    assert(!AppleAdapter::prepareGeometry(fb,path,p,t,unsupported));assert(calls==1);put(fb,0x4A00,1U);
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        assert(bytes.size()<0x400000);
        auto image=static_cast<uint8_t*>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));
        assert(image!=MAP_FAILED);memcpy(image,bytes.data(),bytes.size());
        patchCall(image,0x3B841,reinterpret_cast<uintptr_t>(zero));
        uintptr_t vtable[0x220/8]{};vtable[0x218/8]=reinterpret_cast<uintptr_t>(notify);
        auto object=vtable;put(controller,0xC10,&object);put(controller,0x1BB4,uint8_t(1));
        put(controller,0xD5C,1U);put(controller,0xD60,fb);put(fb,0x1E0,uint8_t(1));
        alignas(8) uint8_t port[0x580]{},config[0x30]{};
        put(path,0x3648,port);put(port,0x548,config);
        const auto compute=reinterpret_cast<void(*)(void*,const AppleAdapter::Timing*)>(image+0x31484);
        for(int joined:{-1,0})for(bool owner:{false,true}) {
            put(fb,0x3FD8,joined);put(fb,0x49E8,1U);put(fb,0x49F0,path);
            put(controller,0xC0,owner?uintptr_t(1):uintptr_t(0));
            assert(AppleAdapter::prepareGeometry(fb,path,p,t,compute));
            assert(field<uint32_t>(fb,0x4334)==4352 && field<uint32_t>(fb,0x4314)==8355840);
            assert(!field<uintptr_t>(fb,0x4348) && !field<uintptr_t>(fb,0x4340));
            // Exact unscaled logical-buffer path must be a successful no-op.
            assert(reinterpret_cast<uint32_t(*)(void*)>(image+0x31726)(fb)==0);
            assert(!field<uintptr_t>(fb,0x4340));
            reinterpret_cast<void(*)(void*,uint32_t)>(image+0x318E8)(fb,0);
            assert(!field<uint32_t>(fb,0x4000) && field<uint32_t>(fb,0x4008)==UINT32_MAX &&
                   field<uint32_t>(fb,0x400C)==UINT32_MAX && field<uint32_t>(fb,0x4200)==1);
            reinterpret_cast<void(*)(void*)>(image+0x319D2)(fb);
            assert(field<uint32_t>(fb,0x402C)>=1 && field<uint32_t>(fb,0x402C)<=2);

        }
        // setupClocks' CPU assignment at 0x6C719 reads Timing+0x28 and
        // stores path+0x118. Replay both WM consumers without a DDI modeset.
        const uint8_t cacheCopy[]={0x49,0x8B,0x77,0x28,0x48,0x89,0xB3,0x18,0x01,0x00,0x00};
        assert(!memcmp(image+0x6C719,cacheCopy,sizeof(cacheCopy)));
        alignas(8) uint8_t scaler[0x80]{},plane[0x1C8]{};
        put(path,0x32C0,scaler);put(path,0x32C8,plane);
        put(scaler,0x4C,uint32_t(p.mode.width<<16 | p.mode.height));
        put(plane,0x88,uintptr_t(0));put(path,0x118,t.pixelClock);
        const auto adjust=reinterpret_cast<uint64_t(*)(void*,void*,void*,bool,void*)>(image+0x4ACE4);
        assert(adjust(controller,fb,path,false,nullptr)==135255000);
        assert(adjust(controller,fb,path,true,nullptr)==135255000);
        put(scaler,0x4C,0U); // Disabled boot scaler also leaves Hz unchanged.
        assert(adjust(controller,fb,path,false,nullptr)==135255000);
        assert(notifications==4);
        // Quarter-turns use a separate GPU logical buffer, not a rotated
        // hardware scanout. Exercise the exact geometry/allocation/release
        // bodies; only kernel heap and accelerator allocation are mocked.
        patchCall(image,0x31790,reinterpret_cast<uintptr_t>(allocateRecord));
        patchCall(image,0x317A5,reinterpret_cast<uintptr_t>(zero));
        patchCall(image,0x330E3,reinterpret_cast<uintptr_t>(freeRecord));
        uintptr_t gpuVtable[0x6C0/8]{};gpuVtable[0x6B8/8]=reinterpret_cast<uintptr_t>(logicalCallback);
        auto gpuObject=gpuVtable;put(controller,0xC0,&gpuObject);
        put(fb,0x420C,2U);put(fb,0x4AD8,uintptr_t(1));put(fb,0x4AE0,uintptr_t(2));
        put(scaler,0x10,fb);
        const auto allocateLogical=reinterpret_cast<uint32_t(*)(void*)>(image+0x31726);
        const auto releaseLogical=reinterpret_cast<void(*)(void*)>(image+0x33062);
        for(uint32_t rotation:{0x30U,0x50U})for(bool hidpi:{false,true}) {
            auto presentation=t;presentation.scalerFlags=rotation;
            presentation.horizontalScaled=1920;presentation.verticalScaled=1080;
            if(hidpi){presentation.horizontalScaled=2880;presentation.verticalScaled=1620;}
            put(fb,0x4348,uintptr_t(0x12345000));
            assert(AppleAdapter::prepareGeometry(fb,path,p,presentation,compute,true));
            assert(field<uint32_t>(fb,0x430C)==(hidpi?2880U:1920U));
            assert(field<uint32_t>(fb,0x4310)==(hidpi?1620U:1080U));
            for(unsigned offset:{0x4320U,0x4328U})assert(field<uint32_t>(fb,offset)==1080);
            for(unsigned offset:{0x4324U,0x432CU})assert(field<uint32_t>(fb,offset)==1920);
            assert(field<uint32_t>(fb,0x4338)==0x30U);
            assert(field<uint32_t>(fb,0x4334)==(hidpi?11520U:7680U));
            assert(field<uint32_t>(path,0xF8)==1080 && field<uint32_t>(path,0xFC)==1920);
            assert(allocateLogical(fb)==0);
            auto record=field<NativeBacking::Record *>(fb,0x4340);
            assert(record && record->pitch==(hidpi?11520U:7680U));
            assert(record->bytes==record->pitch*(hidpi?1620U:1080U));
            assert(field<uintptr_t>(fb,0x4348)==0x12345000); // Logical allocation preserves the scanout.
            releaseLogical(fb);assert(!field<uintptr_t>(fb,0x4340));
        }
        // Replay the native GPU fallback for a portrait HiDPI source. Only
        // hardware-scaler admission is declined, as for the adopted DSI owner.
        patchCall(image,0x32909,reinterpret_cast<uintptr_t>(noPanelFitter));
        auto portrait=t;portrait.horizontalScaled=1620;portrait.verticalScaled=2880;
        assert(AppleAdapter::prepareGeometry(fb,path,p,portrait,compute,true));
        assert(field<uint32_t>(fb,0x4338)==0x20 && field<uint32_t>(fb,0x4334)==6528);
        assert(field<uint32_t>(path,0xF8)==1080 && field<uint32_t>(path,0xFC)==1920);
        assert(allocateLogical(fb)==0);
        auto portraitRecord=field<NativeBacking::Record *>(fb,0x4340);
        assert(portraitRecord && portraitRecord->bytes==6528U*2880);
        releaseLogical(fb);assert(!field<uintptr_t>(fb,0x4340));
        // A failing accelerator callback still publishes the heap record.
        // A future mode adapter must explicitly retire it before rollback.
        logicalResult=0xE00002BD;assert(allocateLogical(fb)==logicalResult);
        assert(field<uintptr_t>(fb,0x4340));releaseLogical(fb);
        logicalResult=0;failRecord=true;assert(allocateLogical(fb)==0xE00002BD);
        assert(!field<uintptr_t>(fb,0x4340));
        assert(logicalAllocations==6 && logicalFrees==6 && recordAllocations==6 && recordFrees==6);
        // Different render sizes and all ordinary rotations use the same
        // donor contract. No preferred HiDPI size is an admission condition.
        failRecord=false;
        for(uint32_t rotation:{0U,0x30U,0x40U,0x50U})for(unsigned width:{1280U,2560U,3200U}) {
            auto other=t;other.scalerFlags=rotation;
            const auto height=width*9/16;
            other.horizontalScaled=(rotation&0x10)?width:height;
            other.verticalScaled=(rotation&0x10)?height:width;
            assert(AppleAdapter::prepareGeometry(fb,path,p,other,compute,true));
            assert(allocateLogical(fb)==0);
            auto record=field<NativeBacking::Record *>(fb,0x4340);
            assert(record && record->bytes==uint64_t(record->pitch)*other.verticalScaled);
            assert(field<uintptr_t>(fb,0x4348)==0x12345000);
            releaseLogical(fb);assert(!field<uintptr_t>(fb,0x4340));
        }
        assert(logicalAllocations==18 && logicalFrees==18 && recordAllocations==18 && recordFrees==18);
        // Replay the donor's real pixel descriptor producer and native flip
        // consumer. The 10-bit FB uses the same allocation size, while the
        // Timing still requests RGB8 on the wire. Kernel string/memory leaves,
        // scaler/color setup and packet submission are the modeled boundaries.
        pixelTiming=t;
        patchCall(image,0x32500,reinterpret_cast<uintptr_t>(validatePixelMode));
        patchCall(image,0x325A5,reinterpret_cast<uintptr_t>(zero));
        patchCall(image,0x325F1,reinterpret_cast<uintptr_t>(copyPixelFormat));
        patchCall(image,0x3266C,reinterpret_cast<uintptr_t>(copyPixelFormat));
        patchCall(image,0x21CF,reinterpret_cast<uintptr_t>(memcmp));
        patchCall(image,0x2464,reinterpret_cast<uintptr_t>(planeBoundary));
        patchCall(image,0x2923,reinterpret_cast<uintptr_t>(planeBoundary));
        patchCall(image,0x29DF,reinterpret_cast<uintptr_t>(planeBoundary));
        patchCall(image,0x2BE3,reinterpret_cast<uintptr_t>(planeBoundary));
        patchCall(image,0x2CE8,reinterpret_cast<uintptr_t>(planeBoundary));
        patchCall(image,0x2CF8,reinterpret_cast<uintptr_t>(memcpy));
        const auto pixel=reinterpret_cast<uint32_t(*)(void*,int32_t,int32_t,int32_t,void*)>(image+0x324A4);
        const auto flip=reinterpret_cast<void(*)(void*,void*)>(image+0x2198);
        put(plane,0x68,fb);put(plane,0x70,path);
        for(uint32_t depth:{0U,1U,0U,1U}) {
            uint8_t info[172]{};assert(!pixel(fb,AppleAdapter::FixedModeID,depth,0,info));
            assert(field<uint32_t>(info,8)==32 && field<uint32_t>(info,16)==3);
            assert(field<uint32_t>(info,20)==(depth?10:8));
            assert(field<uint32_t>(info,24)==(depth?0x3ff00000U:0xff0000U));
            assert(field<uint32_t>(info,28)==(depth?0xffc00U:0xff00U));
            assert(field<uint32_t>(info,32)==(depth?0x3ffU:0xffU));
            assert(field<uint32_t>(info,0x9C)==1080 && field<uint32_t>(info,0xA0)==1920);
            alignas(8) uint8_t args[0x58]{};
            put(args,0x8,4352U);
            for(unsigned off:{0x14U,0x24U})put(args,off,1080.0f);
            for(unsigned off:{0x18U,0x28U})put(args,off,1920.0f);
            put(args,0x34,depth?0x6C313072U:0x42475241U);put(args,0x38,depth);put(args,0x3C,1U);
            flip(plane,args);
            assert(((field<uint32_t>(plane,0x100)>>23)&31)==(depth?4:8));
            assert(pixelTiming.bitsPerColorComponent==2 && !memcmp(&pixelTiming,&t,sizeof(t)));
        }
        puts("DSI FB descriptor and native flip: RGB8/RGB10 masks, 32-bit storage and plane format replay passed");
        munmap(image,0x400000);puts("DSI native geometry: fixed scanout, quarter-turn logical buffers, HiDPI and failure cleanup replay passed");
    }
    puts("DSI native mode preparation rollback and admission passed");
}
