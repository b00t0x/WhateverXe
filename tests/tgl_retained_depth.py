#!/usr/bin/env python3
"""Exercise native full/partial selection and the production eDP register consumer."""
import ctypes
import hashlib
import mmap
import platform
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "WhateverXe/kern_xe.cpp").read_text()
wrapper = source[source.index("bool XE::wrapTGLRetainedMode"):source.index("// Successful base reads")]
# Only the native mode-change caller enters the mutation path.
harness = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include "kern_display13_handoff.hpp"
#include "kern_xelp_sst_route.hpp"
uintptr_t caller = 1;
uint32_t regs[3];
unsigned writes;
bool nativeUpdate;
unsigned currentScenario;
struct Root { void setProperty(const char *, void *, unsigned long) {} };
struct IORegistryEntry { static Root *getRegistryRoot() {static Root root; return &root;} };
struct XE {
    uintptr_t tglRetainedModeCaller = 1;
    void *tglObservationController, *tglPlatformCopy;
    bool tglDPSSTMode(unsigned ddi,unsigned mode) const {
        return XELP::Display12SSTRoutes{2,8,8,0}.external(ddi,mode).valid();
    }
    static bool prepareDisplay12RGBMode(void*,void*,uint32_t*,bool);
    bool (*orgTGLRetainedMode)(void*,void*,void*,void*,const void*,void*);
    uint32_t (*orgTGLRegisterRead)(void*,unsigned long);
    void (*orgTGLRegisterWrite)(void*,unsigned long,uint32_t);
    // This fixture starts after the bootstrap decision. Its physical-link
    // alternative is exercised separately with the original mode comparator.
    static bool selectTGLBootDepth(bool update,void*,void*,void*,void*,const void*,void*) {return update;}
    static bool wrapTGLRetainedMode(void*,void*,void*,void*,const void*,void*);
    static uint32_t tglPanelDepthPort(void *p) {return p && currentScenario<4 ? 1 : 9;}
} xe, *callbackXE = &xe;
#define FunctionCast(f,p) (p)
#define __builtin_return_address(n) caller
WRAPPER
static unsigned indexOf(unsigned long reg) {
    switch (reg) {
    case 0x62400: return 0;
    case 0x72030: return 1;
    case 0x62410: return 2;
    default: __builtin_trap();
    }
}
extern "C" unsigned replay(bool full, uint32_t liveLink, uint32_t requestedLink, uint32_t pipe,
                           uint32_t msa, uint32_t plannedMSA, uint32_t plannedPipe,
                           unsigned scenario, uint32_t *result) {
    alignas(8) uint8_t ctrl[0xe00]{}, fb[0x200]{}, path[0x3650]{}, port[0x600]{}, platform[0x200]{};
    uint32_t params[60]{};
    uint16_t timing[80]{};
    auto putPtr=[](uint8_t *out, void *ptr){memcpy(out,&ptr,sizeof(ptr));};
    putPtr(fb+0x1d0,ctrl); putPtr(ctrl+0xc40,ctrl);
    currentScenario=scenario;
    unsigned ddi=scenario==7 || scenario==8 ? 3:1;
    auto config=reinterpret_cast<uint32_t *>(platform+0x20+24*ddi);
    config[0]=ddi;config[2]=1;
    putPtr(path+0x3648,scenario==1 ? nullptr : port);
    putPtr(ctrl+0xd40,platform);xe.tglPlatformCopy=platform;putPtr(port+0x548,config);
    putPtr(port+0x578,scenario==6 ? nullptr : ctrl);
    *reinterpret_cast<uint32_t *>(port+0x120)=ddi;
    *reinterpret_cast<uint32_t *>(port+0x590)=0x64000+0x100*ddi;
    *reinterpret_cast<uint32_t *>(port+0x550)=scenario==7 ? 3:scenario==8 ? 4:1;
    path[0x3640]=2;
    timing[0x88/2]=scenario==2 ? 2 : 1;
    params[1]=requestedLink; params[3]=plannedMSA; params[12]=plannedPipe;
    xe.tglObservationController=ctrl; caller=scenario==3 ? 2 : 1;
    xe.orgTGLRetainedMode=[](void*,void*,void*,void*,const void*,void*){return nativeUpdate;};
    xe.orgTGLRegisterRead=[](void*,unsigned long reg){return regs[indexOf(reg)];};
    xe.orgTGLRegisterWrite=[](void*,unsigned long reg,uint32_t v){if(currentScenario!=5)regs[indexOf(reg)]=v; ++writes;};
    nativeUpdate=full; regs[0]=liveLink; regs[1]=pipe; regs[2]=msa; writes=0;
    const bool changedMode=XE::wrapTGLRetainedMode(ctrl,fb,path,params,timing,nullptr);
    assert(changedMode==(full || scenario==5 || scenario>=9 || liveLink==UINT32_MAX || msa==UINT32_MAX || pipe==UINT32_MAX));
    result[0]=regs[1]; result[1]=regs[2]; result[2]=params[3]; result[3]=params[12];
    return writes;
}
"""
with tempfile.TemporaryDirectory() as d:
    cpp, lib = Path(d)/"replay.cpp", Path(d)/"replay.dylib"
    cpp.write_text(harness.replace("WRAPPER", wrapper))
    subprocess.run(["clang++","-std=c++17","-I"+str(root/"WhateverXe"),"-shared","-fPIC",str(cpp),"-o",str(lib)],check=True)
    replay = ctypes.CDLL(str(lib)).replay
    word = ctypes.c_uint32
    replay.argtypes = [ctypes.c_bool] + [word]*7 + [ctypes.POINTER(word)]
    replay.restype = word
    compare = None
    if len(sys.argv) > 1:
        if platform.machine() != "x86_64":
            raise SystemExit("Native framebuffer replay requires an x86_64 Python process")
        image = Path(sys.argv[1]).read_bytes()
        assert hashlib.sha256(image).hexdigest() in {
            "c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731",
            "115a4616e862228a8c82df6e52ba41ac02b9750f116352ff3575aadcc950888e",
        }
        assert image[0x4fca1:0x4fcaa] == bytes.fromhex("e8 9e f3 ff ff 84 c0 74 3d")
        memory = mmap.mmap(-1,0x100000,prot=mmap.PROT_READ|mmap.PROT_WRITE)
        memory[0x4f1c0:0x4f2d5] = image[0x4f1c0:0x4f2d5]
        base = ctypes.addressof(ctypes.c_char.from_buffer(memory))
        assert ctypes.CDLL(None).mprotect(ctypes.c_void_p(base+0x4f000),0x1000,mmap.PROT_READ|mmap.PROT_EXEC)==0
        compare = ctypes.CFUNCTYPE(ctypes.c_bool,*[ctypes.c_void_p]*3)(base+0x4f1c0)
    for code, msaBits in [(2,0),(0,0x20),(1,0x40)]:
        active = 0x92000006 | code<<20  # DDI B, pipe C
        pipe = 0x01000010 | code<<5
        for other, partial in [(active,True),(active ^ (1<<20),False)]:
            for stale in [False,True]:
                actualMSA = 1 | (msaBits ^ (0x20 if stale else 0))
                plan = 1 | msaBits
                if compare:
                    ctrl, old, new = bytearray(0xf00), bytearray(0xf0), bytearray(0xf0)
                    for blob, link, msa in [(old,active,actualMSA),(new,other,plan)]:
                        struct.pack_into("<I",blob,4,link)
                        struct.pack_into("<I",blob,12,msa)
                        struct.pack_into("<I",blob,48,pipe)
                    def ptr(b):
                        return ctypes.addressof(ctypes.c_char.from_buffer(b))
                    assert compare(ptr(ctrl),ptr(new),ptr(old)) == partial
                for scenario in range(4):
                    result = (word*4)()
                    changed = partial and stale and scenario==0
                    count = replay(not partial,active,other,pipe,actualMSA,plan,pipe,scenario,result)
                    assert count==int(changed)
                    # Toggling the low depth bit also covers a genuine
                    # 6-to-12 bpc full request; live values must stay intact.
                    twelveBitFull=not partial and ((other>>20)&7)==3 and scenario==0
                    expectedPlan=0x61 if twelveBitFull else plan
                    assert list(result)[:3]==[pipe,plan if changed else actualMSA,expectedPlan]
                    assert result[3]==(pipe&~0xf0 if twelveBitFull else pipe)
        # Future setColorMode consumes the copied CRTCParams, including when
        # only the planned notification is stale and hardware is coherent.
        result = (word*4)()
        assert replay(False,active,active,pipe,1|msaBits,1|(msaBits^0x20),pipe,0,result)==0
        assert list(result)[:3]==[pipe,1|msaBits,1|msaBits]
        wrong = (pipe & ~0xe0) | ((code+1)%3)<<5
        result = (word*4)()
        assert replay(False,active,active,wrong,1|msaBits,1|msaBits,pipe,0,result)==1
        assert list(result)[:3]==[pipe,1|msaBits,1|msaBits]
        assert replay(False,active,active,wrong,0x4001,0x4001,pipe,0,result)==0
        assert list(result)[:3]==[wrong,0x4001,0x4001]
    # Live link, requested link and intended/native depth are held constant.
    # Fixed Combo B, fixed Dekel and DP-alt all use their physical SST port.
    for scenario, ddi in [(4,1),(5,1),(6,1),(7,3),(8,3),(9,1)]:
        link=0x82000006 | ((ddi+1)<<27)
        result=(word*4)()
        initial=0xffffffff if scenario==9 else 1
        replay(False,link,link,0x01000010,initial,1,0x01000010,scenario,result)
        assert result[1]==(initial if scenario in (5,6,9) else 0x21)
        assert result[2]==(1 if scenario==6 else 0x21)
    for live,pipe,msa in [(0xffffffff,0,1),(0x92000006,0xffffffff,1),(0x92000006,0,0xffffffff)]:
        result=(word*4)()
        replay(False,live,0x92000006,pipe,msa,1,0,10,result)
        assert list(result)[:3]==[pipe,msa,0x21]
    # Display12 can carry a 12-bpc stream without a 12-bpc dither encoding.
    for scenario in [0,4]:
        link=0x92300006; result=(word*4)()
        replay(False,link,link,0x01000000,1,1,0x01000000,scenario,result)
        assert list(result)[:3]==[0x01000000,0x61,0x61]
    # SetupParams produces 0x800070 on a 12-bpc SDR request. Display12
    # dithering and transport notification have different owners.
    link=0x92330006; pipe=0x800070
    for scenario in [0,4]:
        full=False
        if compare:
            old,new=(ctypes.create_string_buffer(0xf0) for _ in range(2))
            ctrl=ctypes.create_string_buffer(0x1200)
            for out,notification in [(old,1),(new,0x61)]:
                struct.pack_into('<I',out,4,link)
                struct.pack_into('<I',out,12,notification)
                struct.pack_into('<I',out,48,pipe)
            full=not compare(ctrl,new,old)
            assert not full
        for live,activePipe,msa in [(link,pipe,1),(0xffffffff,pipe,1),(link,0xffffffff,1),(link,pipe,0xffffffff)]:
            result=(word*4)()
            writes=replay(full,live,link,activePipe,msa,1,pipe,scenario,result)
            valid=0xffffffff not in (live,activePipe,msa)
            assert writes==2*int(valid)
            assert list(result)==[0x800000 if valid else activePipe,0x61 if valid else msa,0x61,0x800000]
        # A full modeset programs its copied plan. The next original request
        # must compare against that same normalized PIPE_MISC value.
        result=(word*4)()
        assert replay(True,link,link,pipe,1,0x61,pipe,scenario,result)==0
        assert result[3]==0x800000 and result[2]==0x61
        assert replay(False,link,link,result[3],result[2],0x61,pipe,scenario,result)==0
        assert list(result)==[0x800000,0x61,0x61,0x800000]
    if compare:
        # Captured 6 bpc firmware and 8 bpc request on TL156MDMP01. The donor
        # requests a complete modeset even with every other field equal.
        ctrl, old, new = (ctypes.create_string_buffer(n) for n in (0xf00,0xf0,0xf0))
        struct.pack_into("<I",old,4,0x8a230006)
        struct.pack_into("<I",new,4,0x8a030006)
        full = not compare(ctrl,new,old)
        assert full
        result = (word*4)()
        assert replay(full,0x8a230006,0x8a030006,0x01000050,1,0x21,0x01800010,0,result)==0
        assert list(result)[:3]==[0x01000050,1,0x21]
print("Checked retained eDP depth, unchanged full transitions and register targets")
