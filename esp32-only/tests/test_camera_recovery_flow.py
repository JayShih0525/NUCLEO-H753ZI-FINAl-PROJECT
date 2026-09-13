"""Exercise main's retry limits and cleanup with fake hardware, no COM needed."""
import argparse
import json
import itertools
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import MagicMock, Mock, patch

from host import pqc_camera_demo as camera
from host.serial_protocol import ProtocolError


class CameraRecoveryFlowTests(unittest.TestCase):
    def run_failure(self, limit=2, auth_failure=None, tcp=False):
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            trace = Path(directory) / 'trace.jsonl'
            args = argparse.Namespace(port='FAKE', baud=921600, mode='record', host='192.0.2.1' if tcp else None, tcp_port=9000,
                seconds=60, memory_every=0, max_recoveries=limit,
                inject_camera_timeout_at=0, rekey_every=10, output=None,
                diagnostics=trace, skip_device_selftest=True, display=False)
            cv2 = Mock()
            stack.enter_context(patch.dict('sys.modules', cv2=cv2))
            def replace(name, **kwargs):
                return stack.enter_context(patch.object(camera, name, **kwargs))
            replace('parse_arguments', return_value=args)
            replace('load_trusted_key', return_value=b'trusted')
            connection = MagicMock()
            connection.__enter__.return_value.readline.side_effect = itertools.cycle([
                b'OK rekey_every=10\n', b'OK camera_mode=STREAM\n'])
            uart = replace('camera_connection', return_value=connection)
            tcp_factory = replace('TcpConnection', return_value=connection)
            startup = replace('capture_startup')
            replace('request_info', return_value='INFO')
            replace('request_snapshot', return_value={'BOOT_INFO': {'boot_id': 'boot'}})
            establish = replace('establish_session', side_effect=[
                (b'pk0', b'ct0', b'key0', 1),
                auth_failure or (b'pk1', b'ct1', b'key1', 2),
                (b'pk2', b'ct2', b'key2', 3)])
            # Keep real recover_camera orchestration; mock only its UART sync.
            sync = stack.enter_context(patch('host.camera_recovery.synchronize_camera'))
            receive = replace('request_encrypted_frame', side_effect=TimeoutError('tag.header 3/4'))
            decode = replace('decode_jpeg')
            sign = replace('test_device_signature')
            stack.enter_context(patch('builtins.print'))
            expected = ProtocolError if auth_failure else TimeoutError
            with self.assertRaises(expected):
                camera.main()
            events = [json.loads(line) for line in trace.read_text(encoding='utf-8').splitlines()]
            names = [e['event'] for e in events]
            self.assertIn('run_error', names)
            self.assertNotIn('run_complete', names)
            self.assertNotIn('camera_verified', names)
            decode.assert_not_called()
            sign.assert_not_called()
            connection.__exit__.assert_called_once()
            cv2.destroyAllWindows.assert_called_once()
            if tcp:
                uart.assert_not_called()
                startup.assert_not_called()
                tcp_factory.assert_called_once()
                connection.__enter__.return_value.reset_output_buffer.assert_not_called()
            self.assertEqual(list(Path(directory).glob('*.avi')), [])
            return events, receive.call_count, establish.call_count, sync.call_count

    def test_third_timeout_stops_after_two_recoveries(self):
        events, reads, handshakes, syncs = self.run_failure()
        self.assertEqual((reads, handshakes, syncs), (3, 3, 2))
        self.assertEqual(sum(e['event'] == 'recovery_complete' for e in events), 2)
        stopped = next(e for e in events if e['event'] == 'recovery_stopped')
        self.assertEqual(stopped['reason'], 'recovery_limit_reached')
        self.assertEqual(stopped['attempts'], 2)

    def test_zero_budget_never_synchronizes_or_reauthenticates(self):
        events, reads, handshakes, syncs = self.run_failure(limit=0)
        self.assertEqual((reads, handshakes, syncs), (1, 1, 0))
        self.assertFalse(any(e['event'] == 'recovery_start' for e in events))

    def test_tcp_timeout_stops_without_uart_recovery(self):
        events, reads, handshakes, syncs = self.run_failure(tcp=True)
        self.assertEqual((reads, handshakes, syncs), (1, 1, 0))
        stopped = next(e for e in events if e['event'] == 'recovery_stopped')
        self.assertEqual(stopped['reason'], 'tcp_reconnect_required')

    def test_identity_failure_stops_without_next_capture(self):
        events, reads, handshakes, syncs = self.run_failure(
            auth_failure=ProtocolError('Device authentication rejected: invalid proof'))
        self.assertEqual((reads, handshakes, syncs), (1, 2, 1))
        self.assertFalse(any(e['event'] == 'recovery_complete' for e in events))
        failure = next(e for e in events if e['event'] == 'recovery_failed')
        self.assertEqual(failure['error_type'], 'ProtocolError')

    def test_session_key_confirmation_failure_stops(self):
        events, reads, handshakes, syncs = self.run_failure(
            auth_failure=ProtocolError('Session key confirmation failed'))
        self.assertEqual((reads, handshakes, syncs), (1, 2, 1))
        self.assertFalse(any(e['event'] == 'recovery_complete' for e in events))
