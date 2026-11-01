#!/usr/bin/env python3
"""Replay exact donor timing publication/validation with retained mock objects.

No Apple bytes are stored in the fixture. This checks the native data contract,
not CoreDisplay mode enumeration, native lock behavior, or hardware modesetting.
"""
from pathlib import Path
import hashlib, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[1]
donor = Path(sys.argv[1]).resolve()
data = donor.read_bytes()
assert hashlib.sha256(data).hexdigest() == 'c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
assert data[0x30e6b] == 0xe8
assert data[0x30e4c:0x30e52] == bytes.fromhex('81 e6 ff 03 00 00')
with tempfile.TemporaryDirectory(prefix='dsi-timing-') as directory:
    executable = Path(directory) / 'test'
    subprocess.run(['clang++', '-arch', 'x86_64', '-std=c++17', '-O1',
                    '-I'+str(root/'WhateverXe'), str(root/'tests/dsi_apple_timing_native_fixture.cpp'),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(donor)], check=True)
