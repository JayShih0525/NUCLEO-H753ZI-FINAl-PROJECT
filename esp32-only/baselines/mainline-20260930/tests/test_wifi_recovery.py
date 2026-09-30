import argparse
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
from host.wifi_recovery import supervise
from host.tcp_connection import TcpTransportError


class WifiRecoveryTests(unittest.TestCase):
    def run_case(self, side_effect):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        args = argparse.Namespace(seconds=60, diagnostics=Path(directory.name) / 'trace.jsonl', display=False)
        run = Mock(side_effect=side_effect)
        with patch('host.wifi_recovery.time.monotonic', side_effect=range(1000)), patch('builtins.print'):
            result = supervise(args, run, Mock())
        return result, run, args

    def test_transport_failure_gets_new_trace_and_remaining_duration(self):
        result, run, args = self.run_case([TcpTransportError('lost'), 0])
        self.assertEqual(result, 0)
        self.assertEqual(run.call_count, 2)
        first, second = [call.args[0] for call in run.call_args_list]
        self.assertNotEqual(first.diagnostics, second.diagnostics)
        self.assertLess(second.seconds, first.seconds)
        self.assertEqual(args.seconds, 60)

    def test_validation_error_never_retries(self):
        with self.assertRaisesRegex(ValueError, 'authentication'):
            self.run_case([ValueError('authentication rejected')])

    def test_local_file_error_never_retries(self):
        with self.assertRaises(PermissionError):
            self.run_case([PermissionError('trace is not writable')])

    def test_expired_duration_stops_retrying(self):
        result, run, _ = self.run_case(TcpTransportError('offline'))
        self.assertEqual(result, 1)
        self.assertGreater(run.call_count, 1)

    def test_socket_timeout_is_wrapped_for_retry(self):
        from host.tcp_connection import TcpConnection
        with patch('host.tcp_connection.socket.create_connection', side_effect=TimeoutError('offline')):
            with self.assertRaises(TcpTransportError):
                TcpConnection('192.0.2.1')

    def test_eof_is_wrapped_for_retry(self):
        from host.tcp_connection import TcpConnection
        with patch('host.tcp_connection.socket.create_connection') as create:
            create.return_value.recv.return_value = b''
            with TcpConnection('192.0.2.1') as connection:
                with self.assertRaises(TcpTransportError):
                    connection.read(4)
            create.return_value.close.assert_called_once()
