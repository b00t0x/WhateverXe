#!/usr/bin/env python3
"""Execute every Compat export against bounded OS/memory boundaries."""
from pathlib import Path
import subprocess
import tempfile
import sys
from compat_test_support import ROOT, compile_fixture
sys.path.insert(0, str(ROOT/'tools'))
from check_compat_version import check_exports

fixture = Path(__file__).with_name('compat_exports_fixture.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='compat-exports-') as directory:
    exe = compile_fixture(directory, fixture)
    check_exports(exe, c_prefix='_' if sys.platform=='darwin' else '')
    failed = []
    for mode in ['plugin-off','mmio32','mmio64','external64','gates','dsb','dsb-last','dsb-last-small','clock',
                 'alloc','short','null-bytes','zero-physical','unaligned','overflow',
                 'pte-window','pte-base','ggtt-alignment','fixed-size','page-size']:
        result = subprocess.run([str(exe), mode])
        if result.returncode:
            failed.append(mode)
    assert not failed, 'Compat contract failures: '+', '.join(failed)
