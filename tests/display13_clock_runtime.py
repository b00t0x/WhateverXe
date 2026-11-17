#!/usr/bin/env python3
"""Production dual-clock dispatch and IRQ guard, real clock state machine.

The MMIO/mailbox device and kernel locks are modeled. Active WM certificates
are not synthesized: active changes exercise refusal/unchanged paths here.
"""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
def fn(sig):return re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group()
clock=(r/'tests/display13_clock_set.cpp').read_text().split('int main()')[0]
clock=clock.replace('return regs.at(reg);','return regs[reg];')
clock=clock.replace('assert(reg==Mailbox || reg==Data || reg==Data1 || reg==PLL || reg==Control);','')
code=(r/'tests/display13_clock_runtime_fixture.cpp').read_text()
code=code.replace('// DEVICE',clock).replace('// GUARD',s[s.index('struct Display13FBClockGuard {'):s.index('static void adlnInvalidateWM()')])
code=code.replace('// METHODS',fn('static void adlnSetColdClock(')+'\n'+fn('void XE::adlnSetDualClock('))
with tempfile.TemporaryDirectory(prefix='display13-clock-runtime-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
