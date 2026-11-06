"""Replay production pmap location/ABI guards in a supplied x86_64 kernel."""
import re
import struct
import subprocess
import tempfile
from pathlib import Path


def check(kernel, source, family, hde_source):
    assert hde_source and hde_source.is_file(), '--kernel requires --hde-source (Lilu hde/hde64.c)'
    data = kernel.read_bytes()
    assert data[:4] == bytes.fromhex('cffaedfe'), 'expected x86_64 Mach-O'
    symbols = {}
    for line in subprocess.check_output(['nm', str(kernel)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                symbols[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    segments, pos = [], 32
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<II', data, pos)
        if command == 0x19:
            segments.append(struct.unpack_from('<4Q', data, pos + 24))
        pos += size

    def read(address, length):
        for base, _, offset, size in segments:
            if base <= address and address + length <= base + size:
                return data[offset + address - base:offset + address - base + length]
        raise ValueError(hex(address))

    fn, protect, verify, store, panic = (symbols[x] for x in (
        '_pmap_enter_options', '_IOProtectCacheMode', '_pmap_verify_noncacheable', '_kernel_pmap_store', '_panic'))
    name = '' if family == 'BigSur' else family
    block = source.split('static void tglPrepare' + name + 'MMIOUC(')[1].split('\n}')[0]
    patterns = re.findall(r'const bool (\w+)Ready=native.find\(\1,\1At(?:,false)?\);', block)
    assert len(patterns) >= 4
    abi = re.search(r'tglNativeProtectCall\(protect,fn,XELPNative::ProtectABI::(\w+)\)', block)[1]
    assert not re.search(r'fn\+0x|protect\+0x', block), 'fixed native instruction placement'
    include = Path(__file__).resolve().parents[1] / 'WhateverXe'
    decoder = re.search(r'static size_t tglNativeInstructionSize\(.*?\n\}',source,re.S)[0]
    decoder = decoder.replace('Disassembler::hde_t','hde64s').replace('Disassembler::hdeDisasm(reinterpret_cast<uintptr_t>(code),','hde64_disasm(code,')
    locator = re.search(r'struct TGLNativePmapCode \{.*?\n\};',source,re.S)[0]
    lilu = (hde_source.parent.parent/'Lilu/Sources/kern_patcher.cpp').read_text()
    find = re.search(r'bool KernelPatcher::findPattern\(.*?\n\}',lilu,re.S)[0]
    caller = read(verify,96)
    # Fetch the accessor from the production resolver; no caller/callee offset.
    def literal(value):
        return ','.join(str(x) for x in value)
    with tempfile.TemporaryDirectory(prefix='xe-root-target-') as directory:
        path=Path(directory)
        (path/'target.cpp').write_text('#include "kern_xelp_native_placement.hpp"\n#include <cstdio>\nint main(){unsigned char b[]={' + literal(caller) + '};printf("%llx",(unsigned long long)XELPNative::rootCall(b,sizeof(b),' + str(verify) + 'ULL,' + str(store) + 'ULL,'+str(symbols['_kernel_pmap'])+'ULL));}')
        subprocess.run(['clang++','-std=c++17','-I'+str(include),str(path/'target.cpp'),'-o',str(path/'target')],check=True)
        target=int(subprocess.check_output([str(path/'target')],text=True),16)
    assert target
    cpp=r'''
#include "kern_xelp_native_placement.hpp"
#include "hde64.h"
#include <cassert>
#include <cstdlib>
#include <vector>
using Bytes=std::vector<uint8_t>;
namespace Buffer { template<class T>T*create(size_t n){return static_cast<T*>(malloc(n*sizeof(T)));} template<class T>void deleter(T*p){free(p);} }
struct KernelPatcher {
 static constexpr int KernelID=0; uintptr_t panic;
 uintptr_t solveSymbol(int,const char*){return panic;} void clearError(){}
 static bool findPattern(const void*,const void*,size_t,const void*,size_t,size_t*);
};
struct {uint64_t(*nofault)(uint64_t,uint64_t,uint64_t);}tglVM;
uintptr_t mapped=0;Bytes input;
uint64_t copy(uint64_t from,uint64_t to,uint64_t n){if(from!=mapped)return 0;auto len=n<input.size()?n:input.size();memcpy(reinterpret_cast<void*>(to),input.data(),len);return len;}
'''+find+'\n'+decoder+'\n'+locator+'\nint main(){\n'
    cpp+=f'KernelPatcher patcher{{{panic}ULL}};tglVM.nofault=copy;mapped={fn}ULL;\n'
    cpp+='const Bytes original={'+literal(read(fn,0x4000))+'};input=original;\n'
    cpp+='for(unsigned shift:{0U,17U}){input=original;input.insert(input.begin(),shift,0x90);input.resize(original.size());mapped='+str(fn)+'ULL-shift;TGLNativePmapCode code(patcher,mapped);assert(code.length);\n'
    for key in patterns:
        values=re.search(r'const uint8_t '+key+r'\[\]\s*\{([^}]+)\}',block)[1]
        unique='false' if 'native.find('+key+','+key+'At,false)' in block else 'true'
        cpp+='{const uint8_t pattern[]={'+values+'};size_t at=0;assert(code.find(pattern,at,'+unique+'));\n'
        cpp+='auto saved=input;input[at+sizeof(pattern)-1]^=1;{TGLNativePmapCode wrong(patcher,mapped);size_t ignored=0;assert(!wrong.find(pattern,ignored,'+unique+'));}input=saved;}\n'
    cpp+='}\n'
    cpp+='uint8_t protection[]={'+literal(read(protect,512))+'},accessor[]={'+literal(read(target,64))+'};\n'
    cpp+=f'assert(XELPNative::protectCall(protection,sizeof(protection),{protect}ULL,{fn}ULL,XELPNative::ProtectABI::{abi},tglNativeInstructionSize));\n'
    cpp+=f'assert(!XELPNative::protectCall(protection,sizeof(protection),{protect}ULL,{fn+1}ULL,XELPNative::ProtectABI::{abi},tglNativeInstructionSize));\n'
    cpp+='assert(XELPNative::rootAccessor(accessor,sizeof(accessor)));}\n'
    with tempfile.TemporaryDirectory(prefix='xe-kernel-contract-') as directory:
        path=Path(directory);(path/'test.cpp').write_text(cpp)
        subprocess.run(['clang','-I'+str(hde_source.parent),'-c',str(hde_source),'-o',str(path/'hde.o')],check=True)
        subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(include),'-I'+str(hde_source.parent),str(path/'test.cpp'),str(path/'hde.o'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
    print('PASS: '+family+' production locator with Lilu search/HDE, placement shift, ABI mutation, resolved protect/verifier calls')

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--kernel',type=Path,required=True);p.add_argument('--family',choices=['BigSur','Monterey','Ventura','Sonoma','Sequoia','Tahoe'],required=True);p.add_argument('--hde-source',type=Path,required=True);a=p.parse_args()
    check(a.kernel,(Path(__file__).resolve().parents[1]/'WhateverXe/kern_xe.cpp').read_text(),a.family,a.hde_source)
