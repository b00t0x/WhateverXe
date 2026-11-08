#!/usr/bin/env python3
import plistlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_compat_version import check


class CompatVersionTests(unittest.TestCase):
    def validate(self, current, minimum, libraries=None):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'Info.plist'
            path.write_bytes(plistlib.dumps({
                'CFBundleIdentifier': 'org.b00t0x.AppleIntelTGLCompat',
                'CFBundleVersion': current, 'OSBundleCompatibleVersion': minimum,
                'OSBundleLibraries': libraries or {}}))
            check(path)

    def test_release_bump_preserves_old_clients(self):
        for current in ['0.5.0', '0.6.0', '0.7.0', '1.0.0']:
            with self.subTest(current=current):
                self.validate(current, '0.5.0')

    def test_compatibility_floor_bump_rejects_old_clients(self):
        with self.assertRaisesRegex(ValueError, 'no longer accepts'):
            self.validate('0.6.0', '0.6.0')

    def test_provider_too_old(self):
        with self.assertRaisesRegex(ValueError, 'no longer accepts'):
            self.validate('0.4.0', '0.4.0')

    def test_efi_only_plugin_is_not_an_auxkc_dependency(self):
        with self.assertRaisesRegex(ValueError, 'EFI-only WEX'):
            self.validate('0.8.0', '0.5.0', {'org.b00t0x.WhateverXe': '0.8.0'})


if __name__ == '__main__':
    unittest.main()
