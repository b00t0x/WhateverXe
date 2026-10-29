// Execute the real donor CPU helper when its exact binary is supplied.
#include "kern_dsi_native_mode.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
using namespace XELP::DSI;
using Display13FBMemory::field;
template<class T>void put(void *p,unsigned offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
unsigned notifications=0;
void notify(void *){++notifications;}
void zero(void *p,size_t n){memset(p,0,n);}
void patchCall(uint8_t *image,unsigned call,uintptr_t function) {
    const unsigned stub=0x200000;image[stub]=0x48;image[stub+1]=0xB8;
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
        assert(notifications==4);munmap(image,0x400000);puts("DSI native mode geometry and logical-buffer replay passed");
    }
    puts("DSI native mode preparation rollback and admission passed");
}
