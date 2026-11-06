#!/usr/bin/env python3
"""Replay qualified native TGL setPortMode with the production result repair."""
from pathlib import Path
import argparse,hashlib,re,struct,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('donor',type=Path);p.add_argument('--serialization',action='store_true');p.add_argument('--source',type=Path);a=p.parse_args()
if hashlib.sha256(a.donor.read_bytes()).hexdigest()!='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731':p.error('unqualified donor SHA256')
r=Path(__file__).resolve().parents[1];s=(a.source or r/'WhateverXe/kern_xe.cpp').read_text()
# Verify the production admission bytes, not just the fixture's ready flag.
donor=a.donor.read_bytes()
# Verify reachability of the lane wrapper in the actual fixed-DP clock path,
# independently of direct wrapper tests. TC mode != TBT selects this branch;
# the port's negotiated/default lanes feed setFIALaneCount before clocks and
# DP_MODE. These are static donor assertions, not a full clock/MMIO replay.
for slot,target in [(0x198,0x603e8),(0x150,0x61d5c),(0x180,0x60930),(0x1a0,0x602cc)]:
 assert struct.unpack_from('<Q',donor,0xccad0+16+slot)[0]==target
for offset,code in [
 (0x6ce4c,'49 8b 86 48 05 00 00 83 78 0c 00 0f 84 91 00 00 00 48 ff 05 54 88 07 00 41 83 be 50 05 00 00 02 0f 85 39 01 00 00'),
 (0x6cfb2,'41 80 be 14 01 00 00 00 0f 84 c2 01 00 00 48 ff 05 01 87 07 00 41 8b b6 24 01 00 00 e9 b4 01 00 00'),
 (0x6d182,'41 0f b6 76 11 49 8b 06 4c 89 f7 ff 90 98 01 00 00 49 8b 06 4c 89 f7 ff 90 50 01 00 00 49 8b 06 4c 89 f7 ff 90 80 01 00 00')]:
 expected=bytes.fromhex(code);assert donor[offset:offset+len(expected)]==expected
for name,offset in [('modeEntry',0x5fcb8),('modeFailure',0x5ffeb),('modeReturn',0x6010e),('hotplugModeCall',0x74a4b),('laneGetEntry',0x602cc),('laneSetEntry',0x603e8)]:
 values=re.search(r'const uint8_t '+name+r'\[\] \{([^}]+)\}',s).group(1)
 expected=bytes(int(v.strip(),0) for v in values.split(','))
 assert donor[offset:offset+len(expected)]==expected,name
body=re.search(r'uint32_t XE::wrapTGLSetPortMode\(.*?\n\}',s,re.S).group()
sites=[int(v.strip(),0) for v in re.search(r'const uint32_t fiaSites\[\]=\{([^}]+)\}',s).group(1).split(',')]
calls=re.search(r'const uint8_t fiaCalls\[\]\[5\]=\{(.*?)\};',s,re.S).group(1)
expected=[bytes(int(v.strip(),0) for v in row.split(',')) for row in re.findall(r'\{([^}]+)\}',calls)]
assert len(sites)==len(expected)==5
for site,call in zip(sites,expected):assert donor[site:site+5]==call
wrappers='\n'.join(re.search(pattern,s,re.S).group() for pattern in [r'bool XE::tglNativeFiaAccess\(.*?\n\}',r'uint32_t XE::wrapTGLPortReadReg\(.*?\n\}',r'void XE::wrapTGLPortWriteReg\(.*?\n\}'])
legacy=re.search(r'bool XE::tglLegacyFia\(.*?\n\}',s,re.S).group()
f=(r/'tests'/('tgl_mode_serial_fixture.cpp' if a.serialization else 'tgl_mode_native_fixture.cpp')).read_text().replace('// PRODUCTION_WRAPPER',body).replace('// FIA_WRAPPERS',wrappers).replace('// LEGACY_FIA_WRAPPER',legacy)
header=(r/'WhateverXe/kern_xe.hpp').read_text()
policy=header[header.index(' uintptr_t tglNativeLegacyHPDCaller'):header.index(' uint32_t tglTCProbeMask')]
f=f.replace('// LEGACY_HPD_POLICY',policy)
mode_policy=re.search(r' bool tglTCModeAllowed\(.*?\n \}',header,re.S).group()
f=f.replace('// LEGACY_MODE_POLICY',mode_policy)
installer=re.search(r'   const auto hotplug=patcher.solveSymbol<mach_vm_address_t>\(index,\s*"__ZN31AppleIntelFramebufferController20invokeHotplugHandler.*?patcher.clearError\(\);',s,re.S).group()
f=f.replace('// LEGACY_HPD_INSTALLER',installer)
with tempfile.TemporaryDirectory(prefix='tgl-mode-') as d:
 d=Path(d);(d/'check.cpp').write_text(f)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O1','-g','-pthread','-I',str(r/'WhateverXe'),str(d/'check.cpp'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check'),str(a.donor.resolve())],check=True)
