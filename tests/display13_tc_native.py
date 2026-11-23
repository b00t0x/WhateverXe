#!/usr/bin/env python3
"""Qualify the TC native hook entries, mode-return patches and caller ABI."""
from pathlib import Path
import hashlib,re,subprocess,sys
r=Path(__file__).resolve().parents[1]
h=(r/'WhateverXe/kern_display13_tc_native.hpp').read_text()
s=(r/'WhateverXe/kern_xe.cpp').read_text()
arrays={name:bytes(int(v,0) for v in body.split(',')) for name,body in
        re.findall(r'uint8_t (\w+)\[\]=\{([^}]+)\}',h)}
offsets={name:int(value,0) for name,value in re.findall(r'uint32_t (\w+)Offset=(0x[0-9a-f]+);',h)}
assert len(arrays['innerBefore'])==len(arrays['innerAfter'])==3
assert len(arrays['outerBefore'])==len(arrays['outerAfter'])==24
assert int.from_bytes(arrays['outerAfter'][4:8],'little',signed=True)+0x695ac+8==0x698ed
assert 0x695ac+18+arrays['outerAfter'][17]==0x695fc
install=s[s.index('bool XE::processDisplay13FramebufferTC('):s.index('bool XE::processDisplay13FramebufferOutput(')]
assert '!(adlnOutputWiring.selected()&0x78)' in install
assert 'entries[LightUp].symbol,wrapDisplay13TCLightUp' in install
assert 'entries[SetMode].symbol,wrapDisplay13TCSetMode' in install
assert 'wrapDisplay13TCSignal,' not in install # one common SST signal hook
assert '{innerBeforeOffset,innerBefore,innerAfter,sizeof(innerAfter)}' in install
assert '{outerBeforeOffset,outerBefore,outerAfter,sizeof(outerAfter)}' in install
print('PASS TC native mode-patch destinations, installation scope and single signal dispatcher')
if len(sys.argv)>1:
    path=Path(sys.argv[1]);donor=path.read_bytes()
    assert hashlib.sha256(donor).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
    symbols={line.split()[-1]:int(line.split()[0],16) for line in
             subprocess.check_output(['nm','-n',str(path)],text=True).splitlines()
             if len(line.split())==3 and re.fullmatch('[0-9a-fA-F]+',line.split()[0])}
    entries=re.findall(r'\{"([^"\n]+)",(0x[0-9a-f]+),\{([^}]+)\}\}',h)
    assert len(entries)==18
    for symbol,value,body in entries:
        offset=int(value,0);prefix=bytes(int(v,0) for v in body.split(','))
        assert symbols[symbol]==offset and donor[offset:offset+len(prefix)]==prefix
    for name,offset in offsets.items():
        assert donor[offset:offset+len(arrays[name])]==arrays[name]
    # Independently resolve the native startup and first full DP modeset calls.
    # Byte matching alone must not be mistaken for a call-graph explanation.
    for at,symbol in [
        (0x3fdd2,'__ZN14AppleIntelPort14getPortByIndexEj'),
        (0x3fe3c,'__ZN19AppleIntelPowerWell4initEP31AppleIntelFramebufferController'),
        (0x3ff44,'__ZN31AppleIntelFramebufferController16setupBootDisplayEv'),
        (0x4fec7,'__ZN31AppleIntelFramebufferController14disableDisplayEP21AppleIntelFramebufferP21AppleIntelDisplayPathh'),
        (0x4ffd4,'__ZN31AppleIntelFramebufferController9LightUpDPEP21AppleIntelFramebufferP21AppleIntelDisplayPathPK29IODetailedTimingInformationV2')]:
        assert donor[at]==0xe8 and at+5+int.from_bytes(donor[at+1:at+5],'little',signed=True)==symbols[symbol]
    for name in ['bootPortInit','bootPowerCall','bootSetupCall','modeStopCall','dpLightCall']:
        assert '{'+name+'Offset,'+name+',sizeof('+name+')}' in install
    print('PASS native port initialization before boot Mode acquisition, full stop before DP LightUp')
    # The path error branch calls disableDisplay before returning through its
    # normal unlock path. These are independent of the new outer early return.
    assert donor[0x50004:0x50009]==bytes.fromhex('4585ed7437')
    assert donor[0x5001b:0x50020]==bytes.fromhex('e82cd3ffff')
    print('PASS exact c004fc1a: 18 entries, full TMDS sequence, native cleanup, both mode-return boundaries')

    # Ordinary probe classifies and acquires mode before any DP AUX access.
    # Qualify the virtual slots and direct call targets independently of wrappers.
    for at,expected in [(0x1faad,'ff9078010000'),(0x1fabb,'ff9138010000'),
                        (0x1fb38,'ff9038010000'),(0x1bdbe,'ff9088010000'),
                        (0x1be04,'ff9040010000'),(0x1be1e,'ff9028010000')]:
        assert donor[at:at+6]==bytes.fromhex(expected),hex(at)
    for at,target in [(0x50979,0x1ba02),(0x50b0c,0x51f56),(0x5fb9b,0x1fea8)]:
        assert donor[at]==0xe8 and at+5+int.from_bytes(donor[at+1:at+5],'little',signed=True)==target
    print('PASS native DP mode-before-AUX, training signal/gating/enable slots and SDP/stagger caller ABI')

    # The HPD callbacks can bypass probePortState. Verify their intended
    # native entries and the first port-dependent effects of each callback.
    for symbol,at in [
        ('__ZN31AppleIntelFramebufferController23invokeShortPulseHandlerEjN14AppleIntelPort8PortModeE',0x746d4),
        ('__ZN31AppleIntelFramebufferController20invokeHotplugHandlerEjN14AppleIntelPort8PortModeE',0x748d4)
    ]:
        assert symbols[symbol]==at and donor[at:at+13]==bytes.fromhex('554889e5415741564155415453')
    for at,target in [(0x746f4,0x1b1ea),(0x74757,0x5bedc),(0x748fa,0x1b1ea),
                      (0x7496b,0x1b368),(0x74ae7,0x1d8bc)]:
        assert donor[at]==0xe8 and at+5+int.from_bytes(donor[at+1:at+5],'little',signed=True)==target
    print('PASS native short and long HPD handlers have direct power, HPD and AUX callers')
