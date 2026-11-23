"""Fixed TGL/IOAccel callbacks on RAM, including the real fence client callback.

Kernel locks, scheduling/time and GPU writes are explicit leaves. Error marking,
stamp comparisons, linked-list retirement and client payload construction execute
native instructions; a callback spy must not supply the expected notification.
"""
import ctypes as C
import hashlib
import json
import os
import signal as signals
from pathlib import Path
import subprocess
import sys
import tempfile

from native_macho import Image, address, put, get
from tgl_recovery_adapter import build

P, U, Q = C.c_void_p, C.c_uint32, C.c_uint64
IO_HASHES = {
    '6ae28339dc55201f5cfc07bc1efc8bc5ec988573eb4e27016225e7fdd40e2777': 'BigSur',
    '5748160caf228eddf7eade869d9f979be1493ffa0f12d0a20c0cd7e5997a52fa': 'Sonoma',
    '1e4f4ca51610c5ff75477e318ef957e23afa073b012478f6eb918592a8a39134': 'Monterey',
    '6834761065e7875af30eab137ef83f3c00708d8edf8c89fd755992b6ddfc031c': 'Ventura',
    '0e73f4e1e293211d094993fa67ee074787444a443cc3aef425a17cef55e05218': 'Sequoia',
    '37e563d6cf3626101f30b1727a4f60dec02152dff9c74c0269c30c213a7320c8': 'Tahoe',
}


# Faults in the final producer stores must fail even if the ring has valid
# command bytes and every client callback would work with an external stamp.
DONOR_MUTANTS = {
    'drop_load': (0x3aa39, bytes.fromhex('c7040801000000'), b'\x90'*7),
    'clear_force_restore': (0x3a7ca, bytes.fromhex('83c804'), bytes.fromhex('83c800')),
}


def emitted_stamp(words, engine, stamp_address):
    # Decode the post-sync write addressed to this channel. Compute emits
    # additional PIPE_CONTROL writes to scratch which must not retire a fence.
    values = []
    i = 0
    while i < len(words):
        header = words[i]
        if header in (0, 0x03800000):  # MI_NOOP or MI_USER_INTERRUPT
            i += 1
            continue
        if engine == 1:
            assert header == 0x7a000004 and i+6 <= len(words)
            flags, low, high, data, upper = words[i+1:i+6]
            immediate = ((flags >> 14) & 3) == 1
            destination = (high << 32) | (low & ~3)
            i += 6
        else:
            assert header & 0xff80003f == 0x13000003 and i+5 <= len(words)
            low, high, data, upper = words[i+1:i+5]
            immediate = ((header >> 14) & 3) == 1
            destination = (high << 32) | (low & ~7)
            i += 5
        if immediate and destination == stamp_address:
            assert upper == 0
            values.append(data)
    assert len(values) == 1, (engine, hex(stamp_address), words)
    return values[0]


