"""Replay TGL GT2 and ADL-P fuse admission through the native media rings."""
import ctypes as C
import hashlib
import mmap
from pathlib import Path
import re
import subprocess
import tempfile
import struct
import sys

root = Path(__file__).resolve().parents[1]
original = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(original).hexdigest() in {
    'bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0',
    'b4893d74f6a52ba01bcf7c18b019445c61f39b754a7cfab62d9d97ff5b5338a0',
}
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
patch_body = source[source.index('bool videoRoutingReady ='):source.index('patcher.clearError();',source.index('replace(mediaContext'))]
adl_readout = re.search(r'auto gt=AlderLakeN::readGT\(io,barReady && xelpMemoryMatches\(pci\),[^;]+;',source)[0]
tgl_readout = re.search(r'auto gt=AlderLakeN::readGT\(io,true,&edram,tglGT2Detected[^;]+;',source)[0]
tgl_admission = source[source.index('const bool readoutReady='):source.index('physicalGPU->setProperty("TGLGT2Qualified"')] + '}'
adl_selection = re.search(r'mobilePrimaryVCS2 = renderReady[^;]+;',source)[0]
# Stand in for Lilu's bounded byte replacement only. Compile the production
# fuse selection, all preconditions, patch sites and post-write checks.
gt_fixture = (root/'tests/alderlake_n.cpp').read_text()
io = gt_fixture.split('struct LinearIO',1)[0] + 'struct GTIO' + gt_fixture.split('struct GTIO',1)[1].split('static LinearIO firmware()',1)[0]
render_gate = next(line.strip() for line in source.splitlines() if 'renderReady=gt.status==' in line)
boot_gate = next(line.strip() for line in source.splitlines() if 'adlnNativeReady=renderReady' in line)
adapter = r"""
#include "kern_alderlake_n.hpp"
#include <cstring>
GT_IO
struct KernelPatcher {
    enum class Error { NoError, Failed };
    struct LookupPatch { void *owner; const uint8_t *from,*to; size_t length,count; };
    Error error = Error::NoError;
    void clearError() { error=Error::NoError; }
    Error getError() const { return error; }
    void applyLookupPatch(LookupPatch *p,uint8_t *site,size_t span) {
        if(span<=p->length || memcmp(site,p->from,p->length)) error=Error::Failed;
        else memcpy(site,p->to,p->length);
    }
};
extern "C" bool install(uintptr_t address,size_t size,bool tgl,uint32_t mediaFuse,bool urbReady) {
    auto io = firmwareGT();
    io.regs[0x913c]=tgl ? 7 : 15;
    io.regs[0x9134]=0;
    io.regs[0x9118]=0xc0;
    io.regs[0x9140]=mediaFuse;
    uint32_t edram=UINT32_MAX;
    bool mobilePrimaryVCS2=false;
    if(tgl) {
        bool tglGT2Detected=true,tglGT2Ready=false,display12SmallGTDetected=false,display12SmallGTReady=false;
        XELP::GTResources display12SmallGT,tglGT2Resources;
        TGL_READOUT
        TGL_ADMISSION
        if(!tglGT2Ready) return false;
        assert(tglGT2Resources.eus==48 && tglGT2Resources.l3Banks==6);
        assert(gt.fuses[4]==mediaFuse && gt.releasedMask==3 && gt.resources.urbKiB==384);
    } else {
        bool barReady=true,adlnNativeRequested=true,adlpRenderRequested=true;
        bool renderReady=false,display13BootReady=true,adlnNativeReady=false;
        auto pci=nullptr;
        auto xelpMemoryMatches=[](void *) { return true; };
        ADL_READOUT
        RENDER_GATE
        ADL_SELECT
        BOOT_GATE
        if(!adlnNativeReady) return false;
        assert(gt.fuses[4]==mediaFuse && gt.releasedMask==3 && gt.resources.eus==64 && gt.resources.urbKiB==384);
    }
    void *currentGraphics=nullptr;
    KernelPatcher patcher;
    PATCH
    return videoRoutingReady;
}
"""
for key, body in {
    'GT_IO':io, 'TGL_READOUT':tgl_readout, 'TGL_ADMISSION':tgl_admission,
    'ADL_READOUT':adl_readout,
    'RENDER_GATE':render_gate, 'ADL_SELECT':adl_selection, 'BOOT_GATE':boot_gate, 'PATCH':patch_body,
}.items():
    adapter = adapter.replace(key, body)
