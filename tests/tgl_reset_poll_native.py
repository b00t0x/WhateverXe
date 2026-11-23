"""Production installer/bridge/helper -> exact donor reset and native exits.

The RAM MMIO model acknowledges the selected domain at a chosen delay. Native
forcewake and scheduler replay are boundary counters, never completion or IRQ
producers. The actual donor controls whether either exit reaches those leaves.
"""
import ctypes as C
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from native_macho import Image, address, get, put

P, U, Q = C.c_void_p, C.c_uint32, C.c_uint64
root = Path(__file__).resolve().parents[1]
donor = Path(sys.argv[1])
assert hashlib.sha256(donor.read_bytes()).hexdigest() == \
    'bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
from tgl_recovery_adapter import build
mutations={
    'truncate_domain':('status&mask','status&mask&0xff'),
    'timeout_as_success':('return false;','return true;'),
    'omit_clock_restore':('io.write(0x4a08,0x40000);',''),
    'omit_scratch':('if(scratch&1)','if(false)'),
    'wait_unrelated_domain':('if(!(status&mask))','if(!status)'),
}
mutant=sys.argv[2] if len(sys.argv)>2 else None
with tempfile.TemporaryDirectory(prefix='xe-reset-poll-') as temporary:
    headers={}
    if mutant:
        text=(root/'WhateverXe/kern_tgl_reset_poll.hpp').read_text()
        old,new=mutations[mutant]
        assert old in text, 'mutation anchor missing'
        headers['kern_tgl_reset_poll.hpp']=text.replace(old,new)
    library = build(Path(temporary),headers=headers)
    def case(engine, mask, ack, native=False, stuck_clock=False, flags=0, peer=False):
        image = Image(donor)
        mmio, ring, accel, scheduler, vt, context, registers, slot = [
            C.create_string_buffer(n) for n in (0x200000,0x1000,0x2000,0x20,0x200,0xd0,0x20,0xb0)]
        calls = {'delays':0, 'native_delays':0, 'replay':0, 'wake':[], 'time':0}
        def delay():
            calls['delays'] += 1
            if not stuck_clock:
                calls['time'] += 10000
            if peer:put(mmio,0x941c,get(mmio,0x941c)|0x40000000,'I')
            if ack is not None and calls['delays'] >= ack:
                put(mmio, 0x941c, get(mmio, 0x941c)&~mask, 'I')
        delaycb = image.callback(None, (), delay)
        def native_delay():
            calls['native_delays']+=1
            delay()
        native_delaycb=image.callback(None, (),native_delay)
        nowcb = image.callback(Q, (), lambda: calls['time'])
        quiet = image.callback(Q, (), lambda: 0)
        replay = image.callback(None, (P, U, P), lambda *_: calls.__setitem__('replay', calls['replay']+1))
        fw = image.callback(None, (P, C.c_bool, U), lambda _, on, domain: calls['wake'].append(on))
        image.link(bindings={'_assert_wait_timeout':quiet,'_thread_block':native_delaycb,'_IOLog':quiet,'_IOSleep':quiet})
        for symbol in ('__ZN16IntelAccelerator13SafeForceWakeEbj','__ZN16IntelAccelerator13SafeForceWakeEb'):
            image.override(symbol, fw)
        put(ring, 0x10, address(accel))
        put(ring, 0x40, engine, 'B')
        put(ring, 0x50, address(mmio))
        put(ring, 0x70, mask, 'I')
        put(ring, 0x78, address(registers))
        put(ring, 0x8b8+8*engine, address(slot))
        separate_writes=C.create_string_buffer(0x200000)
        put(accel, 0x1240, address(separate_writes) if flags else address(mmio))
        put(slot,0xa4,flags,'I')
        put(accel, 0x1250, address(scheduler))
        put(scheduler, 0, address(vt))
        put(vt, 0x158, quiet)
        put(vt, 0x190, replay)
        put(mmio, 0xcec4, 0x21, 'I')
        if not native:
            library.configure(nowcb, delaycb)
            # User-space dylib placement can exceed rel32 range. A RAM thunk
            # adapts only placement; the actual naked bridge still executes.
            target=image.jump(C.cast(library.tglResetPollBridge,P).value)
            assert library.install(image.base,target)
        result = image.function('__ZN20IGHardwareRingBuffer19resetGraphicsEngineEP17IGHardwareContext', C.c_bool, P, P)(ring, context)
        pending = get(mmio, 0x941c)&mask
        assert calls['wake'] == [True,False], calls
        if flags:
            assert result and calls['replay']==0 and get(mmio,0x941c)==0
            assert get(slot,0xa4)==0 and get(mmio,0xcec4)==0x21
            return calls  # Native early true is deliberately NOT a proven reset.
        assert get(mmio,0x4100)==0xf0f0b1b1 and get(mmio,0x4104)==0xf0f0b2b2 and get(mmio,0xcec4)==0x20
        assert get(mmio,0x4a08)==0x40000
        if native and engine==5:
            assert result and pending and calls['replay']==1
            print('REPRODUCED donor VECS replay before reset acknowledgement')
        else:
            assert result == (ack is not None), (engine,result,calls)
            assert calls['replay'] == int(ack is not None)
            assert not result or not pending
            assert calls['delays'] <= 200, (engine,ack,native,stuck_clock,calls)
            if peer:assert get(mmio,0x941c)&0x40000000
        return calls
    for engine, mask in ((2,4),(3,0x20),(4,0x80),(5,0x2000)):
        for ack in (1,2,200,None):
            case(engine,mask,ack)
        case(engine,mask,None,stuck_clock=True)
        case(engine,mask,2,peer=True)
        for flags in (4,8):case(engine,mask,None,flags=flags)
        print('PASS full native reset exits / bounded timeout / scratch / forcewake',engine)
    case(2,4,2,native=True)
    case(5,0x2000,None,native=True)
    # Every instruction guarded by the installer is necessary, while the
    # kernel's two import relocations must remain admissible.
    for offset in (0,13,83):
        image=Image(donor)
        image.mem[0x4377e+offset]^=1
        assert not library.install(image.base,image.base+0x100)
    print('PASS altered donor refused before patching')
if not mutant:
    for name in mutations:
        run=subprocess.run([sys.executable,__file__,str(donor),name],text=True,
            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
        assert run.returncode!=0 and 'assert' in run.stdout.lower() and 'mutation anchor missing' not in run.stdout,(name,run.stdout)
        print('CAUGHT reset-domain semantic mutant:',name)
