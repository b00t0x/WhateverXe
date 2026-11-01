"""Check runtime guards in any supplied x86_64 kernel/KC, without a build list."""
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

    fn, protect = (symbols[x] for x in ('_pmap_enter_options', '_IOProtectCacheMode'))
    block = source.split('static void tglPrepare' + family + 'MMIOUC')[1].split('static void ')[0]
    checks = re.findall(r'!memcmp\(reinterpret_cast<void \*>\((fn|protect)(\+0x[0-9A-Fa-f]+)?\),([A-Za-z]+),sizeof', block)
    assert len(checks) == 8, checks
    for base, offset, name in checks:
        address = (fn if base == 'fn' else protect) + (int(offset[1:], 16) if offset else 0)
        values = re.search(r'const uint8_t ' + name + r'\[\] \{([^}]+)\}', block)[1]
        expected = bytes(int(v.strip(), 0) for v in values.split(','))
        assert read(address, len(expected)) == expected, name
    assert 'tglNativeProtectCall(protect,fn,' + ('true' if family == 'Tahoe' else 'false') + ')' in block
    protect_code = read(protect, 512)
    verify = symbols['_pmap_verify_noncacheable']
    caller = read(verify, 96)
    store = symbols['_kernel_pmap_store']
    # Resolve the call with the production matcher below before fetching its
    # callee. No verifier/call/field offsets or particular relative distance.
    include = Path(__file__).resolve().parents[1] / 'WhateverXe'
    with tempfile.TemporaryDirectory(prefix='xe-root-target-') as directory:
        path = Path(directory)
        (path / 'target.cpp').write_text('#include "kern_xelp_native_placement.hpp"\n#include <cstdio>\nint main(){unsigned char b[]={' + ','.join(map(str,caller)) + '};printf("%llx",(unsigned long long)XELPNative::rootCall(b,sizeof(b),' + str(verify) + 'ULL,' + str(store) + 'ULL));}')
        subprocess.run(['clang++','-std=c++17','-I'+str(include),str(path/'target.cpp'),'-o',str(path/'target')],check=True)
        target = int(subprocess.check_output([str(path/'target')],text=True),16)
    assert target
    accessor = read(target, 64)
    # Execute the production decoder/field contract on the real native bytes.
    def literal(value):
        return ','.join(str(x) for x in value)
    # Compile the exact production decoder wrapper against Lilu's HDE source.
    decoder = re.search(r'static size_t tglNativeInstructionSize\(.*?\n\}',source,re.S)[0]
    decoder = decoder.replace('Disassembler::hde_t','hde64s').replace('Disassembler::hdeDisasm(reinterpret_cast<uintptr_t>(code),','hde64_disasm(code,')
    cpp = '#include "kern_xelp_native_placement.hpp"\n#include "hde64.h"\n#include <assert.h>\n' + decoder + '\nint main(){\n'
    cpp += 'unsigned char protect[]={' + literal(protect_code) + '}, accessor[]={' + literal(accessor) + '};\n'
    cpp += f'assert(XELPNative::protectCall(protect,sizeof(protect),{protect}ULL,{fn}ULL,' + ('true' if family=='Tahoe' else 'false') + ',tglNativeInstructionSize));\n'
    cpp += 'assert(XELPNative::rootAccessor(accessor,sizeof(accessor)));}\n'
    with tempfile.TemporaryDirectory(prefix='xe-kernel-contract-') as directory:
        path = Path(directory)
        (path / 'test.cpp').write_text(cpp)
        headers = include.parent / 'Lilu.kext/Contents/Resources/Headers'
        subprocess.run(['clang','-I'+str(headers),'-c',str(hde_source),'-o',str(path/'hde.o')],check=True)
        subprocess.run(['clang++', '-std=c++17', '-I' + str(include), '-I'+str(headers), str(path / 'test.cpp'), str(path/'hde.o'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
    print('PASS: 8 instruction/stack guards, decoded native protect/verifier calls and private pmap accessor; no build/hash/call-offset admission')
