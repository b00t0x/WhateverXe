#!/usr/bin/env python3
"""Execute production PPS/PWM wrappers with actual PG bridge and MMIO adapter."""
from pathlib import Path
import re,sys,subprocess,tempfile,hashlib
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
names=['processDisplay13Panel','wrapADLSLightUpEDP','display13PanelHealthy','display13PanelTarget','display13PanelPowerReference','display13PanelCapture','display13PanelTiming','display13PanelAUX','display13PanelBrightness','display13PanelPWMPeriod','display13PanelPWMEnable','wrapDisplay13PanelPower']
methods='\n'.join(re.search(r'(?:bool|void|uint32_t) XE::'+n+r'\(.*?\n\}',s,re.S).group() for n in names)
io=s[s.index('struct Display13FBPowerIO {'):s.index('// The static boot reader')]
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes();assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 installer=s[s.index('void XE::processDisplay13Panel('):s.index('bool XE::display13PanelTarget(')]
 for name,off in [('entry',0x48652),('configCall',0x4866b)]:
  expected=bytes(int(x,0) for x in re.search(name+r'\[\]=\{([^}]+)',installer).group(1).split(','))
  assert b[off:off+len(expected)]==expected
 # Native panel on/off and success return: the wrapper must surround both
 # control writes and the donor's wait, not just a brightness callback.
 assert b[0x487df:0x487e2]==bytes.fromhex('83 ca 03')
 assert b[0x4875e:0x48761]==bytes.fromhex('83 e2 f6')
 assert b[0x48abc:0x48abe]==bytes.fromhex('31 c0')
code=(r/'tests/display13_panel_runtime_fixture.cpp').read_text().replace('// PRODUCTION_IO',io).replace('// METHODS',methods)
with tempfile.TemporaryDirectory(prefix='display13-panel-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+sys.argv[1:],check=True)
