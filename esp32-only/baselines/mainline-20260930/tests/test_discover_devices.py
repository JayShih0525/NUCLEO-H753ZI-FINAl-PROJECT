import unittest
import json
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from host.discover_devices import Advertisement, parse_advertisement, resolve_devices
from host.multi_camera import Device
from host.multi_camera import main


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.device = Device('camera1', 'old.invalid', 9000, Path('pinned.pub'), 'a' * 64)

    def test_only_valid_version_fingerprint_and_unicast_ipv4(self):
        info = SimpleNamespace(name='camera', port=9000,
                               properties={b'discovery': b'1', b'fingerprint': b'a' * 64},
                               parsed_addresses=lambda: ['192.168.1.3', '::1', '0.0.0.0', '224.0.0.251', '127.0.0.1'])
        records = parse_advertisement(info)
        self.assertEqual([r.host for r in records], ['192.168.1.3'])
        info.properties[b'fingerprint'] = b'bad'
        self.assertEqual(parse_advertisement(info), [])

    def test_match_preserves_pinned_key_and_ignores_unknown(self):
        records = [Advertisement('untrusted name', '192.168.1.3', 9001, 'a' * 64),
                   Advertisement('camera1', '192.168.1.4', 9000, 'b' * 64)]
        result, = resolve_devices([self.device], records)
        self.assertEqual((result.host, result.port), ('192.168.1.3', 9001))
        self.assertEqual(result.trust_key, self.device.trust_key)
        self.assertEqual(result.fingerprint, self.device.fingerprint)

    def test_missing_or_spoofed_duplicate_fails_closed(self):
        with self.assertRaises(ValueError):
            resolve_devices([self.device], [])
        records = [Advertisement('one', '192.168.1.3', 9000, 'a' * 64),
                   Advertisement('two', '192.168.1.4', 9000, 'a' * 64)]
        with self.assertRaises(ValueError):
            resolve_devices([self.device], records)

    def test_same_endpoint_claiming_two_identities_rejected(self):
        second = Device('camera2', 'other.invalid', 9000, Path('other.pub'), 'b' * 64)
        records = [Advertisement('one', '192.168.1.3', 9000, 'a' * 64),
                   Advertisement('two', '192.168.1.3', 9000, 'b' * 64)]
        with self.assertRaises(ValueError):
            resolve_devices([self.device, second], records)

    def test_real_serviceinfo_api(self):
        from zeroconf import ServiceInfo
        from host.discover_devices import SERVICE
        info = ServiceInfo(SERVICE, 'test.' + SERVICE, port=9000,
                           parsed_addresses=['192.168.1.3'],
                           properties={'discovery': '1', 'fingerprint': 'a' * 64})
        self.assertEqual(parse_advertisement(info)[0].fingerprint, 'a' * 64)

    def test_discover_cli_uses_pinned_key_and_selected_endpoint(self):
        import hashlib
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            key = b'k' * 1312
            (root / 'camera.pub').write_bytes(key)
            config = root / 'devices.json'
            config.write_text(json.dumps({'devices': [dict(name='camera1', host='OLD',
                                                          enabled=False, trust_key='camera.pub')]}))
            record = Advertisement('untrusted', '192.168.1.7', 9000, hashlib.sha256(key).hexdigest())
            with patch('host.discover_devices.discover', return_value=[record]), \
                    patch('host.multi_camera.run_devices', return_value=0) as run, \
                    patch('builtins.print'):
                self.assertEqual(main(['--config', str(config), '--discover']), 0)
                device = run.call_args.args[0][0]
                self.assertEqual(device.host, '192.168.1.7')
                self.assertEqual(device.trust_key, root / 'camera.pub')
