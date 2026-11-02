#!/usr/bin/env python3
"""Call the real TC mode entry, selector, GOP proof and power/ownership core."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
f='\n'.join(re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group() for sig in ['XELP::LinkRequirements XE::framebufferLinkRequirements(', 'unsigned XE::display13TCTarget(', 'uint32_t XE::wrapDisplay13TCMode('])
install=s[s.index('bool XE::processDisplay13FramebufferTC('):s.index('bool XE::processDisplay13FramebufferOutput(')]
assert '{entries[Mode].symbol,wrapDisplay13TCMode,orgDisplay13TCMode}' in install
assert 'display13FBTCHooksReady=processDisplay13FramebufferTC(patcher,index,address,size)' in s
# Admission never substitutes an installed prototype for the real mode route.
assert 'display13FBTCHooksReady' in s[s.index('unsigned XE::display13TCTarget('):s.index('uint32_t XE::wrapDisplay13TCMode(')]
code=(r/'tests/display13_tc_preparation_fixture.cpp').read_text().replace('// FUNCTIONS',f)
code=code.replace('// MODE_QUIESCENCE',re.search(r'    bool modeQuiescent\(.*?\n    \}',s,re.S).group())
with tempfile.TemporaryDirectory(prefix='tc-preparation-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
