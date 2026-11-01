#!/usr/bin/env python3
"""Run actual provider binding/probe entries with bounded IOKit object mocks."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = (root.parent / "AppleIntelTGLCompat/AppleIntelTGLCompat.cpp").read_text()
core = source[source.index("template <typename T>"):source.index('extern "C" uint32_t tglRead32')]
probe = re.search(r'extern "C" uint32_t tglProbePort\(void \*port\) \{.*?\n\}', source, re.S).group()
adapter = (root.parent / "WhateverXe/kern_xelp_device.hpp").read_text()
adapter = adapter[adapter.index('namespace XELP {'):adapter.rindex('#endif')]
xe = (root.parent / "WhateverXe/kern_xe.cpp").read_text()
guard = xe[xe.index('static IOPCIDevice *xelpSelectedGPU;'):xe.index('// Detailed read-only display evidence')]
fixture = (root / "provider_binding_fixture.cpp").read_text().replace("// PROVIDER_SOURCE", adapter + guard + core + probe)
with tempfile.TemporaryDirectory(prefix="xelp-provider-binding-") as directory:
    directory = Path(directory)
    cpp, exe = directory / "test.cpp", directory / "test"
    cpp.write_text(fixture)
    subprocess.run(["clang++", "-std=c++14", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-I" + str(root.parent / "AppleIntelTGLCompat"),
                    "-I" + str(root.parent / "WhateverXe"),
                    str(cpp), "-o", str(exe)], check=True)
    for mode in ["adlp-fb", "adlp-auto", "rplp-auto", "tgl", "tgl-9a40", "tgl-g4", "adln", "adln-16eu", "adln-abi1", "unknown", "adln-revision", "adln-fake", "tgl-fake", "adln-abi1-fake", "adls", "adls-revision", "rpls", "rpls-revision"]:
        subprocess.run([str(exe), mode], check=True)
    # Every Linux ADL-P/RPL-P/U ID formerly missing from the shared table must
    # pass the actual Compat binding, physical-owner and native probe paths.
    for pci in ["4626", "4628", "462a", "46a0", "46a1", "46a2", "46b0", "46b1", "46b2", "46c0", "46c1", "46c2", "a720", "a721"]:
        subprocess.run([str(exe), "adlp-auto", pci], check=True)
    for pci in ["9a59", "9ac0", "9ac9", "9ad9", "9af8"]:
        subprocess.run([str(exe), "tgl", pci], check=True)
