#!/usr/bin/env python3
"""Run actual provider binding/probe entries with bounded IOKit object mocks."""
from pathlib import Path
import subprocess
import tempfile

from compat_test_support import compile_fixture

root = Path(__file__).resolve().parent
xe = (root.parent / "WhateverXe/kern_xe.cpp").read_text()
guard = xe[xe.index('static IOPCIDevice *xelpSelectedGPU;'):xe.index('// Detailed read-only display evidence')]
fixture = (root / "provider_binding_fixture.cpp").read_text().replace("// PROVIDER_SOURCE", guard)
with tempfile.TemporaryDirectory(prefix="xelp-provider-binding-") as directory:
    directory = Path(directory)
    exe = compile_fixture(directory, fixture)
    for mode in ["adln-map-small", "adln-map-large", "adlp-map-small", "tgl-map-small", "adlp-fb", "adlp-auto", "rplp-auto", "tgl", "tgl-9a40", "tgl-g4", "adln", "adln-16eu", "adln-abi1", "unknown", "adln-revision", "adln-fake", "tgl-fake", "adln-abi1-fake", "adls", "adls-revision", "rpls", "rpls-revision"]:
        subprocess.run([str(exe), mode], check=True)
    # Every Linux ADL-P/RPL-P/U ID formerly missing from the shared table must
    # pass the actual Compat binding, physical-owner and native probe paths.
    for pci in ["4626", "4628", "462a", "46a0", "46a1", "46a2", "46b0", "46b1", "46b2", "46c0", "46c1", "46c2", "a720", "a721"]:
        subprocess.run([str(exe), "adlp-auto", pci], check=True)
    for pci in ["9a59", "9ac0", "9ac9", "9ad9", "9af8"]:
        subprocess.run([str(exe), "tgl", pci], check=True)
