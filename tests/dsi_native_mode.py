#!/usr/bin/env python3
"""Check geometry rollback and optionally replay the exact donor CPU helpers."""
from pathlib import Path
import hashlib, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
args=[]
if len(sys.argv)>1:
    donor=Path(sys.argv[1]).resolve()
    assert hashlib.sha256(donor.read_bytes()).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
    args=[str(donor)]
with tempfile.TemporaryDirectory(prefix='dsi-mode-') as directory:
    executable=Path(directory)/'test'
    subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-I'+str(root/'WhateverXe'),
                    str(root/'tests/dsi_native_mode_fixture.cpp'),'-o',str(executable)],check=True)
    subprocess.run([str(executable),*args],check=True)
