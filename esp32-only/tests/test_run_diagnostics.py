import unittest
from unittest.mock import Mock, patch

from host.run_diagnostics import capture_startup, request_snapshot
from host.serial_protocol import SerialProtocol


class RunDiagnosticTests(unittest.TestCase):
    def test_startup_preserves_garbled_bytes_and_restores_timeout(self):
        port = Mock(timeout=5)
        port.read.side_effect = [b'\xffBO', b'OT\n']
        events = []
        protocol = SerialProtocol(port, events.append)
        with patch('host.run_diagnostics.time.monotonic', side_effect=[0, 0, 0, 0, 0]):
            capture_startup(protocol, maximum=6)
        self.assertEqual(port.timeout, 5)
        self.assertEqual(events[-1]['raw_hex'], b'\xffBOOT\n'.hex())
        self.assertTrue(events[-1]['limit_reached'])
        port.reset_input_buffer.assert_not_called()

    def test_startup_error_preserves_partial_capture(self):
        port = Mock(timeout=5)
        port.read.side_effect = [b'boot', OSError('disconnected')]
        events = []
        with self.assertRaises(OSError):
            capture_startup(SerialProtocol(port, events.append))
        self.assertEqual(port.timeout, 5)
        self.assertEqual(events[-1]['raw_hex'], b'boot'.hex())

    def test_snapshot_commands_and_stage_context(self):
        port = Mock()
        port.readline.side_effect = [
            b'BOOT boot_id=abc reset_reason=1 reset_name=POWERON uptime_ms=4000\n',
            b'CAMERA ready=1 frame_id=123\n',
            b'TX short_writes=2 failures=1 last_requested=16 last_written=3\n']
        events = []
        protocol = SerialProtocol(port, events.append, dict(stage='finalize'))
        with patch('builtins.print'):
            snapshot = request_snapshot(protocol, 'end')
        self.assertEqual(snapshot['BOOT_INFO']['boot_id'], 'abc')
        self.assertEqual(snapshot['TX_INFO']['failures'], '1')
        self.assertEqual(events[-1]['snapshot_stage'], 'end')
        self.assertEqual([c.args[0] for c in port.write.call_args_list],
                         [b'BOOT_INFO\n', b'CAMERA_INFO\n', b'TX_INFO\n'])
