"""Execute the real recovery wrapper and helper with exact-donor ABI spies."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
start = source.index('void XE::wrapTGLRecovery(void *channel) {')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
entry = source[start:end]
header = (root / 'WhateverXe/kern_tgl_recovery.hpp').read_text()
select = 'result.resetOK && result.engine==2 && affected'
submit = 'stamp.submit(affected);'
wait = 'stamp.wait(ring,&result.observedStamp,result.targetStamp,1000000)'
account = 'if (timedOut) ++field<uint32_t>(affected,0x17C);'
owner = 'owner=field<void *>(channel,0x18);\n\t\tvoid *events'
mutations = {
    'equivalent': (select, 'affected && 2==result.engine && result.resetOK'),
    'wrong_engine': (select, select.replace('==2', '==0')),
    'stamp_after_failed_reset': (select, 'result.engine==2 && affected'),
    'omit_submit': (submit, ''),
    'duplicate_submit': (submit, submit+'\n'+submit),
    'wrong_timeout': (wait, wait.replace('1000000', '100000')),
    'reverse_result': (wait, '!'+wait),
    'omit_timeout_account': (account, ''),
    'stale_event_owner': (owner, 'void *events'),
}
with tempfile.TemporaryDirectory(prefix='xe-tgl-recovery-') as directory:
    out = Path(directory)
    (out/'recovery_entry.hpp').write_text(entry)
    for name in ('production', *mutations):
        text = header
        if name in mutations:
            old, new = mutations[name]
            assert header.count(old)==1, old
            text = header.replace(old,new)
        (out/'kern_tgl_recovery.hpp').write_text(text)
        exe = out/'test'
        subprocess.run(['clang++','-x','c++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer','-I'+str(out),str(root/'tests/tgl_recovery_fixture.inc'),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if name in ('production','equivalent'):
            assert run.returncode==0,run.stdout
            print('PASS actual wrapper/helper:',name)
        else:
            assert run.returncode!=0 and 'assert' in run.stdout.lower(),(name,run.stdout)
            print('CAUGHT semantic mutant:',name)
