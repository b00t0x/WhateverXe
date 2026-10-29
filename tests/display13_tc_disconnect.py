#!/usr/bin/env python3
"""Compose the actual TC IO owner checks with clock stop and mode release."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('struct Display13FBTCIO :');b=s.index('// Only used after native object/BAR binding',a)
code=(r/'tests/display13_tc_disconnect_fixture.cpp').read_text().replace('// PRODUCTION_IO',s[a:b]).replace('// PRODUCTION_GATE',re.search(r'void XE::wrapDisplay13DPPhyGating\(.*?\n\}',s,re.S).group())
code=code.replace('// PRODUCTION_DRAIN',re.search(r'void XE::drainDisplay13TCMode\(.*?\n\}',s,re.S).group())
release=s[s.index('uint32_t XE::releaseDisplay13Path('):s.index('void XE::wrapAlderLakeNFreePath(')]
assert release.index('orgAlderLakeNFreePath)(fb,path)') < release.index('if(removed)drainDisplay13TCMode(binding.ddi);')
with tempfile.TemporaryDirectory(prefix='tc-disconnect-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
