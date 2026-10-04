import contextlib
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from host import trust_store as store
from host import trust_manage as manage
from host.device_auth import load_trusted_key, protocol_trusted_key
from host.serial_protocol import ProtocolError
from host.live_devices import load_live_devices
from host.multi_camera import load_devices
from host import host_identity


class TrustTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for module, attribute, value in ((store,'ROOT',self.root), (store,'REVOCATIONS',self.root/'revoked.json'),
                                          (host_identity,'ROOT',self.root)):
            mock = patch.object(module, attribute, value)
            mock.start(); self.addCleanup(mock.stop)
        self.keys = [bytes([i])*1312 for i in range(1,6)]
        self.paths = [self.root/f'camera{i}.pub' for i in range(1,6)]
        for path,key in zip(self.paths,self.keys): path.write_bytes(key)
        self.config = self.root/'devices.json'
        self.header = self.root/'host_trust.h'

    def cli(self, *args):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return manage.main(['--config',str(self.config),'--header',str(self.header),*args])

    def add(self, index=0, name='camera1'):
        return self.cli('device-add','--name',name,'--public-key',str(self.paths[index]),
                        '--fingerprint',store.fingerprint(self.keys[index]))

    def test_add_multiple_auto_devices_and_list(self):
        self.add(); self.add(1,'camera2')
        self.assertEqual(len(load_devices(self.config)),2)
        self.assertEqual(len(load_live_devices(self.config)[0]),2)
        self.assertEqual(self.cli('device-list'),0)

    def test_bad_fingerprint_and_duplicate_do_not_mutate_config(self):
        self.add(); before=self.config.read_bytes()
        for args in [('device-add','--name','camera1','--public-key',str(self.paths[1]),'--fingerprint',store.fingerprint(self.keys[1])),
                     ('device-add','--name','renamed','--public-key',str(self.paths[0]),'--fingerprint',store.fingerprint(self.keys[0])),
                     ('device-add','--name','camera2','--public-key',str(self.paths[1]),'--fingerprint','0'*64)]:
            with self.assertRaises(SystemExit): self.cli(*args)
            self.assertEqual(before,self.config.read_bytes())

    def test_revoke_blocks_copied_key_direct_pin_live_and_multi(self):
        self.add(); self.add(1,'camera2')
        self.cli('device-revoke','--fingerprint',store.fingerprint(self.keys[0]))
        copied=self.root/'different-name.pub'; copied.write_bytes(self.keys[0])
        with self.assertRaises(ProtocolError): load_trusted_key(copied)
        with self.assertRaises(ProtocolError): protocol_trusted_key(SimpleNamespace(trusted_key=self.keys[0]))
        with self.assertRaises(ValueError): load_devices(self.config)
        devices,errors=load_live_devices(self.config)
        self.assertEqual([d.name for d,k in devices],['camera2'])
        self.assertEqual(len(errors),1)
        self.assertEqual(load_trusted_key(self.paths[1]),self.keys[1])

    def test_restore_requires_matching_key_and_restores_same_config(self):
        self.add(); before=self.config.read_bytes()
        fp=store.fingerprint(self.keys[0])
        self.cli('device-revoke','--fingerprint',fp)
        with self.assertRaises(SystemExit):
            self.cli('device-restore','--public-key',str(self.paths[1]),'--fingerprint',fp)
        self.assertIn(fp,store.revoked_devices())
        self.cli('device-restore','--public-key',str(self.paths[0]),'--fingerprint',fp)
        self.assertEqual(load_trusted_key(self.paths[0]),self.keys[0])
        self.assertEqual(before,self.config.read_bytes())

    def test_malformed_policy_fails_closed(self):
        store.REVOCATIONS.write_text('{broken')
        with self.assertRaises(ProtocolError): load_trusted_key(self.paths[0])
        with self.assertRaises(ProtocolError): protocol_trusted_key(SimpleNamespace(trusted_key=self.keys[0]))

    def test_revoked_identity_cannot_start_mutual_handshake(self):
        from host.mutual_auth import establish
        from host.serial_protocol import SerialProtocol
        from unittest.mock import Mock
        store.set_revoked(store.fingerprint(self.keys[0]),True)
        wire=Mock()
        protocol=SerialProtocol(wire,trusted_key=self.keys[0],mutual_auth=True,secure_records=True)
        with patch('host.mutual_auth.load_identity',return_value=(b'p'*1312,b's'*2560)):
            with self.assertRaisesRegex(ProtocolError,'revoked'): establish(protocol)
        wire.write.assert_not_called()

    def test_device_add_never_implicitly_restores_revoked_identity(self):
        store.set_revoked(store.fingerprint(self.keys[0]),True)
        with self.assertRaises(SystemExit): self.add()
        self.assertFalse(self.config.exists())

    def test_host_header_removal_changes_real_mutual_auth_allowlist(self):
        from pqcrypto.sign import ml_dsa_44
        from host.mutual_auth import establish
        from host.serial_protocol import SerialProtocol
        from test_mutual_auth import ProtectedBoard
        original=ml_dsa_44.generate_keypair()
        replacement=ml_dsa_44.generate_keypair()
        store.write_host_keys(self.header,[original[0],replacement[0]])
        self.cli('host-revoke','--fingerprint',store.fingerprint(original[0]))
        retained=store.read_host_keys(self.header)
        self.assertEqual(retained,[replacement[0]])
        board=ProtectedBoard(retained[0])
        protocol=SerialProtocol(board,trusted_key=board.pk,inline_rekey=True,mutual_auth=True,secure_records=True)
        with patch('host.mutual_auth.load_identity',return_value=original):
            with self.assertRaisesRegex(ProtocolError,'HOST_NOT_TRUSTED'): establish(protocol)
        with patch('host.mutual_auth.load_identity',return_value=replacement):
            establish(protocol)
        self.assertTrue(board.authorized)

    def test_atomic_replace_failure_preserves_previous_policy(self):
        store.set_revoked(store.fingerprint(self.keys[0]),True)
        before=store.REVOCATIONS.read_bytes()
        with patch('host.trust_store.os.replace',side_effect=OSError('simulated disk error')):
            with self.assertRaises(OSError): store.set_revoked(store.fingerprint(self.keys[1]),True)
        self.assertEqual(before,store.REVOCATIONS.read_bytes())
        self.assertFalse(list(self.root.glob('*.tmp')))

    def test_host_list_add_revoke_and_last_key_guard(self):
        store.write_host_keys(self.header,[self.keys[0]])
        self.assertEqual(self.cli('host-list'),0)
        self.cli('host-add','--public-key',str(self.paths[1]),'--fingerprint',store.fingerprint(self.keys[1]))
        self.assertEqual(store.read_host_keys(self.header),self.keys[:2])
        self.cli('host-revoke','--fingerprint',store.fingerprint(self.keys[0]))
        before=self.header.read_bytes()
        with self.assertRaises(SystemExit): self.cli('host-revoke','--fingerprint',store.fingerprint(self.keys[1]))
        self.assertEqual(before,self.header.read_bytes())

    def test_host_limit_duplicates_wrong_fingerprint_and_unknown_revoke(self):
        store.write_host_keys(self.header,self.keys[:4]); before=self.header.read_bytes()
        for args in [('host-add','--public-key',str(self.paths[4]),'--fingerprint',store.fingerprint(self.keys[4])),
                     ('host-add','--public-key',str(self.paths[0]),'--fingerprint',store.fingerprint(self.keys[0])),
                     ('host-add','--public-key',str(self.paths[4]),'--fingerprint','0'*64),
                     ('host-revoke','--fingerprint','0'*64)]:
            with self.assertRaises(SystemExit): self.cli(*args)
            self.assertEqual(before,self.header.read_bytes())

    def test_bad_header_not_overwritten(self):
        self.header.write_text('#include "unexpected.h"')
        with self.assertRaises(SystemExit): self.cli('host-revoke','--fingerprint',store.fingerprint(self.keys[0]))
        self.assertEqual(self.header.read_text(),'#include "unexpected.h"')

    def test_setup_preserves_other_hosts_and_does_not_restore_revoked_self(self):
        identity=self.root/'identity.json'
        args=['--identity',str(identity),'--header',str(self.header)]
        with contextlib.redirect_stdout(io.StringIO()): host_identity.main(args)
        own=store.read_host_keys(self.header)[0]
        store.write_host_keys(self.header,[own,self.keys[0]])
        with contextlib.redirect_stdout(io.StringIO()): host_identity.main(args)
        self.assertEqual(store.read_host_keys(self.header),[own,self.keys[0]])
        store.write_host_keys(self.header,[self.keys[0]])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit): host_identity.main(args)
        self.assertEqual(store.read_host_keys(self.header),[self.keys[0]])
