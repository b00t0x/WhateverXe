#!/usr/bin/env python3
"""A rejected ADL-S boot consumer transition must not start the accelerator."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('bool XE::wrapTGLAcceleratorStart(');b=s.index(' if (callbackXE->adlnDetected',a)
entry=s[a:b]+' return true;\n}'
code='''
#include <cassert>
struct IOService {};
struct XE {
 bool tglGT2Detected=false,tglGT2Ready=false;
 bool display12SmallGTDetected=false,display12SmallGTReady=true;
 bool adlsDisplaySelected=true,adlsDisplayHooksReady=true,adlsBootConsumersFailed=false;
 static bool wrapTGLAcceleratorStart(IOService*,IOService*);
} xe,*callbackXE=&xe;
'''+entry+'''
int main(){
 assert(XE::wrapTGLAcceleratorStart(nullptr,nullptr));
 xe.adlsBootConsumersFailed=true;
 assert(!XE::wrapTGLAcceleratorStart(nullptr,nullptr));
 xe.adlsDisplaySelected=false;
 assert(XE::wrapTGLAcceleratorStart(nullptr,nullptr));
 xe.adlsDisplaySelected=true;xe.adlsBootConsumersFailed=false;xe.adlsDisplayHooksReady=false;
 assert(!XE::wrapTGLAcceleratorStart(nullptr,nullptr));
}
'''
with tempfile.TemporaryDirectory(prefix='adls-boot-runtime-') as d:
 d=Path(d);(d/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
print('PASS actual accelerator admission: failed ADL-S boot retirement blocks native start; unrelated backend unchanged')
