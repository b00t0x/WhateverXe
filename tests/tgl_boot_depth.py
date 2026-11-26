"""Run the initial eDP decision with an original framebuffer or a bounded comparator."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'WhateverXe/kern_xe.cpp').read_text()
state = s[s.index('enum TGLBootPhase'):s.index('\n}\n',s.index('void endTGLBootMode'))+2]
decision = s[s.index('bool XE::selectTGLBootDepth'):s.index('// hwSetMode copies CRTCParams')]
fixture = (root/'tests/tgl_boot_depth_fixture.cpp').read_text()
with tempfile.TemporaryDirectory() as d:
    path = Path(d)
    cpp = path/'fixture.cpp'
    cpp.write_text(fixture.replace('// PRODUCTION_STATE',state).replace('// PRODUCTION_DECISION',decision))
    subprocess.run(['clang++','-arch','x86_64','-std=c++17','-I'+str(root/'WhateverXe'),str(cpp),'-o',str(path/'fixture')],check=True)
    args = sys.argv[1:]
    if args:
        assert hashlib.sha256(Path(args[0]).read_bytes()).hexdigest() in {
            'c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731',
            '115a4616e862228a8c82df6e52ba41ac02b9750f116352ff3575aadcc950888e'}
    subprocess.run([str(path/'fixture'),*args],check=True)
