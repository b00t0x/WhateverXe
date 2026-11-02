#include "kern_dsi_apple_timing.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
using namespace XELP::DSI;
struct Object {void **vtable {};unsigned refs {1};bool data {};};
struct Data : Object {std::vector<uint8_t> bytes;};
struct Array : Object {Data *entry {};};
static Array *property=nullptr;
static bool publishOK=true;
void retain(Object *p){assert(p&&p->refs);++p->refs;}
void release(Object *p){assert(p&&p->refs);--p->refs;}
bool setProperty(void *,const char *key,Array *array) {
    assert(!strcmp(key,"IOFBDetailedTimings"));if(!publishOK)return false;
    retain(array);if(property)release(property);property=array;return true;
}
void removeProperty(void *,const char *key) {
    assert(!strcmp(key,"IOFBDetailedTimings"));if(property)release(property);property=nullptr;
}
void *getObject(Array *a,uint32_t index){assert(a->refs);return index==0?a->entry:nullptr;}
void *getBytes(Data *d){assert(d->refs);return d->bytes.data();}
void *cast(Object *o,const void *){return o&&o->data?o:nullptr;}
template<class T>void put(void *p,size_t offset,T value){memcpy(static_cast<uint8_t *>(p)+offset,&value,sizeof(value));}
void jump(uint8_t *p,uintptr_t target){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&target,8);p[10]=0xff;p[11]=0xe0;}
int main(int argc,char **argv) {
    assert(argc==2);std::ifstream f(argv[1],std::ios::binary);assert(f);
    std::vector<uint8_t> binary{std::istreambuf_iterator<char>(f),{}};
    constexpr size_t size=4*1024*1024;assert(binary.size()<size);
    auto image=static_cast<uint8_t *>(mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(image!=MAP_FAILED);memcpy(image,binary.data(),binary.size());
    // Redirect only the imported meta-cast leaf, retaining all donor branches.
    jump(image+0x300000,reinterpret_cast<uintptr_t>(cast));
    int32_t relative=0x300000-0x30e70;put(image,0x30e6c,relative);
    void *meta=nullptr;put(image,0xc8050,&meta);
    auto publish=reinterpret_cast<uint32_t(*)(void *,Array *)>(image+0x30d0e);
    auto validate=reinterpret_cast<uint32_t(*)(void *,int32_t,void **,const void **)>(image+0x30de2);
    void *fbTable[0x2a8/8] {},*arrayTable[0x1f0/8] {},*dataTable[0x180/8] {};
    fbTable[0x268/8]=reinterpret_cast<void *>(setProperty);fbTable[0x2a0/8]=reinterpret_cast<void *>(removeProperty);
    arrayTable[0x1e8/8]=reinterpret_cast<void *>(getObject);dataTable[0x178/8]=reinterpret_cast<void *>(getBytes);
    alignas(8) uint8_t fb[0x4300] {};put(fb,0,fbTable+0);fb[0x1e0]=1;
    Profile p;p.status=ProfileStatus::Valid;p.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
    AppleAdapter::Timing timing;assert(AppleAdapter::makeTiming(p,timing));
    Data data;data.vtable=dataTable;data.data=true;
    const auto bytes=reinterpret_cast<const uint8_t *>(&timing);data.bytes.assign(bytes,bytes+sizeof(timing));
    Array array;array.vtable=arrayTable;array.entry=&data;retain(&data); // array retains OSData
    assert(!publish(fb,&array)&&property==&array&&array.refs==2);
    release(&array);release(&data); // only property/array now retain the data
    const void *result=nullptr;void *description=nullptr;
    assert(!validate(fb,AppleAdapter::FixedModeID,&description,&result)&&description&&result==data.bytes.data());
    assert(!memcmp(result,&timing,sizeof(timing))&&array.refs==1&&data.refs==1);
    assert(validate(fb,0x10001,nullptr,&result)==0xe00002c2U);
    assert(validate(fb,0xffff,nullptr,&result)==0xe00002c2U);
    data.data=false;assert(validate(fb,0x10000,nullptr,&result)==0xe00002c2U);data.data=true;
    // Native mode indexing aliases high bits; the DSI mode dispatcher must
    // separately constrain the selected fixed mode rather than relying on it.
    assert(!validate(fb,0x10400,nullptr,&result));
    // Native validation does not check OSData length. Publication must always
    // carry the complete structure; a type-correct short blob is insufficient.
    data.bytes.resize(1);assert(!validate(fb,0x10000,nullptr,&result));
    assert(!publish(fb,nullptr)&&!property&&!array.refs);
    assert(validate(fb,0x10000,nullptr,&result)==0xe00002c2U);release(&data);
    // Native publication ignores setProperty failure and still stores a raw
    // array pointer. The eventual adapter must retain its own backing owner.
    Array held;held.vtable=arrayTable;held.entry=nullptr;publishOK=false;
    assert(!publish(fb,&held)&&!property&&held.refs==1);
    Array *raw=nullptr;memcpy(&raw,fb+0x4260,sizeof(raw));assert(raw==&held);
    assert(!publish(fb,nullptr));release(&held);
    munmap(image,size);
    puts("PASS exact donor timing: retained array/data, type rejection, index alias, short-data and property-failure limits");
}
