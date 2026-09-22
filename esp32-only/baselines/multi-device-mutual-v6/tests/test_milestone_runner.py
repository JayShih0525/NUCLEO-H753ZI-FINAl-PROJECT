import io
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch
from pathlib import Path
import hashlib
import run_camera as runner
from host.multi_camera import Device
from host.serial_protocol import ProtocolError


class MilestoneRunnerTests(unittest.TestCase):
    def run_receiver(self, mutual=True, disconnect=False):
        from contextlib import ExitStack
        stop = threading.Event()
        view = Mock()
        output = io.StringIO()
        key = b'p' * 1312
        device = Device('camera1', 'old.invalid', 9000, Path('test.pub'), hashlib.sha256(key).hexdigest())
        endpoint = Device('camera1', '192.168.1.3', 9000, device.trust_key, device.fingerprint)
        captured = []
        def capture(*args, **kwargs):
            captured.append(1)
            if disconnect and len(captured) == 1:
                from host.tcp_connection import TcpTransportError
                raise TcpTransportError('simulated disconnect')
            if len(captured) == (4 if disconnect else 3):
                stop.set()
            return object()
        def info(protocol):
            protocol.mutual_auth = mutual
            protocol.pipeline_rekey = True
        with ExitStack() as stack:
            def mocked(target, **kwargs):
                return stack.enter_context(patch(target, **kwargs))
            mocked('host.host_identity.load_identity', return_value=(b'', b''))
            stack.enter_context(patch.object(stop, 'wait', return_value=False))
            mocked('host.device_auth.load_trusted_key', return_value=key)
            mocked('host.discover_devices.discover', return_value=[SimpleNamespace(fingerprint=device.fingerprint)])
            mocked('host.discover_devices.resolve_devices', return_value=[endpoint])
            connection = mocked('host.tcp_connection.TcpConnection')
            mocked('host.pqc_host_demo.request_info', side_effect=info)
            handshake = mocked('host.pqc_host_demo.establish_session', return_value=(b'', b'', b'k' * 32, 1))
            mocked('host.pqc_camera_demo.configure_camera_mode')
            mocked('host.pqc_camera_demo.request_encrypted_frame', side_effect=capture)
            mocked('host.pqc_camera_demo.decode_jpeg', return_value='image')
            mocked('host.rekey_pipeline.RekeyPipeline', return_value=SimpleNamespace(active=(b'', b'', b'k' * 32, 1)))
            # Any attempt to open a log/recording during live reception is a failure.
            mocked('builtins.open', side_effect=AssertionError('Unexpected file output'))
            result = runner.receive_forever(device, stop, view, output)
        return result, captured, view, connection, handshake

    def test_live_loop_stops_only_on_explicit_stop_without_files(self):
        result, captured, view, connection, handshake = self.run_receiver()
        self.assertEqual(result, 0)
        self.assertEqual(len(captured), 3)
        self.assertEqual(view.publish.call_count, 3)
        self.assertEqual(handshake.call_count, 1)
        self.assertIsNotNone(connection.call_args.kwargs['stop_event'])

    def test_refuses_legacy_firmware(self):
        with self.assertRaisesRegex(ProtocolError, 'refusing downgrade'):
            self.run_receiver(mutual=False)

    def test_transport_failure_reauthenticates_before_more_frames(self):
        result, captured, view, connection, handshake = self.run_receiver(disconnect=True)
        self.assertEqual(result, 0)
        self.assertEqual(handshake.call_count, 2)
        self.assertEqual(connection.call_count, 2)
        self.assertEqual(view.publish.call_count, 3)

    def test_invalid_settings_fail_before_connecting(self):
        with patch.object(runner.settings, 'RESOLUTION', 'invalid'):
            with self.assertRaises(ValueError):
                runner.validate_settings()

    def test_suppressed_output_does_not_accumulate(self):
        sink = runner.SilentOutput()
        for _ in range(1000):
            self.assertEqual(sink.write('diagnostic'), 10)
        self.assertEqual(vars(sink), {})
