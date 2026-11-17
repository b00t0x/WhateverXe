#!/usr/bin/env python3
"""Production Display13 SST adapters with real bridge/binding and mock MMIO."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
functions='\n'.join(re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group() for sig in [
 'uint8_t *XE::display13SSTPort(', 'bool XE::display13HDMIPort(', 'bool XE::display13MainLinkAUX(',
 'uint32_t XE::wrapDisplay13EnablePort(', 'uint32_t XE::wrapDisplay13DisablePort(',
 'uint32_t XE::wrapDisplay13SignalLevels(', 'uint32_t XE::wrapDisplay13TrainingPattern('])
binding=re.search(r'static TGLFBBinding::Readout adlnInspectPathBinding\(.*?\n\}',s,re.S).group()
fixture=(r/'tests/tgl_fb_binding.cpp').read_text().split('int main()')[0]
fixture=fixture.replace('port[0x580]','port[0x700]')
code=(r/'tests/display13_sst_fixture.cpp').read_text().replace('// NATIVE_OBJECTS',fixture).replace('// BINDING',binding).replace('// FUNCTIONS',functions)
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes();assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 install=s[s.index('bool XE::processDisplay13FramebufferSST('):s.index('uint8_t *XE::display13SSTPort(')]
 for name,offset in [('patternEntry',0x955b8),('signalEntry',0x60ae0),('enableEntry',0x5f9f4),('disableEntry',0x5fc56),('pcsABI',0x60eb9),('lanesABI',0x61468),('successABI',0x61478)]:
  a=re.search(name+r'\[\]=\{([^}]+)\}',install).group(1);v=bytes(int(x,0) for x in a.split(','))
  assert b[offset:offset+len(v)]==v
 # resetSoftwareState clears port mode before setPortMode writes it. These
 # producer instructions establish mode0 independently of a firmware report.
 assert b[0x1b12c:0x1b12e]==bytes.fromhex('31c0')             # xor eax,eax
 assert b[0x1b15b:0x1b162]==bytes.fromhex('48898750050000')   # mov rax,0x550(rdi)
 assert b[0x1b50d:0x1b514]==bytes.fromhex('41899e50050000')   # mov ebx,0x550(r14)
 print('PASS exact donor Display13 SST hooks, initial mode0 producer and Combo register-field preflight')
with tempfile.TemporaryDirectory(prefix='display13-sst-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+sys.argv[1:],check=True)
