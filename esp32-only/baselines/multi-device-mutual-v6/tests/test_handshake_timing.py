import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

from host.device_auth import timing_mark
from host.pqc_host_demo import establish_session


class HandshakeTimingTests(unittest.TestCase):
    def test_single_summary_and_no_protocol_io_added(self):
        protocol = SimpleNamespace(profile=True, trace=Mock())
        result = (b'pk', b'ct', b'key', 1)
        def handshake(p):
            timing_mark(p, 'auth_ready_ms', 10)
            return result
        with patch('host.pqc_host_demo._establish_session', side_effect=handshake), \
             patch('host.device_auth.time.perf_counter', return_value=11), \
             patch('builtins.print') as output:
            self.assertEqual(establish_session(protocol), result)
        protocol.trace.assert_called_once()
        fields = protocol.trace.call_args.kwargs
        self.assertEqual(fields['outcome'], 'ok')
        self.assertEqual(fields['auth_ready_ms'], 1000)
        self.assertIn('total_ms', fields)
        output.assert_called_once()
        self.assertFalse(hasattr(protocol, '_handshake_timings'))

    def test_error_records_partial_summary_and_is_not_swallowed(self):
        protocol = SimpleNamespace(profile=True, trace=Mock())
        with patch('host.pqc_host_demo._establish_session', side_effect=TimeoutError('test')), \
             patch('builtins.print'):
            with self.assertRaises(TimeoutError):
                establish_session(protocol)
        protocol.trace.assert_called_once()
        self.assertEqual(protocol.trace.call_args.kwargs['outcome'], 'error')
        self.assertFalse(hasattr(protocol, '_handshake_timings'))

    def test_disabled_has_no_summary(self):
        protocol = SimpleNamespace(profile=False, trace=Mock())
        with patch('host.pqc_host_demo._establish_session', return_value='result'):
            self.assertEqual(establish_session(protocol), 'result')
        protocol.trace.assert_not_called()
