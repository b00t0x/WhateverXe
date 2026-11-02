#!/usr/bin/env python3
"""Check every emitted WEX stack frame, with bounded, reviewed exceptions.

These are per-function budgets, not a whole-kernel call-chain proof. Native,
Lilu/IOKit and indirect callers still need separate review. DEBUG must not rely
on optimization to remove repeated snapshots or diagnostic string buffers.
"""
import json
import struct
import sys
from pathlib import Path

DEFAULT_LIMIT = 2048
# These installers no longer nest inside one another. The remaining larger
# frames retain bounded patch tables, register records or diagnostic snapshots.
LIMITS = {
    '_ZN2XE13processKernelER13KernelPatcherP10DeviceInfo': 2048,
    '_ZN2XE13adlnBootstrapEP11IOPCIDevice': 5120,
    '_ZN2XE11processKextER13KernelPatchermym': 256,
    '_ZN2XE21processTGLAcceleratorER13KernelPatchermym': 6144,
    '_ZN2XE15processKextFullER13KernelPatchermym': 6144,
    '_ZL22tglStartOrderSerializePvS_P11OSSerialize': 3328,
    '_ZN2XE20display13WMArmUpdateEP9IOService': 2816, # Existing body behind the DSI entry guard.
    '_ZN2XE24wrapDisplay13WMArmUpdateEP9IOService': 256,
    '_ZN12_GLOBAL__N_120tglCaptureHangDetailERKN14TGLHangPending7CaptureEPKhRKN13TGLHangSelect9SelectionE': 2688,
    '_ZN2XE17tglObserveScanoutEPvS0_jbj': 2304,
    '_ZN12_GLOBAL__N_120tglRecoverySerializeEPvS0_P11OSSerialize': 2304,
    '_ZN2XE17wrapTGLCSBProcessEPvj': 2304,
    # Snapshot owners consume reusable storage before native reentry/unlock.
    '_ZN2XE23wrapAlderLakeNPowerInitEPvS0_': 1280,
    '_ZN2XE27wrapAlderLakeNSetupWorkLoopEP9IOServiceP10IOWorkLoop': 1536,
    '_ZN12_GLOBAL__N_124display13FBInspectMemoryEP9IOServicebjb': 1536,
    '_ZN2XE19wrapDisplay13TCModeEP8OSObjectj': 1024,
    '_ZL15tglHostCSBFatalPKcPKjPv': 2048,
    '_ZN2XE21wrapTGLGuCHeaderPanicEPv': 1024,
}
TEMPLATE_LIMITS = {
    '_ZN10AlderLakeS4Boot7prepareI': 2304,
    '_ZN13Display13Boot9normalizeI': 1280,
}
REQUIRED = {
    '_ZN2XE13processKernelER13KernelPatcherP10DeviceInfo',
    '_ZN2XE13adlnBootstrapEP11IOPCIDevice',
    '_ZN2XE11processKextER13KernelPatchermym',
    '_ZN2XE21processTGLAcceleratorER13KernelPatchermym',
}


def limit_for(symbol):
    if symbol in LIMITS:
        return LIMITS[symbol]
    for prefix, limit in TEMPLATE_LIMITS.items():
        if symbol.startswith(prefix):
            return limit
    return DEFAULT_LIMIT


def stack_files(objects):
    paths = []
    for obj in map(Path, objects):
        usage = obj.with_suffix('.su')
        if usage.is_file():
            paths.append(usage)
            continue
        # Clang emits no .su for an optimized data-only translation unit.
        # Accept that case only after checking the actual Mach-O sections.
        data = obj.read_bytes()
        if struct.unpack_from('<I', data)[0] != 0xFEEDFACF:
            raise ValueError(f'not a Mach-O 64-bit object: {obj.name}')
        offset = 32
        for _ in range(struct.unpack_from('<I', data, 16)[0]):
            command, size = struct.unpack_from('<II', data, offset)
            if command == 0x19:  # LC_SEGMENT_64
                for i in range(struct.unpack_from('<I', data, offset + 64)[0]):
                    section = offset + 72 + i * 80
                    length = struct.unpack_from('<Q', data, section + 40)[0]
                    flags = struct.unpack_from('<I', data, section + 64)[0]
                    if length and flags & 0x80000400:  # instruction attributes
                        raise ValueError(f'missing stack-usage file for code: {obj.name}')
            offset += size
    return paths


def check_objects(objects):
    return check(stack_files(objects))


def check(paths):
    if isinstance(paths, (str, Path)):
        paths = [Path(paths)]
    files = []
    for path in map(Path, paths):
        files.extend(sorted(path.glob('*.su')) if path.is_dir() else [path])
    if not files:
        raise ValueError('no stack-usage files')
    frames = {}
    for path in files:
        for line in path.read_text().splitlines():
            location, size, kind = line.split('\t')
            symbol = location.rsplit(':', 1)[-1]
            frame = {'bytes': int(size), 'kind': kind, 'limit': limit_for(symbol)}
            if kind != 'static' or frame['bytes'] > frame['limit']:
                raise ValueError(f'kernel stack frame exceeds budget: {symbol}: {frame}')
            # Inline/template bodies may be emitted by several translation units.
            if symbol not in frames or frame['bytes'] > frames[symbol]['bytes']:
                frames[symbol] = frame
    missing = REQUIRED - frames.keys()
    if missing:
        raise ValueError(f'missing stack records: {sorted(missing)}')
    return frames


if __name__ == '__main__':
    print(json.dumps(check(sys.argv[1:]), indent=2))
