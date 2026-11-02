#!/usr/bin/env python3
"""Reject old frames and new oversized functions in any translation unit."""
from pathlib import Path
import importlib.util,tempfile,subprocess
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('stack_limits',root/'tools/check_boot_stack.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def line(symbol,size,kind='static'):
 return f'/fixture.cpp:1:{symbol}\t{size}\t{kind}\n'
def rejects(paths):
 try:module.check(paths)
 except ValueError:return
 raise AssertionError('unsafe stack records accepted')
with tempfile.TemporaryDirectory(prefix='wex-stack-limits-') as tmp:
 p=Path(tmp);core=p/'core.su';extra=p/'other.su'
 baseline=''.join(line(s,24) for s in module.REQUIRED)
 core.write_text(baseline);extra.write_text(line('_new_callback',2048))
 assert module.check([core,extra])['_new_callback']['bytes']==2048
 # Formerly only kern_xe was checked; a new frame in another TU must fail.
 extra.write_text(line('_new_callback',2049));rejects([core,extra])
 extra.write_text(line('_new_callback',24,'dynamic'));rejects([core,extra])
 old={
  '_ZN2XE23wrapAlderLakeNPowerInitEPvS0_':4856,
  '_ZN2XE27wrapAlderLakeNSetupWorkLoopEP9IOServiceP10IOWorkLoop':2440,
  '_ZN12_GLOBAL__N_124display13FBInspectMemoryEP9IOServicebjb':2408,
  '_ZN2XE19wrapDisplay13TCModeEP8OSObjectj':1816,
  '_ZN2XE20display13WMArmUpdateEP9IOService':3432,
  '_ZL15tglHostCSBFatalPKcPKjPv':6504,
  '_ZN2XE21wrapTGLGuCHeaderPanicEPv':3992,
  '_ZN13Display13Boot9normalizeI_fixture':2328,
 }
 for symbol,size in old.items():
  extra.write_text(line(symbol,size));rejects([core,extra])
 # Shared inline bodies can recur; every occurrence must satisfy its budget.
 core.write_text(baseline+line('_shared',128));extra.write_text(line('_shared',256))
 assert module.check(p)['_shared']['bytes']==256
 extra.write_text(line('_shared',4096));rejects(p)
 core.write_text('');extra.write_text('');rejects(p)
 # Optimized data-only objects need no frame report; real code always does.
 for name,source in [('data','int value=7;'),('code','int f(int a){return a+1;}')]:
  cpp=p/(name+'.cpp');obj=p/(name+'.o');cpp.write_text(source)
  subprocess.run(['clang++','-target','x86_64-apple-macos10.15','-O2','-fstack-usage','-c',str(cpp),'-o',str(obj)],check=True)
  if name=='data':
   obj.with_suffix('.su').unlink(missing_ok=True)
   assert module.stack_files([obj])==[]
  else:
   assert module.stack_files([obj])==[obj.with_suffix('.su')]
   obj.with_suffix('.su').unlink()
   try:module.stack_files([obj])
   except ValueError:pass
   else:raise AssertionError('missing report for emitted code accepted')
print('PASS all-TU frame limits: unknown/dynamic frames, 8 old large frames, duplicate bodies, missing records')
