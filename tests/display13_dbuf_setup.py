#!/usr/bin/env python3
"""Exercise the production allocation adapter with real DBUF ownership."""
from pathlib import Path
from production_source import read_source
import re, subprocess, tempfile
r = Path(__file__).resolve().parents[1]
s = read_source(r/'WhateverXe/kern_xe.cpp')
guard = re.search(r'bool XE::dsiSharedWriterStopped\(.*?\n\}', s, re.S).group()
body = re.search(r'void XE::adlnSetupDBUFPipe\(.*?\n\}', s, re.S).group()
pipewm = re.search(r'void XE::wrapAlderLakeNPipeWMSetup\(.*?\n\}', s, re.S).group()
code = (r / 'tests/display13_dbuf_setup_fixture.cpp').read_text().replace('// PRODUCTION_SETUP', guard+'\n'+body+'\n'+pipewm)
with tempfile.TemporaryDirectory(prefix='dbuf-setup-') as d:
    p = Path(d)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-I' + str(r / 'WhateverXe'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
