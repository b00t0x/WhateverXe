// Exact c004fc1a native FB/path/primary-plane/physical-port CPU association.
// No MMIO, new ownership, slot allocation or hardware-success substitution.
#ifndef KERN_TGL_FB_BINDING_HPP
#define KERN_TGL_FB_BINDING_HPP
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_fb_topology.hpp"
namespace TGLFBBinding {
struct Object {const void *pointer {};size_t bytes {};};
struct Objects {Object controller,framebuffer,path,plane,port;uintptr_t accelerator {},logicalSurface {};};
enum class Status : uint32_t {Unavailable,Association,Path,Port,Plane,Complete};
struct Readout {
    Status status {Status::Unavailable};
    uint32_t framebuffer {UINT32_MAX},pipe {UINT32_MAX},ddi {UINT32_MAX},plane {UINT32_MAX};
};
// Copied only after inspect() succeeds under the native framebuffer mutex.
// These addresses are identity keys, never pointers to dereference. The owner
// lock protects this snapshot through native putPG/putDDI and path withdrawal.
struct PublishedPath {
    uintptr_t framebuffer {},path {};
    Readout binding;
    bool empty() const {return !framebuffer && !path && binding.status==Status::Unavailable;}
};
inline bool publish(PublishedPath &p,uintptr_t framebuffer,uintptr_t path,const Readout &r,unsigned pipe) {
    if(!framebuffer || !path || r.status!=Status::Complete || pipe>=3 || r.pipe!=pipe ||
       r.framebuffer>=3 || r.ddi>=7 || r.plane!=5*pipe)return false;
    if(!p.empty() && (p.framebuffer!=framebuffer || p.path!=path || p.binding.status!=Status::Complete ||
       p.binding.framebuffer!=r.framebuffer || p.binding.pipe!=r.pipe || p.binding.ddi!=r.ddi || p.binding.plane!=r.plane))return false;
    p.framebuffer=framebuffer;p.path=path;p.binding=r;return true;
}
struct PortPipes {
    bool valid {};
    uint32_t known {},mask[7] {7,7,7,7,7,7,7}; // Unbound/unknown never means no consumers.
};
inline PortPipes powerPipes(const PublishedPath (&paths)[3],const uintptr_t (&owners)[3],uint32_t selectedPorts) {
    PortPipes r;
    if(!selectedPorts || (selectedPorts&~0x7FU))return r;
    uint32_t fbMask=0;
    for(unsigned pipe=0;pipe<3;++pipe) {
        const auto &p=paths[pipe];
        if(!owners[pipe]) {if(!p.empty())return {};continue;}
        const auto &b=p.binding;
        if(p.framebuffer!=owners[pipe] || !p.path || b.status!=Status::Complete || b.framebuffer>=3 ||
           b.pipe!=pipe || b.ddi>=7 || b.plane!=5*pipe || !(selectedPorts&(1U<<b.ddi)) ||
           (r.known&(1U<<b.ddi)) || (fbMask&(1U<<b.framebuffer)))return {};
        for(unsigned other=0;other<pipe;++other)if(owners[other]==owners[pipe])return {};
        r.mask[b.ddi]=1U<<pipe;r.known|=1U<<b.ddi;fbMask|=1U<<b.framebuffer;
    }
    r.valid=true;return r;
}
// Controller/FB private arrays are donor-owned and the kernel caller checks
// the actual OSObject classes/sizes. Supplied objects are never discovered by
// following arbitrary pointers in this helper. A config must be bound before
// its record bytes can be inspected.
inline Readout inspect(const Objects &o,const XELP::NativePorts &ports,bool attached=false) {
    Readout r;
    using Display13FBMemory::field;
    const auto c=o.controller.pointer,fb=o.framebuffer.pointer,path=o.path.pointer,
        plane=o.plane.pointer,port=o.port.pointer;
    if(!c || !fb || !path || !plane || !port || o.controller.bytes<0x1C20 || o.framebuffer.bytes<0x4B90 ||
       o.path.bytes<0x3730 || o.plane.bytes<0x1C8 || o.port.bytes<0x580)return r;
    if(!Display13FBMemory::associations(c,o.controller.bytes,fb,o.framebuffer.bytes,o.accelerator,true,o.logicalSurface)) {
        r.status=Status::Association;return r;
    }
    r.framebuffer=field<uint32_t>(fb,0x1DC);r.pipe=field<uint32_t>(path,0x3640);
    if(r.pipe>=3 || field<const void *>(path,0x32B0)!=fb || field<const void *>(path,0x32C8)!=plane ||
       field<const void *>(path,0x3648)!=port || (attached && Display13FBMemory::singlePath<const void *>(fb)!=path)) {
        r.status=Status::Path;return r;
    }
    r.ddi=field<uint32_t>(port,0x120);
    const auto config=field<const void *>(port,0x548);
    if(!ports.bound(config,r.ddi) || field<uint32_t>(config,0)!=r.ddi) {
        r.status=Status::Port;return r;
    }
    const auto access=field<const void *>(c,0xC40);r.plane=field<uint32_t>(plane,0x7C);
    // Native getPlane scans these 15 pointers and matches pipe/local ID;
    // their array position is not itself a plane identity.
    unsigned matches=0;for(unsigned i=0;i<15;++i)if(field<const void *>(c,0xF58+8*i)==plane)++matches;
    const auto bank=0x1000*r.pipe;
    // Plane init computes pipe=ID/5, local plane=ID%5. This backend uses the
    // RGB primary (local0) on each pipe, with that pipe's separate cursor.
    if(!access || field<const void *>(fb,0x4A40)!=access || r.plane!=5*r.pipe ||
       matches!=1 || field<const void *>(plane,0x68)!=fb ||
       field<const void *>(plane,0x70)!=path || field<const void *>(plane,0x90)!=access ||
       field<uint32_t>(plane,0x78)!=r.pipe || field<uint32_t>(plane,0x80)!=0 ||
       field<uint32_t>(plane,0x98)!=0x70180+bank || field<uint32_t>(plane,0xA0)!=0x7027C+bank ||
       field<uint32_t>(plane,0xA4)!=0x7017C+bank) {
        r.status=Status::Plane;return r;
    }
    r.status=Status::Complete;return r;
}
// Transport qualification precedes the shared native object binding.
inline Readout inspect(const Objects &o,const void *platform,const Display13OutputWiring &w,bool attached=false) {
    return inspect(o,Display13FBTopology::boundPorts(platform,w),attached);
}
}
#endif
