import json
import tempfile
import unittest
from pathlib import Path
from host.live_devices import load_live_devices


class LiveDeviceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'a.pub').write_bytes(b'a'*1312)
        (self.root/'b.pub').write_bytes(b'b'*1312)

    def load(self, items):
        path = self.root/'devices.json'
        path.write_text(json.dumps({'devices': items}))
        return load_live_devices(path)

    def test_missing_key_does_not_block_valid_device(self):
        devices, errors = self.load([{'name':'camera1','trust_key':'missing.pub'},
                                    {'name':'camera2','trust_key':'b.pub'}])
        self.assertEqual([d.name for d,k in devices], ['camera2'])
        self.assertEqual(devices[0][1], b'b'*1312)
        self.assertEqual(len(errors), 1)

    def test_disabled_entry_does_not_require_key(self):
        devices, errors = self.load([{'enabled':False}, {'name':'camera1','trust_key':'a.pub'}])
        self.assertEqual(len(devices), 1)
        self.assertFalse(errors)

    def test_all_conflicting_names_skipped_even_with_missing_key(self):
        devices, errors = self.load([{'name':'camera1','trust_key':'a.pub'},
            {'name':'Camera1','trust_key':'missing.pub'}, {'name':'camera2','trust_key':'b.pub'}])
        self.assertEqual([d.name for d,k in devices], ['camera2'])
        self.assertEqual(len(errors), 2)

    def test_duplicate_identity_skips_both_not_first_wins(self):
        devices, errors = self.load([{'name':'camera1','trust_key':'a.pub'},
                                    {'name':'camera3','trust_key':'a.pub'}])
        self.assertFalse(devices)
        self.assertEqual(len(errors), 2)

    def test_invalid_entries_are_isolated(self):
        devices, errors = self.load([None, {'enabled':'false'},
             {'name':'../escape','trust_key':'a.pub'}, {'name':'camera1','trust_key':'a.pub'}])
        self.assertEqual(len(devices), 1)
        self.assertEqual(len(errors), 3)

    def test_document_schema_is_still_fatal(self):
        path = self.root/'devices.json'
        path.write_text('{"devices": "bad"}')
        with self.assertRaises(ValueError):
            load_live_devices(path)
