import unittest
from unittest.mock import Mock, patch

from host.camera_recovery import synchronize_camera, recover_camera
from host.serial_protocol import ProtocolError


class CameraRecoveryTests(unittest.TestCase):
    def protocol(self, chunks):
        protocol = Mock()
        protocol.serial_port.timeout = 5
        protocol.serial_port.read.side_effect = chunks
        return protocol

    def test_partial_stale_frame_and_fragmented_fresh_marker(self):
        token = 'a' * 32
        marker = b'\nRECOVERED ' + token.encode() + b'\n'
        protocol = self.protocol([b'old ciphertext\x00\xff', marker[:8], marker[8:]])
        with patch('host.camera_recovery.secrets.token_hex', return_value=token):
            synchronize_camera(protocol)
        self.assertEqual(protocol.serial_port.timeout, 5)
        protocol.send_line.assert_called_once_with('\nRECOVER ' + token)
        self.assertNotIn('old ciphertext', str(protocol.trace.call_args_list))

    def test_stale_marker_cannot_complete_recovery(self):
        stale = b'\nRECOVERED ' + b'b' * 32 + b'\n'
        protocol = self.protocol([stale])
        with patch('host.camera_recovery.secrets.token_hex', return_value='a' * 32):
            with self.assertRaises(TimeoutError):
                synchronize_camera(protocol, maximum=len(stale))
        self.assertEqual(protocol.serial_port.timeout, 5)

    def test_silent_port_deadline(self):
        protocol = self.protocol([])
        with self.assertRaises(TimeoutError):
            synchronize_camera(protocol, timeout=0)
        self.assertEqual(protocol.serial_port.timeout, 5)

    def test_disconnect_restores_timeout(self):
        protocol = self.protocol([])
        protocol.serial_port.read.side_effect = OSError('disconnected')
        with self.assertRaises(OSError):
            synchronize_camera(protocol)
        self.assertEqual(protocol.serial_port.timeout, 5)

    def test_trailing_data_rejected(self):
        protocol = self.protocol([b'\nRECOVERED ' + b'a' * 32 + b'\nunexpected'])
        with patch('host.camera_recovery.secrets.token_hex', return_value='a' * 32):
            with self.assertRaises(ProtocolError):
                synchronize_camera(protocol)

    def test_no_authentication_until_boundary_found(self):
        protocol = Mock()
        establish, snapshot = Mock(), Mock()
        with patch('host.camera_recovery.synchronize_camera', side_effect=TimeoutError):
            with self.assertRaises(TimeoutError):
                recover_camera(protocol, 10, establish, snapshot)
        establish.assert_not_called()
        snapshot.assert_not_called()

    def test_auth_failure_is_not_retried_or_accepted(self):
        protocol = Mock()
        establish = Mock(side_effect=ProtocolError('bad identity'))
        with patch('host.camera_recovery.synchronize_camera'):
            with self.assertRaisesRegex(ProtocolError, 'bad identity'):
                recover_camera(protocol, 10, establish, Mock())
        establish.assert_called_once_with(protocol)

    def test_recovery_returns_only_confirmed_new_session(self):
        protocol = Mock()
        session = (b'pk', b'ct', b'newkey', 8)
        with patch('host.camera_recovery.synchronize_camera') as sync:
            result = recover_camera(protocol, 10, Mock(return_value=session), Mock(return_value={'boot': 1}))
        sync.assert_called_once_with(protocol)
        self.assertEqual(result, (session, {'boot': 1}))
        self.assertEqual([c.args[0] for c in protocol.send_line.call_args_list],
                         ['SET_REKEY_INTERVAL 10', 'CAMERA_MODE STREAM'])