tmp = tempfile.TemporaryDirectory()
directory = Path(tmp.name)
(directory/'adapter.cpp').write_text(adapter)
subprocess.run(['clang++','-arch','x86_64','-std=c++17','-dynamiclib','-I'+str(root/'WhateverXe'),
                str(directory/'adapter.cpp'),'-o',str(directory/'adapter.dylib')],check=True)
install = C.CDLL(str(directory/'adapter.dylib')).install
install.argtypes = [C.c_size_t,C.c_size_t,C.c_bool,C.c_uint32,C.c_bool]
install.restype = C.c_bool
registers = C.c_uint64 * (0x200000 // 8)
accel = C.create_string_buffer(0x2000)
streamer = C.create_string_buffer(0x100)
frame = C.create_string_buffer(0x100)
context = C.create_string_buffer(0x100)
bar = registers()
struct.pack_into('<Q', accel, 0x1240, C.addressof(bar))
struct.pack_into('<Q', streamer, 0x10, C.addressof(accel))
sentinel = 0x123456789abcdef0
struct.pack_into('<Q', frame, 0x40, sentinel)
args = C.CFUNCTYPE(C.c_uint64, C.c_void_p, C.c_uint64, C.c_void_p)


def replay(tgl, mask):
    image = mmap.mmap(-1, len(original), prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
    image[:] = original
    base = C.addressof(C.c_char.from_buffer(image))
    fuse = 0xe00ff ^ mask
    for invalid in (0xe00ff, 0xe00fd, 0xe00f7, 0xe00f3, 0xf00fb, 0xffffffff):
        assert not install(base,len(image),tgl,invalid,True) and image[:] == original
    if mask == 4:
        # A changed donor at any site must prevent admission before the first write.
        assert not install(base,len(image),tgl,fuse,False) and image[:] == original
        for offset in (0x25c8e,0xb09b0,0xbe8a0):
            image[offset] ^= 1
            damaged = image[:]
            assert not install(base,len(image),tgl,fuse,True) and image[:] == damaged
            image[offset] ^= 1
    assert install(base,len(image),tgl,fuse,True)
    if mask != 4:
        assert image[:] == original

    def run(start, stops, prefix, result, arg1=0, arg2=0, arg3=0):
        # Save ABI registers touched by the original block and fixture.
        save = bytes.fromhex('55 53 41 55 41 57')
        restore = bytes.fromhex('41 5f 41 5d 5b 5d c3')
        begin = start - len(save) - len(bytes.fromhex(prefix))
        image[begin:start] = save + bytes.fromhex(prefix)
        for stop in stops:
            tail = bytes.fromhex(result) + restore
            image[stop:stop+len(tail)] = tail
        return args(base + begin)(arg1, arg2, arg3)

    # Read 0x9140, invert the physical disable bits and apply the donor filter.
    # ForceWake and logging frame the block in getGPUInfo and are modeled here.
    struct.pack_into('<I', bar, 0x9140, fuse)
    enabled = run(0x284f7,[0x2850b],'48 89 f8 41 56','44 89 e8 41 5e',C.addressof(bar))
    assert enabled == mask | 0x10000
    # R15=accelerator, R13D=the real result of the donor's fuse read.
    struct.pack_into('<Q', accel, 0x1300, 0x3f)
    run(0x28591, [0x28605], '49 89 ff 49 89 f5', '', C.addressof(accel), enabled)
    present, = struct.unpack_from('<Q', accel, 0x1300)
    assert present == {1:0x2f,4:0x37,5:0x3f}[mask]
    # Startup tests the enumerated engine and copies the system context template.
    # Give the donor its normal RBP scratch and an empty accelerator context slot.
    struct.pack_into('<Q', frame, 0x40, 0)
    run(0x25c8e,[0x25cbf,0x25ce0],'49 89 fd 48 89 d5','',
        C.addressof(accel),0,C.addressof(frame)+0x80)
    system = frame.raw[0x40]
    # processDataBuffers loads the actual media template address in RSI.
    template = run(0x40c6f,[0x40c76],'','48 89 f0')
    engine = C.c_uint8.from_address(template).value
    assert engine == system == (4 if mask == 4 else 3)
    # The native context parameter copy and streamer descriptor lookup.
    run(0x7ba05,[0x7ba12],'','',C.addressof(context),0,template)
    assert context.raw[0x6c] == engine
    run(0x39af2,[0x39b04],'48 89 fb','',C.addressof(streamer),context.raw[0x6c])
    # The ring factory selects IGHardwareRingBufferMedia for both VCS types.
    metaclass = run(0x41379,[0x413c1],'41 56','41 5e',C.addressof(context))
    assert metaclass == base + 0xd8ec0
    ring = C.create_string_buffer(0x200)
    channel = C.create_string_buffer(0x200)
    run(0x4143f,[0x41447],'41 56 49 89 ff 49 89 d6','41 5e',
        C.addressof(context),0,C.addressof(ring))
    assert ring.raw[0x40] == engine
    # Media ring initialization independently selects its watchdog and reset
    # registers by physical instance (context+0x74), not by native type.
    run(0x6d9d9,[0x6da28],'48 89 fb 49 89 d7','',C.addressof(context),0,C.addressof(ring))
    target = 0x1d0000 if mask == 4 else 0x1c0000
    assert struct.unpack_from('<II',ring,0x58) == (target+0x3c,target+0x244)
    assert struct.unpack_from('<Q',ring,0x78)[0] == base+(0x14afa0 if mask==4 else 0x14af88)
    assert frame.raw[0x48] == (2 if mask==4 else 0)
    run(0x4c1b8,[0x4c1bf],'','',C.addressof(channel),0,C.addressof(ring))
    assert struct.unpack_from('<Q',channel,0x130)[0] == C.addressof(ring)
    # Host scheduler indexes its streamer slots by the native context type.
    scheduler = C.create_string_buffer(0x1000)
    struct.pack_into('<Q',scheduler,0xca0 + 8*engine,C.addressof(streamer))
    # bind uses R12=context and R14=scheduler. Preserve both in this entry shim.
    bound = run(0x38abb,[0x38ac9],'41 54 41 56 49 89 fc 49 89 d6',
                '48 89 f8 41 5e 41 5c',C.addressof(context),0,C.addressof(scheduler))
    assert bound == C.addressof(streamer)
    struct.pack_into('<Q',frame,0x40,sentinel)
    C.memset(C.addressof(bar),0,C.sizeof(bar))
    run(0x3aa02,[0x3aa40],'48 89 fb 48 89 d5','',bound,0,C.addressof(frame)+0x80)
    target = 0x1d0000 if mask == 4 else 0x1c0000
    assert struct.unpack_from('<Q',bar,target+0x510)[0] == sentinel
    assert struct.unpack_from('<I',bar,target+0x550)[0] == 1
    other = 0x1c0000 if mask == 4 else 0x1d0000
    assert struct.unpack_from('<I',bar,other+0x550)[0] == 0
    image.close()
    family = 'TGL GT2 (48 EU)' if tgl else 'ADL-P/U (64 EU)'
    print(f'{family}, VCS mask {mask}, native type {engine}, EL queue {target+0x510:#x}, load {target+0x550:#x}')


for tiger_lake in (True, False):
    for video in (1, 4, 5):
        replay(tiger_lake, video)
