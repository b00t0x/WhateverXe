#!/usr/bin/env python3
"""Connect the real WEX config producer to the complete Compat consumer.

Only XE declarations/installed-hook prerequisites and registry IO are modeled.
Wiring conversion, config construction/validation, binding and probe are real.
"""
from pathlib import Path
import re
import subprocess
import tempfile
from compat_test_support import ROOT, compile_fixture
from production_source import read_source

source = read_source(ROOT/'WhateverXe/kern_xe.cpp')
names = ('xelpBindProbe','display13Available','display13PortReady','display13HPDAllowed',
         'wrapDisplay13Hotplug','wrapDisplay13ShortPulse','wrapDisplay13ProbeMode','wrapAlderLakeNPortState')
methods = '\n'.join(re.search(r'^[\w:]+ XE::'+name+r'\(.*?\n\}', source, re.M | re.S)[0]
                    for name in names)
header = (ROOT/'WhateverXe/kern_xe.hpp').read_text()
start = header.index('uint32_t tglTCProbeMask() const')
mask = header[start:header.index('\n bool ', start)]
fixture = Path(__file__).with_name('compat_producer_fixture.cpp').read_text()
fixture = fixture.replace('// TC_MASK_METHOD', mask).replace('// PROBE_METHODS', methods)
with tempfile.TemporaryDirectory(prefix='compat-producer-') as directory:
    exe = compile_fixture(directory, fixture)
    for mode in ['tgl-combo','tgl-tc','adls','adln-hdmi','adln-two',
                 'tc-hdmi','tc-dp','tc-alt','panel','dsi',
                 'tc-dp-missing-aux','panel-missing-power','dsi-missing-resource',
                 'panel-alt-wait-hooks','panel-alt-wait-capability','mixed-fixed-tc']:
        subprocess.run([str(exe), mode], check=True)
