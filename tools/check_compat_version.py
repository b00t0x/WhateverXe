#!/usr/bin/env python3
"""Check that built Compat bundles still accept the shipped framebuffer ABI."""
import argparse
import plistlib
import re
import subprocess
from pathlib import Path

# Undefined imports of the fixed c004fc1a framebuffer donor. Keep independent
# of provider source spelling: an accidentally renamed export breaks loading.
DONOR_IMPORTS = {
    '__ZN17AppleIntelPortHAL13probePortModeEv',
    '__ZN21AppleIntelDisplayPath19DSBEngineBusyStatusE',
    '__ZN31AppleIntelFramebufferController14ReadRegister32Em',
    '__ZN31AppleIntelFramebufferController14ReadRegister64EPVvm',
    '__ZN31AppleIntelFramebufferController15WriteRegister32Emj',
    '__ZN31AppleIntelFramebufferController15WriteRegister64EPVvmy',
    '__ZN31AppleIntelFramebufferController16hwSetupDSBMemoryEv',
    '__ZN31AppleIntelFramebufferController9getPMTNowEv',
}
# Current WEX's processKext resolver and the retained ABI1 client entry.
BINDING_EXPORTS = {'xelpConfigureDisplay', 'xelpProbeCapabilities', 'xelpConfigurePortProbe'}


def check_exports(executable, c_prefix='_'):
    symbols = subprocess.check_output(['nm', '-g', str(executable)], text=True)
    defined = {parts[-1] for line in symbols.splitlines()
               if len(parts := line.split()) >= 3 and parts[-2].upper() != 'U'}
    required = DONOR_IMPORTS | {c_prefix+name for name in BINDING_EXPORTS}
    missing = required - defined
    if missing:
        raise ValueError('Compat is missing client imports: '+', '.join(sorted(missing)))


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r'\d+\.\d+\.\d+', value):
        raise ValueError(f'unsupported Compat version: {value!r}')
    return tuple(map(int, value.split('.')))


def check(path):
    info = plistlib.loads(path.read_bytes())
    if info['CFBundleIdentifier'] != 'org.b00t0x.AppleIntelTGLCompat':
        raise ValueError('wrong Compat bundle identifier')
    if 'org.b00t0x.WhateverXe' in info.get('OSBundleLibraries', {}):
        raise ValueError('Compat must not require EFI-only WEX when building AuxKC')
    # Existing Graphics framebuffer payloads request this ABI independently of
    # the provider's release version. Changing it requires coordinated clients.
    required = (0, 5, 0)
    if not version(info['OSBundleCompatibleVersion']) <= required <= version(info['CFBundleVersion']):
        raise ValueError('Compat no longer accepts the shipped framebuffer dependency 0.5.0')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builds', type=Path, nargs='+')
    args = parser.parse_args()
    for build in args.builds:
        check(build / 'AppleIntelTGLCompat.kext/Contents/Info.plist')
        check_exports(build / 'AppleIntelTGLCompat.kext/Contents/MacOS/AppleIntelTGLCompat')
    print('PASS: built Compat bundles accept the shipped framebuffer dependency')
