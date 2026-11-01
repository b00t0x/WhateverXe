#!/usr/bin/env python3
"""Replay production WM preparation while CURCNTR still holds the old size."""
from pathlib import Path
import re, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
body=re.search(r'void XE::adlnWMSetup\(.*?\n\}',s,re.S).group()
code=(r/'tests/display13_cursor_resize_fixture.cpp').read_text().replace('// PRODUCTION_SETUP',body)
with tempfile.TemporaryDirectory(prefix='cursor-resize-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
