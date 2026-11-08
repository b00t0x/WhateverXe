#!/usr/bin/env python3
"""Connect the real WEX config producer to the complete Compat consumer.

Only XE declarations/installed-hook prerequisites and registry IO are modeled.
Wiring conversion, config construction/validation, binding and probe are real.
"""
from pathlib import Path
import subprocess
import tempfile
from compat_test_support import ROOT, compile_fixture
from production_source import read_source

source = read_source(ROOT/'WhateverXe/kern_xe.cpp')
start = source.index('bool XE::xelpBindProbe(')
method = source[start:source.index('\nvoid XE::', start)]
header = (ROOT/'WhateverXe/kern_xe.hpp').read_text()
start = header.index('uint32_t tglTCProbeMask() const')
mask = header[start:header.index('\n bool ', start)]
fixture = Path(__file__).with_name('compat_producer_fixture.cpp').read_text()
fixture = fixture.replace('// TC_MASK_METHOD', mask).replace('// BIND_METHOD', method)
with tempfile.TemporaryDirectory(prefix='compat-producer-') as directory:
    exe = compile_fixture(directory, fixture)
    for mode in ['tgl-combo','tgl-tc','adls','adln-hdmi','adln-two',
                 'tc-hdmi','tc-dp','tc-alt','panel','dsi',
                 'tc-dp-missing-aux','panel-missing-power','dsi-missing-resource']:
        subprocess.run([str(exe), mode], check=True)
