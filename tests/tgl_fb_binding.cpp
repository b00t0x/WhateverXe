#include "kern_tgl_fb_binding.hpp"
#include <cassert>
#include <cstdio>
using namespace TGLFBBinding;
template<class T> void put(void *p,size_t off,T value) {memcpy(static_cast<uint8_t *>(p)+off,&value,sizeof(value));}
struct Fixture {
    alignas(void*) uint8_t ctl[0x1C20] {},fb[0x4B90] {},path[0x3730] {},plane[0x1C8] {},port[0x580] {},access[0x68] {};
    alignas(void*) uint8_t platform[Display13FBTopology::Bytes] {},donor[Display13FBTopology::Bytes] {};
    Display13OutputWiring wiring;
    Fixture(unsigned fbIndex,unsigned pipe,unsigned ddi) {
        const uint32_t property[] {1,7,1,2,3};wiring=TGLHDMIWiring::fromProperty(property,sizeof(property));
        donor[0x12]=9;const uint32_t panel[] {0,0,0,0,2,0x18};memcpy(donor+0x20,panel,24);
        for(unsigned d=1;d<9;++d) {const uint32_t record[] {d,d+1,1,0,0x400,0x201};memcpy(donor+0x20+24*d,record,24);}
        assert(Display13FBTopology::apply(platform,sizeof(platform),donor,wiring));
        put(fb,0x1D0,static_cast<const void *>(ctl));put(fb,0x1DC,fbIndex);put(ctl,0xD5C,uint32_t(3));
        put(ctl,0xD60+8*fbIndex,static_cast<const void *>(fb));put(ctl,0xC40,static_cast<const void *>(access));put(fb,0x4A40,static_cast<const void *>(access));
        put(path,0x32B0,static_cast<const void *>(fb));put(path,0x32C8,static_cast<const void *>(plane));
        put(path,0x3640,pipe);put(path,0x3644,uint32_t(2));put(path,0x3648,static_cast<const void *>(port));
        put(port,0x120,ddi);put(port,0x548,static_cast<const void *>(platform+0x20+24*ddi));
        put(plane,0x68,static_cast<const void *>(fb));put(plane,0x70,static_cast<const void *>(path));put(plane,0x90,static_cast<const void *>(access));
        put(plane,0x78,pipe);put(plane,0x7C,pipe*5);put(plane,0x80,uint32_t(0));put(ctl,0xF58+8*(pipe*5),static_cast<const void *>(plane));
        put(plane,0x98,uint32_t(0x70180+pipe*0x1000));put(plane,0xA0,uint32_t(0x7027C+pipe*0x1000));put(plane,0xA4,uint32_t(0x7017C+pipe*0x1000));
        put(fb,0x3FD8,int32_t(-1));
    }
    Objects objects(uintptr_t owner=0) const {return {{ctl,sizeof(ctl)},{fb,sizeof(fb)},{path,sizeof(path)},{plane,sizeof(plane)},{port,sizeof(port)},owner};}
    Readout inspect(bool attached=false,uintptr_t owner=0) const {return TGLFBBinding::inspect(objects(owner),platform,wiring,attached);}
    void attach() {put(fb,0x49E8,uint32_t(1));put(fb,0x49F0,static_cast<const void *>(path));}
};
int main() {
    // Derive the release masks from validated attached paths. Here physical
    // DDI B is pipe A/FB2, while physical DDI A is pipe B/FB0.
    {Fixture a(2,0,1),b(0,1,0);a.attach();b.attach();PublishedPath paths[3];
     uintptr_t owners[3] {};auto map=powerPipes(paths,owners,3);
     assert(map.valid && !map.known && map.mask[0]==7 && map.mask[1]==7);
     owners[0]=reinterpret_cast<uintptr_t>(a.fb);owners[1]=reinterpret_cast<uintptr_t>(b.fb);
     assert(publish(paths[0],owners[0],reinterpret_cast<uintptr_t>(a.path),a.inspect(true),0));
     assert(publish(paths[1],owners[1],reinterpret_cast<uintptr_t>(b.path),b.inspect(true),1));
     map=powerPipes(paths,owners,3);assert(map.valid && map.known==3 && map.mask[0]==2 && map.mask[1]==1);
     const auto saved=paths[0];
     assert(publish(paths[0],owners[0],reinterpret_cast<uintptr_t>(a.path),a.inspect(true),0));
     assert(!publish(paths[0],owners[0],reinterpret_cast<uintptr_t>(b.path),a.inspect(true),0));
     assert(!publish(paths[0],owners[1],reinterpret_cast<uintptr_t>(a.path),a.inspect(true),0));
     assert(!publish(paths[0],owners[0],reinterpret_cast<uintptr_t>(a.path),a.inspect(true),1));
     auto changed=a.inspect(true);changed.ddi=0;
     assert(!publish(paths[0],owners[0],reinterpret_cast<uintptr_t>(a.path),changed,0));
     assert(paths[0].path==saved.path && paths[0].binding.ddi==saved.binding.ddi);
     owners[0]^=1;map=powerPipes(paths,owners,3);assert(!map.valid && map.mask[0]==7 && map.mask[1]==7);owners[0]^=1;
     assert(!powerPipes(paths,owners,1).valid && !powerPipes(paths,owners,0).valid);
     paths[1].binding.ddi=1;assert(!powerPipes(paths,owners,3).valid);paths[1].binding.ddi=0;
     paths[1].binding.framebuffer=2;assert(!powerPipes(paths,owners,3).valid);paths[1].binding.framebuffer=0;
     owners[1]=owners[0];paths[1].framebuffer=owners[0];assert(!powerPipes(paths,owners,3).valid);
     owners[1]=reinterpret_cast<uintptr_t>(b.fb);paths[1].framebuffer=owners[1];
     // Keep a releasing path's map through native puts, then withdraw it
     // with its owner. A dangling map is never silently treated as detached.
     owners[0]=0;assert(!powerPipes(paths,owners,3).valid);paths[0]={};
     map=powerPipes(paths,owners,3);assert(map.valid && map.known==1 && map.mask[0]==2 && map.mask[1]==7);
    }
    // Every distinct physical-port/pipe permutation: all three native identities,
    // no guessed port==pipe relation, exact power maps and independent withdrawal.
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)for(unsigned c=0;c<3;++c) {
        if(a==b || a==c || b==c)continue;
        Fixture x(2,0,a),y(0,1,b),z(1,2,c);x.attach();y.attach();z.attach();
        Fixture *f[] {&x,&y,&z};PublishedPath paths[3];uintptr_t owners[3] {};
        for(unsigned p=0;p<3;++p) {
            owners[p]=reinterpret_cast<uintptr_t>(f[p]->fb);
            assert(publish(paths[p],owners[p],reinterpret_cast<uintptr_t>(f[p]->path),f[p]->inspect(true),p));
        }
        auto m=powerPipes(paths,owners,7);assert(m.valid && m.known==7);
        for(unsigned p=0;p<3;++p)assert(m.mask[f[p]->inspect().ddi]==(1U<<p));
        for(unsigned p=0;p<3;++p) {
            const auto saved=paths[p];const auto owner=owners[p];paths[p]={};owners[p]=0;
            m=powerPipes(paths,owners,7);assert(m.valid && m.known==(7U&~(1U<<f[p]->inspect().ddi)));
            assert(m.mask[f[p]->inspect().ddi]==7);paths[p]=saved;owners[p]=owner;
        }
    }
    // Mixed HDMI/DP topology feeds the same native binding and withdrawal
    // map. Change protocol, FB index and pipe independently of physical DDI.
    for(unsigned dp=1;dp<4;++dp)for(unsigned pipe=0;pipe<3;++pipe)for(unsigned fb=0;fb<3;++fb) {
        for(unsigned ddi=0;ddi<2;++ddi) {
            Fixture x(fb,pipe,ddi);x.wiring.hdmi=3&~dp;x.wiring.dp=dp;
            for(unsigned d=0;d<3;++d)x.wiring.ddc[d]=(x.wiring.hdmi&(1U<<d))?d+1:0;
            assert(Display13FBTopology::apply(x.platform,sizeof(x.platform),x.donor,x.wiring));x.attach();
            const auto r=x.inspect(true);assert(r.status==Status::Complete&&r.ddi==ddi&&r.pipe==pipe&&r.framebuffer==fb);
            PublishedPath paths[3];uintptr_t owners[3]{};owners[pipe]=reinterpret_cast<uintptr_t>(x.fb);
            assert(publish(paths[pipe],owners[pipe],reinterpret_cast<uintptr_t>(x.path),r,pipe));
            auto m=powerPipes(paths,owners,x.wiring.selected());assert(m.valid&&m.known==(1U<<ddi)&&m.mask[ddi]==(1U<<pipe));
            paths[pipe]={};owners[pipe]=0;m=powerPipes(paths,owners,x.wiring.selected());assert(m.valid&&!m.known&&m.mask[ddi]==7);
        }
    }
    // FB index, display pipe, physical DDI and aperture slot need not agree.
    for(unsigned f=0;f<3;++f)for(unsigned p=0;p<3;++p)for(unsigned d=0;d<3;++d) {
        Fixture x(f,p,d);const auto r=x.inspect();assert(r.status==Status::Complete && r.framebuffer==f && r.pipe==p && r.ddi==d && r.plane==5*p);
        assert(x.inspect(true).status==Status::Path);x.attach();assert(x.inspect(true).status==Status::Complete);
        put(x.fb,0x4288,uint8_t((f+1)%3));assert(x.inspect(true).status==Status::Complete); // Slot is not the path identity.
        put(x.fb,0x3FD8,int32_t(0));put(x.fb,0x4A00,uint32_t(1));put(x.fb,0x4A08,static_cast<const void *>(x.path));
        assert(x.inspect(true).status==Status::Complete);put(x.fb,0x4A00,uint32_t(2));assert(x.inspect(true).status==Status::Path);
    }
    {Fixture x(1,0,1);x.attach();put(x.path,0x3640,uint32_t(3));assert(x.inspect().status==Status::Path);}
    {Fixture x(1,0,1);put(x.path,0x32B0,static_cast<const void *>(x.path));assert(x.inspect().status==Status::Path);}
    {Fixture x(1,0,1);put(x.path,0x32C8,static_cast<const void *>(x.port));assert(x.inspect().status==Status::Path);}
    {Fixture x(1,0,1);put(x.port,0x548,reinterpret_cast<const void *>(1));assert(x.inspect().status==Status::Port);}
    {Fixture x(1,0,1);put(x.port,0x120,uint32_t(0));assert(x.inspect().status==Status::Port);}
    {Fixture x(2,1,0);put(x.ctl,0xF58+8*5,static_cast<const void *>(x.port));assert(x.inspect().status==Status::Plane);}
    {Fixture x(2,1,0);put(x.ctl,0xF58+8*5,static_cast<const void *>(nullptr));put(x.ctl,0xF58+8*13,static_cast<const void *>(x.plane));
     assert(x.inspect().status==Status::Complete);put(x.ctl,0xF58+8*2,static_cast<const void *>(x.plane));assert(x.inspect().status==Status::Plane);}
    {Fixture x(2,1,0);put(x.plane,0x7C,UINT32_MAX);assert(x.inspect().status==Status::Plane);} // No out-of-bounds table read.
    {Fixture x(2,1,0);put(x.plane,0x78,uint32_t(0));assert(x.inspect().status==Status::Plane);}
    {Fixture x(2,1,0);put(x.plane,0x80,uint32_t(1));assert(x.inspect().status==Status::Plane);}
    {Fixture x(2,1,0);put(x.plane,0x98,uint32_t(0x70180));assert(x.inspect().status==Status::Plane);}
    {Fixture x(2,1,0);put(x.plane,0xA4,uint32_t(0x7017C));assert(x.inspect().status==Status::Plane);}
    {Fixture x(2,1,0);put(x.plane,0x70,static_cast<const void *>(x.ctl));assert(x.inspect().status==Status::Plane);}
    {Fixture x(2,1,0);put(x.fb,0x4A40,static_cast<const void *>(x.path));assert(x.inspect().status==Status::Plane);}
    {Fixture x(2,1,0);put(x.ctl,0xD60+8*2,static_cast<const void *>(x.plane));assert(x.inspect().status==Status::Association);}
    {Fixture x(2,1,0);put(x.ctl,0xC0,uintptr_t(0x123400));put(x.fb,0x4348,uintptr_t(0x41300000));
     assert(x.inspect().status==Status::Association && x.inspect(false,0x123400).status==Status::Complete);}
    // A rotation surface is valid only with the separately retained handle
    // and the matching accelerator owner. Non-DSI callers retain zero.
    {Fixture x(2,1,0);x.attach();put(x.ctl,0xC0,uintptr_t(0x123400));
     put(x.fb,0x4348,uintptr_t(0x41300000));put(x.fb,0x4340,uintptr_t(0x41400000));
     auto o=x.objects(0x123400);
     const auto inspect=[&]{return TGLFBBinding::inspect(o,x.platform,x.wiring,true).status;};
     assert(inspect()==Status::Association);
     o.logicalSurface=0x41400000;assert(inspect()==Status::Complete);
     o.logicalSurface^=8;assert(inspect()==Status::Association);o.logicalSurface^=8;
     o.accelerator^=8;assert(inspect()==Status::Association);o.accelerator^=8;
     put(x.ctl,0xC0,uintptr_t(0));o.accelerator=0;assert(inspect()==Status::Association);
    }
    {Fixture x(2,1,0);auto objects=x.objects();objects.plane.bytes=0x1C7;assert(TGLFBBinding::inspect(objects,x.platform,x.wiring).status==Status::Unavailable);}
    puts("PASS native FB binding: independent FB/pipe/DDI/slot identities, attached versus pending paths, native primary table/registers, foreign config and malformed owner rejection. CPU only.");
}
