"""Check runtime guards in any supplied x86_64 kernel/KC, without a build list."""
import re
import struct
import subprocess
import tempfile
from pathlib import Path


def check(kernel, source, family):
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
    offsets = re.findall(r'XELPCall::targets\([^;]+?protect\+(0x[0-9A-Fa-f]+),fn\)', block)
    assert len(offsets) == 1, offsets
    site = protect + int(offsets[0], 16)
    call = read(site, 5)
    verify = symbols['_pmap_verify_noncacheable']
    accessor_call = read(verify + 0x10, 8)
    assert accessor_call[:4] == bytes.fromhex('4889dee8')
    target = verify + 0x18 + struct.unpack_from('<i', accessor_call, 4)[0]
    accessor = read(target, 36)
    # Execute the production decoder/field contract on the real native bytes.
    def literal(value):
        return ','.join(str(x) for x in value)
    cpp = '#include "kern_xelp_call_target.hpp"\n#include <assert.h>\nint main(){\n'
    cpp += 'unsigned char call[]={' + literal(call) + '}, accessor[]={' + literal(accessor) + '};\n'
    cpp += f'assert(XELPCall::targets(call,sizeof(call),{site}ULL,{fn}ULL));\n'
    cpp += 'assert(XELPCall::rootAccessor(accessor,sizeof(accessor)));}\n'
    with tempfile.TemporaryDirectory(prefix='xe-kernel-contract-') as directory:
        path = Path(directory)
        (path / 'test.cpp').write_text(cpp)
        include = Path(__file__).resolve().parents[1] / 'WhateverXe'
        subprocess.run(['clang++', '-std=c++17', '-I' + str(include), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
    print('PASS: 8 instruction/stack guards, symbol-resolved CALL and private pmap accessor; no build/hash admission')