def replay(accelerator, ioaccel, library, donor_mutant=None):
    assert hashlib.sha256(Path(accelerator).read_bytes()).hexdigest() == \
        'bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
    family = IO_HASHES[hashlib.sha256(Path(ioaccel).read_bytes()).hexdigest()]
    tgl, io = Image(accelerator), Image(ioaccel)
    guard = C.c_uint64(0x31abbc)
    page_shift = U(12)
    wakes, delivered = [], []
    control={'block':None}
    quiet = io.callback(Q, (), lambda: 0)
    clock = io.callback(Q, (), lambda: 177)
    wake = io.callback(None, (P, U, U), lambda key, one, result: wakes.append((key, one, result)))
    def send(port, result, values, count):
        delivered.append([port, result, list((Q*count).from_address(values))])
        return 0
    send_cb = io.callback(U, (P, U, P, U), send)
    block=io.callback(U, (), lambda: control['block']() if control['block'] else 0)
    waiting=io.callback(U,(),lambda:0xffffffff)  # THREAD_WAITING from assert_wait.
    clock_ns = [177]
    wait_reports = []
    def read_uptime(output):
        clock_ns[0] += 1
        output.contents.value = clock_ns[0]
    uptime = io.callback(None, (C.POINTER(Q),), read_uptime)
    reporter = io.callback(None, (P, Q, Q), lambda obj, key, amount: wait_reports.append((key, amount)))
    nanos=io.callback(None,(Q,C.POINTER(Q)),lambda n,out: setattr(out.contents,'value',n))
    def add16(value,pointer):
        old=pointer.contents.value
        pointer.contents.value=old+value
        return old
    atomic16=io.callback(C.c_int16,(C.c_int16,C.POINTER(C.c_int16)),add16)
    def add32(pointer,amount):
        old=pointer.contents.value
        pointer.contents.value=old+amount
        return old
    increment=io.callback(C.c_int32,(C.POINTER(C.c_int32),),lambda p:add32(p,1))
    decrement=io.callback(C.c_int32,(C.POINTER(C.c_int32),),lambda p:add32(p,-1))
    bindings = {name: quiet for name in (
        '_IOLockLock', '_IOLockUnlock', '_lck_mtx_lock', '_lck_mtx_unlock',
        '_kernel_debug', '__os_log_internal', '_clock_interval_to_deadline',
        '_assert_wait_deadline', '_assert_wait_timeout', '_IOLog',
        '__ZN16IOSimpleReporter14incrementValueEyx',
        '__ZN15IORegistryEntry18getRegistryEntryIDEv',
        '__ZN12IOStatistics23countIOThreadUsageEventEjj')}
    bindings.update({'___stack_chk_guard': C.addressof(guard), '_page_shift': C.addressof(page_shift),
                     '_thread_wakeup_prim': wake, '_mach_absolute_time': clock,
                     '__ZN16IOSimpleReporter14incrementValueEyx':reporter,
                     '_thread_block':block,'_clock_get_uptime':uptime,
                     '_assert_wait_deadline':waiting,
                     '_absolutetime_to_nanoseconds':nanos,
                     '_OSAddAtomic16':atomic16,
                     '_OSIncrementAtomic':increment,'_OSDecrementAtomic':decrement,
                     '_bzero':C.cast(C.CDLL(None).bzero,P).value,
                     '_memset':C.cast(C.CDLL(None).memset,P).value,
                     '_IOMalloc':C.cast(C.CDLL(None).malloc,P).value,
                     '_IOMallocData':C.cast(C.CDLL(None).malloc,P).value,
                     '_IOFree':C.cast(C.CDLL(None).free,P).value,
                     '_IOFreeData':C.cast(C.CDLL(None).free,P).value,
                     '__ZN15OSMetaClassBase12safeMetaCastEPKS_PK11OSMetaClass':quiet,
                     '__ZN12IOUserClient17sendAsyncResult64EPyiS0_j': send_cb})
    io.link((tgl,), bindings)
    tgl.link((io,), bindings)
    if donor_mutant:
        offset, original, replacement = DONOR_MUTANTS[donor_mutant]
        assert tgl.mem[offset:offset+len(original)] == original, 'mutation anchor missing'
        tgl.mem[offset:offset+len(original)] = replacement
    tgl.override('__ZL14_SafeForceWakebj',quiet)
    # These leaves only record statistics/trace identity, not the result path.
    for image, name in ((tgl, '__ZN16IntelAccelerator9getGPUTimeEv'),
                        (io, '__ZN22IOGraphicsAccelerator214getRegistryIDEv')):
        if name in image.symbols:
            image.override(name, clock)
    objects = []
    def buf(n):
        b = C.create_string_buffer(n)
        objects.append(b)
        return b
    accel, event, pointers, stamps, waits = [buf(n) for n in (0x2000, 0x1000, 64*8, 64*64, 64*4)]
    event_vt=buf(0x300)
    C.memmove(event_vt,tgl.ptr('__ZTV19IGAccelEventMachine')+16,0x300)
    # Electrical stamp-interrupt registration is covered by the separate IRQ
    # replay; this RAM waiter has no kernel interrupt bridge.
    for slot in (0x240,0x248):
        put(event_vt,slot,quiet)
    put(event, 0, address(event_vt))
    put(event, 0x10, address(accel))
    put(event, 0x28, address(pointers))
    put(event, 0x30, 64, 'I')
    put(event, 0x48, address(waits))
    put(accel, 0x380, address(event))
    task,mapped=buf(0x300),buf(0x40)
    put(accel,0x150,address(task))
    put(task,0x288,address(mapped))
    put(mapped,0x38,address(stamps))
    accelerator_vt=buf(0x900)
    put(accel,0,address(accelerator_vt))
    for slot in (0x850,0x858):
        put(accelerator_vt,slot,quiet)  # Kernel/main-context lock boundaries.
    stats = buf(0x80)
    put(accel, 0x388, address(stats))
    for i in range(64):
        put(pointers, i*8, address(stamps)+64*i)
    signal_update=tgl.function('__ZN16IntelAccelerator17signalStampUpdateERK8IGBitSetILm64EE',None,P,P)
    def signal(_,index,value):
        assert get(stamps,index*64)==value
        bits=Q(1<<index)
        signal_update(accel,C.byref(bits))
        count=len(wakes)
        control['fence_irq']=False
        signal_update(accel,C.byref(bits))
        assert len(wakes)==count and not control['fence_irq']
    test_event = io.function('__ZN24IOAccelEventMachineFast217testEventUnlockedEP12IOAccelEvent', C.c_bool, P, P)
    notifier=io.function('__ZN19IOAccelFenceMachine19eventfence_notifierEP22IOInterruptEventSourcei',None,P,P,U)
    io.override('__ZN22IOGraphicsAccelerator218kickFenceCollectorEv',quiet)  # Kernel work scheduling only.
    mark = io.function('__ZN19IOAccelFenceMachine26setErrorOnEventFenceWithIDEP17IOAccelSubmitter2yj', None, P, P, Q, U)
    fence_machine, submitter, port = buf(0x100), buf(0x800), buf(0x60)
    put(fence_machine, 0x10, address(accel))
    put(fence_machine, 0x18, address(event))
    put(accel,0x390,address(fence_machine))
    source,source_vt=buf(0x20),buf(0x240)
    put(source,0,address(source_vt))
    put(source_vt,0x1d8,io.callback(None,(P,U,U,U),lambda *_:control.__setitem__('fence_irq',True)))
    put(source_vt,0x1d0,quiet)
    put(source_vt,0x218,quiet)
    put(fence_machine,0x30,address(source))
    put(fence_machine,0x38,address(source))
    put(fence_machine, 0x68, address(fence_machine)+0x60)
    rows = []
    def fence(index, target):
        f = buf(0xe0)
        put(f, 0, io.ptr('__ZTV17IOAccelBlockFence')+16)
        for start in (0x28, 0x68):
            C.memset(address(f)+start, 255, 64)
            put(f, start, index | (target << 32))
        put(f, 0xd8, address(port))
        put(f, 0xc8, 0x4321)
        put(f, 0xd0, 0x8765)
        return f
    def collect(f):
        head = (C.c_char*16).from_address(address(submitter)+0x528)
        put(head, 0, address(f))
        put(head, 8, address(f)+0xa8)
        put(f, 0xb0, address(head))
        put(fence_machine, 0x80, 1, 'I')
        put(fence_machine,0x40,address(submitter))
        put(fence_machine,0x48,address(submitter)+0x538)
        put(submitter,0x538,0)
        put(submitter,0x540,address(fence_machine)+0x40)
        return head
    def process(machine,head,_):
        before=get(head,0,'Q')
        notifier(machine,None,0)
        return bool(before and not get(head,0,'Q'))
    for index, engine in ((25, 'Compute'), (32, 'BCS'), (36, 'VCS0'), (44, 'VCS2'), (52, 'VECS')):
        f = fence(index, 10)
        head = collect(f)
        before = len(delivered)
        assert not process(fence_machine, head, False)
        assert len(delivered) == before and get(head, 0, 'Q') == address(f)
        put(stamps, index*64, 10, 'I')  # GPU boundary: real command retirement.
        signal(event, index, 10)
        assert process(fence_machine, head, False)
        assert len(delivered) == before+1 and delivered[-1][2][6] == 0, delivered
        assert get(head, 0, 'Q') == 0
        assert not process(fence_machine, head, False) and len(delivered) == before+1
        rows.append({'case': 'normal-'+engine, 'client_error': delivered[-1][2][6]})
        failed = fence(index, 11)
        head = collect(failed)
        put(submitter, 0x528, address(failed))
        mark(fence_machine, submitter, address(failed), 3)
        assert get(failed, 0xc0) == 3
        assert not process(fence_machine, head, False) and len(delivered) == before+1
        put(stamps, index*64, 11, 'I')
        signal(event, index, 11)
        assert process(fence_machine, head, False)
        assert len(delivered) == before+2 and delivered[-1][2][6] == 3, delivered
        rows.append({'case': 'failed-then-retired-'+engine, 'client_error': delivered[-1][2][6]})
        assert test_event(event, C.byref(failed, 0x68))
    # Keep native context reconstruction and descriptor/LOAD stores on the
    # path to the RAM BAR. VM placement, scheduling/idle accounting, interrupt
    # delivery and the hardware response are external inputs to this replay.
    tgl.override('__ZN18IGAccelFIFOChannel22resetHardwareAndReplayEv',C.cast(library.recover,P).value)
    assert library.install(tgl.base,tgl.jump(C.cast(library.tglResetPollBridge,P).value))
    for symbol in ('__ZN16IntelAccelerator13SafeForceWakeEbj','__ZN16IntelAccelerator13SafeForceWakeEb',
                   '__ZN16IntelAccelerator15encodeDebugInfoE15IGTimeoutReason',
                   '__ZN26IGHardwareCommandStreamer530contextSwitchInterruptOccurredEv',
                   '__ZN12IGScheduler515updateIdleStateE10IGHwCsTypej',
                   '__ZN26IGHardwareCommandStreamer520enableStampInterruptEi'):
        tgl.override(symbol,quiet)
    # The VM supplies ring placement and four page-directory roots. The donor
    # initializes the image, rebuilds commands and submits its descriptor.
    ring_gpu_va = 0x900000
    roots = (0x120001000, 0x120002000, 0x120003000, 0x120004000)
    def page_directories(_, output):
        for i, root in enumerate(roots):
            output[i] = root
    page_dirs = tgl.callback(None, (P, C.POINTER(Q)), page_directories)
    vm, vm_vtable = buf(8), buf(0x150)
    put(vm, 0, address(vm_vtable))
    put(vm_vtable, 0x148, page_dirs)
    put(task, 0x260, address(vm))
    scratch_map = buf(0x40)
    put(task,0x280,address(scratch_map))
    gpu_va = {address(scratch_map):0x400000, address(mapped):0x100000}
    tgl.override('__ZNK14IGMappedBuffer20getGPUVirtualAddressEv',
                 tgl.callback(Q, (P,), gpu_va.__getitem__))
    diagnosis=buf(16)
    C.memmove(diagnosis,b'RAM replay\0',11)
    tgl.override('__ZN20IGHardwareRingBuffer16dumpHangAnalysisEv',io.callback(P,(),lambda:address(diagnosis)))
    reports=[]
    io.override('__ZN22IOGraphicsAccelerator216reportGPURestartEP17IOAccelSubmitter2PKcjy',
                io.callback(None,(P,P,P,U,Q),lambda _,who,text,error,serial:reports.append(error)))
    restart=io.function('__ZN20IOAccelEventMachine215restart_channelEv',None,P)
    finish=io.function('__ZN15IOAccelContext218finish_fence_eventEj',U,P,U)
    # Preserve the virtual call to the exact timeout routine. A returned
    # channel transfers responsibility for a halted streamer to FIFO restart.
    timeout_symbol='__ZN19IGAccelEventMachine12eventTimeoutEi'
    timeout=tgl.function(timeout_symbol,P,P,U)
    slots=[off for off in range(0,len(event_vt),8) if get(event_vt,off,'Q')==tgl.ptr(timeout_symbol)]
    assert len(slots)==1
    def observe_timeout(machine, idx):
        selected=timeout(machine,idx)
        timeout_returns.append((selected,get(mmio,reset_ctl),get(mmio,0x941c)))
        return selected
    put(event_vt,slots[0],io.callback(P,(P,U),observe_timeout))
    native_results=[]
    for engine,index,mask in ((1,25,2),(3,36,0x20),(4,44,0x80),(5,52,0x2000)):
        for outcome in ('retired','stamp_missing','reset_stuck'):
            target=12+10*('retired','stamp_missing','reset_stuck').index(outcome)
            scheduler,streamer,context,active,channel,entries,mmio,regtable,ring,descriptor,pool,shared,command = [
                buf(n) for n in (0xd00,0x1d00,0x200,0x30,0x200,0x120,0x200000,0x20,0x1000,0x79,0x4100,0x40,4096)]
            put(scheduler,0,tgl.ptr('__ZTV12IGScheduler5')+16)
            put(scheduler,0x10,address(accel))
            put(scheduler,0xca0+8*engine,address(streamer))
            put(accel,0x1250,address(scheduler))
            put(accel,0x1300,1<<engine)
            put(accel,0x1240,address(mmio))
            put(accel,0x390,address(fence_machine))
            put(streamer,0,tgl.ptr('__ZTV26IGHardwareCommandStreamer5')+16)
            put(streamer,0x10,address(accel))
            put(streamer,0x20,address(scheduler))
            put(streamer,0x28,address(descriptor))
            put(streamer,0x30,address(active))
            put(streamer,0x68,address(buf(0xc0)))
            put(streamer,0x1c04,0,'I')
            put(scheduler,0xaa0+8*index,address(pool))
            native_desc = tgl.ptr('__ZL11kIGHwCsDesc') + engine * 0x79
            C.memmove(descriptor, native_desc, len(descriptor))
            sq_reg, load_reg = get(descriptor,0x19), get(descriptor,0x1d)
            put(streamer,0x1c3c,1,'B')  # Native reset resubmits the active bank.
            image_map, context_image = buf(0x40), buf(0x3000)
            put(image_map,0x38,address(context_image))
            put(context,0x50,address(accel))
            put(context,0x58,address(task))
            put(context,0x6c,engine,'B')
            put(context,0x98,address(image_map))
            put(context,0xb0,address(ring))
            # Use a distinct context ID per engine and the normal descriptor flags.
            ctx_descriptor = (index << 37) | 0x803319
            put(context,0x89,ctx_descriptor)
            put(accel,0x119c,len(command)//1024,'I')
            put(context_image,0x101c,0x70,'I')
            put(active,0,address(context))
            put(active,0x18,target,'I')
            put(context,0xb8,address(channel))
            put(channel,0,tgl.ptr('__ZTV18IGAccelFIFOChannel')+16)
            put(channel,0x18,address(accel))
            put(channel,0x20,index,'I')
            put(channel,0x130,address(ring))
            put(channel,0x80,address(entries))
            put(channel,0x8c,0x90,'I')
            put(channel,0x98,address(entries))
            put(channel,0xa0,address(entries)+0x120)
            put(entries,0,address(submitter))
            C.memset(address(entries)+0x30,255,64)
            put(entries,0x30,index|(target<<32))
            ring_class = '__ZTV27IGHardwareRingBufferCompute' if engine==1 else '__ZTV25IGHardwareRingBufferMedia'
            put(ring,0,tgl.ptr(ring_class)+16)
            gpu_va[address(shared)] = ring_gpu_va
            put(ring,0x10,address(accel))
            put(ring,0x18,address(context_image))
            put(ring,0x20,address(context))
            put(ring,0x28,0x100000)  # GPU virtual stamp mapping, distinct from RAM.
            put(ring,0x30,address(stamps))
            put(ring,0x38,index,'I')
            put(ring,0x40,engine,'B')
            put(ring,0x50,address(mmio))
            put(ring,0x58,0x203c,'I')
            put(ring,0x70,mask,'I')
            put(ring,0x78,address(regtable))
            put(ring,0x80,address(shared))
            put(ring,0x8c,4096,'I')
            put(ring,0x90,4095,'I')
            put(shared,0x38,address(command))
            for typ in range(6):
                put(ring,0x8b8+8*typ,address(buf(0xb0)))
            put(submitter,0,io.ptr('__ZTV15IOAccelContext2')+16)
            put(submitter,0x5a8,address(accel))
            put(stamps,index*64,target-1,'I')
            put(event,0x90,index,'I')
            restart_type = 2 if engine==1 else 3
            put(event,0x94,restart_type,'I')
            put(event,0x98,address(buf(64*4)))
            event_source,event_vt=buf(0x20),buf(0x200)
            put(event_source,0,address(event_vt))
            put(event_vt,0x1d8,quiet)
            put(event,0x80,address(event_source))
            f=fence(index,target)
            put(entries,0x70,address(f))
            head=collect(f)
            put(submitter,0x528,address(f))
            before=len(delivered)
            event_copy=buf(64)
            C.memset(event_copy,255,64)
            put(event_copy,0,index|(target<<32))
            put(submitter,0x6a8,address(event_copy))
            put(submitter,0x6d8,64,'I')
            state={'time':0}
            reset_ctl=get(descriptor,0x3d)
            timeout_returns=[]
            reset_requests=[]
            def delay():
                state['time']+=10000
                # Model RESET_CTRL write-enable semantics (TGL Vol 2c,
                # pp1113-1114). READY acknowledges context-switch hold only.
                request=get(mmio,reset_ctl)
                if request & 0x10000:
                    put(mmio,reset_ctl,3 if request & 1 else 0,'I')
                domain=get(mmio,0x941c)
                if domain:
                    if not reset_requests:
                        reset_requests.append((domain,get(mmio,reset_ctl),len(timeout_returns)))
                    if outcome!='reset_stuck':
                        put(mmio,0x941c,0,'I')
                        put(mmio,reset_ctl,0,'I')  # Hardware reset clears this engine's request.
                return 0
            control['block']=delay
            library.configure(io.callback(Q,(),lambda:state['time']),io.callback(None,(),delay))
            if engine == 1:
                # A watchdog request (type 2) first compares its last sample
                # with the mapped stamp. Progress cancels this restart even
                # when the final target has not been reached.
                put(event,0x100+24*index,target-2,'I')
                report_count = len(reports)
                restart(event)
                assert not timeout_returns and not reset_requests
                assert len(reports)==report_count and get(mmio,load_reg)==0
                assert get(context_image,0x101c)==0x70 and get(f,0xc0)==0
                put(event,0x100+24*index,target-1,'I')
                put(event,0x90,index,'I')
                put(event,0x94,2,'I')
            report_count = len(reports)
            restart(event)
            assert len(reports)==report_count+1
            assert timeout_returns==[(address(channel),3,0)], 'timeout must return the held channel before GDRST'
            assert reset_requests==[(mask,3,1)], 'FIFO reset must follow the timeout handoff'
            assert get(mmio,reset_ctl)==(3 if outcome=='reset_stuck' else 0)
            assert get(mmio,0x941c)==(mask if outcome=='reset_stuck' else 0)
            load = get(mmio,load_reg)
            image_tail = get(context_image,0x101c)
            restart_tail = get(ring,0x64)
            assert get(stamps,index*64)==target-1
            assert get(scheduler,0xa88,'B')==0
            assert reports[-1]!=0 and get(f,0xc0)==reports[-1], (engine,outcome,reports[-1],get(f,0xc0),
                hex(get(entries,0x70,'Q')),hex(address(f)))
            assert not process(fence_machine,head,False) and len(delivered)==before
            if outcome=='reset_stuck':
                assert load==0 and get(mmio,sq_reg,'Q')==0 and image_tail==0x70
                assert get(channel,0x17c)==1
            else:
                assert load==1, 'reset replay did not publish LOAD'
                assert get(mmio,sq_reg,'Q') == (ctx_descriptor | 4), 'wrong reset descriptor'
                assert get(context_image,0x1024)==ring_gpu_va and get(context_image,0x102c)==1
                for i, root in enumerate(reversed(roots)):
                    high = get(context_image,0x1094+16*i)
                    low = get(context_image,0x109c+16*i)
                    assert ((high << 32) | low) == root
                assert get(channel,0x17c)==0
                assert get(ring,0x64)>0  # Native resubmission actually wrote commands.
                words=list((U*(get(ring,0x64)//4)).from_buffer(command))
                stamp_address = 0x100000 + index*64
                recovery_stamp = emitted_stamp(words,engine,stamp_address)
                assert recovery_stamp==target
                if outcome=='retired':
                    # Conditional input to the notification consumer: the GPU
                    # delivered the emitted stamp. A LOAD alone cannot do so.
                    put(stamps,index*64,recovery_stamp,'I')
                    signal(event,index,recovery_stamp)
                    assert process(fence_machine,head,False)
                    assert delivered[-1][2][6]==reports[-1] and len(delivered)==before+1
                    assert not process(fence_machine,head,False) and len(delivered)==before+1
                    assert finish(submitter,0)==0
            if outcome!='retired':
                # Observe the *actual* blocking caller, with no target stamp
                # supplied to make it return. The child observer bounds the
                # experiment, not native code, and records this as an open gap.
                readfd,writefd=os.pipe()
                pid=os.fork()
                if pid==0:
                    os.close(readfd)
                    signals.alarm(5)
                    blocks=[0]
                    def observe_block():
                        blocks[0]+=1
                        if blocks[0]==16:
                            record={'blocked':True,'blocks':blocks[0],
                                    'waiters':get(waits,index*4),
                                    'notifications':len(delivered)-before,
                                    'observed':get(stamps,index*64),'target':target}
                            os.write(writefd,json.dumps(record).encode())
                            os._exit(0)
                        return 1  # Kernel THREAD_TIMED_OUT, no GPU progress.
                    control['block']=observe_block
                    put(accel,0xcf0,2,'I')  # Native wait's two deadline stages.
                    result=finish(submitter,0)
                    record = {'blocked': False, 'result': result, 'waiters': get(waits, index*4),
                              'notifications': len(delivered)-before, 'observed': get(stamps, index*64),
                              'target': target}
                    os.write(writefd, json.dumps(record).encode())
                    os._exit(0)
                os.close(writefd)
                data=os.read(readfd,4096)
                os.close(readfd)
                _,status=os.waitpid(pid,0)
                assert status==0 and data,(outcome,status,data)
                gap=json.loads(data)
                assert gap['notifications']==0 and gap['observed']<gap['target']
                if gap['blocked']:
                    assert gap['waiters']==1
                else:
                    assert gap['result']==0xe00002d6 and gap['waiters']==0
            else:
                gap=None
            if outcome == 'stamp_missing':
                # The held asynchronous fence retires only when its emitted
                # stamp arrives. A pending synchronous caller owns no fence.
                put(stamps, index*64, recovery_stamp, 'I')
                signal(event, index, recovery_stamp)
                assert process(fence_machine, head, False)
                assert len(delivered) == before+1 and delivered[-1][2][6] == reports[-1]
                assert not process(fence_machine, head, False)
                assert finish(submitter, 0) == 0 and get(waits, index*4) == 0
            following=False
            if outcome!='reset_stuck':
                # Normal submitStampCommand is a separate producer from the
                # recovery replay. Its scheduler enqueue is a capacity/CPU
                # boundary here; it cannot supply a completion notification.
                enqueue=[]
                def push(_,ctx,tail,begin,pending,urgent):
                    enqueue.append((ctx,tail,begin,pending,urgent))
                    return True
                tgl.override('__ZN12IGScheduler54pushEP17IGHardwareContextjjbb',
                    io.callback(C.c_bool,(P,P,U,U,C.c_bool,C.c_bool),push))
                put(accel,0xf10,address(buf(0x270)))
                put(accel,0xc78,2,'I')
                put(event,0xfc+24*index,target,'I')
                io.function('__ZN24IOAccelEventMachineFast214incrementStampEi',None,P,U)(event,index)
                old_tail=get(ring,0x64)
                next_fence=fence(index,target+1)
                next_head=collect(next_fence)
                tgl.function('__ZN18IGAccelFIFOChannel18submitStampCommandEv',None,P)(channel)
                assert len(enqueue)==1 and enqueue[0][0]==address(context)
                assert get(channel,0x178)==target+1 and get(ring,0x64)>old_tail
                next_words=list((U*((get(ring,0x64)-old_tail)//4)).from_buffer(command,old_tail))
                emitted=emitted_stamp(next_words,engine,stamp_address)
                assert emitted==target+1
                put(stamps,index*64,emitted,'I')
                signal(event,index,emitted)
                count=len(delivered)
                assert process(fence_machine,next_head,False) and len(delivered)==count+1
                assert delivered[-1][2][6]==0 and not process(fence_machine,next_head,False)
                put(event_copy,0,index|((target+1)<<32))
                wait_reports.clear()
                assert finish(submitter,0)==0 and get(waits,index*4)==0
                assert wait_reports == [(45, 0)], wait_reports
                following=True
                # A completion arriving inside native waitForStamp must update
                # the shared observed cache, as the unpatched caller does.
                # Generate a second normal command through the real emitter;
                # its decoded address/value is the only GPU model write input.
                io.function('__ZN24IOAccelEventMachineFast214incrementStampEi',None,P,U)(event,index)
                next_tail=get(ring,0x64)
                tgl.function('__ZN18IGAccelFIFOChannel18submitStampCommandEv',None,P)(channel)
                waiting_words=list((U*((get(ring,0x64)-next_tail)//4)).from_buffer(command,next_tail))
                waiting_stamp=emitted_stamp(waiting_words,engine,stamp_address)
                assert len(enqueue)==2 and waiting_stamp==target+2 and get(channel,0x178)==target+2
                put(event_copy,0,index|((target+2)<<32))
                put(event,0xf8+24*index,target+1,'I')
                put(event,0x94,0,'I')
                def complete_wait():
                    clock_ns[0] += 1000
                    put(stamps, index*64, waiting_stamp, 'I')
                    return 0
                wait_reports.clear()
                control['block'] = complete_wait
                assert finish(submitter,0)==0
                assert get(event,0xf8+24*index)==target+2
                assert get(waits,index*4)==0
                assert wait_reports == [(43, 1001), (45, 1001)], wait_reports
            native_results.append({'engine':engine,'outcome':outcome,'native_replay':load,
                                   'restart_type':restart_type,'fence_error':get(f,0xc0),
                                   'image_tail_after_restart':image_tail,'ring_tail_after_restart':restart_tail,
                                   'reset_failures':get(channel,0x17c),'client_callbacks':len(delivered)-before,
                                   'unreached_waiter':gap,
                                   'async_pending':outcome=='reset_stuck',
                                   'late_retirement_once':outcome=='stamp_missing','following_stamp':following})
            # A following caller does not inherit a fabricated completed value.
            assert get(stamps,index*64)==(target+2 if outcome!='reset_stuck' else target-1)
    control['block']=None
    return {'family': family, 'cases': rows, 'native_restart':native_results,
            'client_callbacks': len(delivered), 'stamp_wakeups': len(wakes)}


if __name__ == '__main__':
    mutant = sys.argv[3] if len(sys.argv) > 3 else None
    with tempfile.TemporaryDirectory(prefix='xe-native-chain-') as temporary:
        result = replay(*sys.argv[1:3], build(Path(temporary)), mutant)
        if not mutant:
            result['mutants'] = []
            for name in DONOR_MUTANTS:
                run = subprocess.run([sys.executable, __file__, *sys.argv[1:3], name],
                                     text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
                expected = 'wrong reset descriptor' if name=='clear_force_restore' else 'reset replay did not publish LOAD'
                assert run.returncode != 0 and expected in run.stdout, (name, run.stdout)
                result['mutants'].append(name)
        print(json.dumps(result, indent=2))
