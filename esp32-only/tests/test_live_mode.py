import argparse
import hashlib
import io
import json
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from host.discover_devices import Advertisement
from host.live_camera import load_settings
from host.live_display import LatestDisplay
from host.live_logging import RollingFile, LiveOutput, LiveTotals, InstanceLock
from host.wifi_recovery import rediscover_endpoint, supervise
from host.tcp_connection import TcpTransportError


class LiveModeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.key = b'x' * 1312
        self.fp = hashlib.sha256(self.key).hexdigest()

    def args(self, **values):
        return argparse.Namespace(**(dict(seconds=60, diagnostics=self.path/'trace.jsonl', display=False,
            host='192.0.2.1', tcp_port=9000, rediscover=True, discovery_timeout=1,
            _trusted_key=self.key) | values))

    def test_only_pinned_fingerprint_updates_endpoint(self):
        ads = [Advertisement('foreign', '192.0.2.9', 9000, 'f'*64),
               Advertisement('pinned', '192.0.2.2', 9001, self.fp)]
        with patch('host.discover_devices.discover', return_value=ads):
            self.assertEqual(rediscover_endpoint(self.args(), 1), ('192.0.2.2', 9001))

    def test_absence_is_retryable_but_ambiguity_is_not(self):
        with patch('host.discover_devices.discover', return_value=[]):
            with self.assertRaises(TcpTransportError):
                rediscover_endpoint(self.args(), 1)
        ads = [Advertisement('a', host, 9000, self.fp) for host in ('192.0.2.2', '192.0.2.3')]
        with patch('host.discover_devices.discover', return_value=ads):
            with self.assertRaisesRegex(ValueError, 'Ambiguous'):
                rediscover_endpoint(self.args(), 1)

    def test_two_failures_then_new_ip_uses_same_pinned_key(self):
        args = self.args()
        run = Mock(side_effect=[TcpTransportError('lost'), TcpTransportError('lost'), 0])
        with patch('host.wifi_recovery.time.monotonic', side_effect=range(1000)), \
             patch('host.wifi_recovery.rediscover_endpoint', return_value=('192.0.2.2', 9001)) as discover, \
             patch('builtins.print'):
            self.assertEqual(supervise(args, run, Mock()), 0)
        self.assertEqual([c.args[0].host for c in run.call_args_list], ['192.0.2.1', '192.0.2.1', '192.0.2.2'])
        self.assertEqual(run.call_args_list[-1].args[0]._trusted_key, self.key)
        self.assertEqual(args.host, '192.0.2.1')
        discover.assert_called_once()

    def test_bad_auth_after_rediscovery_never_retries(self):
        args = self.args(host='auto')
        run = Mock(side_effect=ValueError('signature rejected'))
        with patch('host.wifi_recovery.rediscover_endpoint', return_value=('192.0.2.2', 9000)), \
             self.assertRaisesRegex(ValueError, 'signature rejected'):
            supervise(args, run, Mock())
        self.assertEqual(run.call_count, 1)

    def test_continuous_wait_can_be_stopped_without_device(self):
        stop = threading.Event()
        args = self.args(host='auto', continuous=True, _stop_event=stop)
        def unavailable(*_):
            stop.set()
            raise TcpTransportError('offline')
        run = Mock()
        with patch('host.wifi_recovery.rediscover_endpoint', side_effect=unavailable), patch('builtins.print'):
            self.assertEqual(supervise(args, run, Mock()), 0)
        run.assert_not_called()

    def test_rolling_utf8_files_are_bounded(self):
        log = RollingFile(self.path/'log', 1024, 2)
        for _ in range(200):
            log.write('測試'*70 + '\n')
        log.close()
        files = list(self.path.iterdir())
        self.assertEqual(len(files), 3)
        for path in files:
            self.assertLessEqual(path.stat().st_size, 1024)
            path.read_text(encoding='utf-8')

    def test_normal_console_samples_fps_and_retains_errors(self):
        stream, console = io.StringIO(), io.StringIO()
        output = LiveOutput(stream, console, 'camera1')
        with patch('host.live_logging.time.monotonic', return_value=1):
            for _ in range(100):
                output.write('[PASS] frame=1 average=30.00 FPS\n')
            output.write('[PASS] other verbose event\n')
            output.write('[ERROR] bad tag\n')
        self.assertEqual(console.getvalue().count('FPS'), 1)
        self.assertEqual(stream.getvalue().count('FPS'), 1)
        self.assertIn('bad tag', stream.getvalue())
        self.assertNotIn('verbose', stream.getvalue())

    def test_settings_reject_unknown_and_nonfinite(self):
        path = self.path/'settings.json'
        self.assertEqual(load_settings(path)['resolution'], 'qvga')
        for values in ({'secret': 1}, {'response_timeout': float('inf')}, {'rekey_every': 2}):
            path.write_text(json.dumps(values))
            with self.assertRaises(ValueError):
                load_settings(path)

    def test_age_tracks_publish_not_gui_redraw(self):
        view = LatestDisplay()
        self.assertIsNone(view.age())
        with patch('host.live_display.time.monotonic', return_value=10):
            view.publish(object())
        with patch('host.live_display.time.monotonic', return_value=15):
            view.snapshot()
            view.set_status('Reconnecting')
            self.assertEqual(view.age(), 5)

    def test_totals_include_all_frames_without_sample_arrays(self):
        totals = LiveTotals()
        for _ in range(10000):
            totals.observe({'event': 'camera_verified'})
        totals.save(self.path/'summary.json', 'user_stop')
        result = json.loads((self.path/'summary.json').read_text())
        self.assertEqual(result['verified_frames'], 10000)
        self.assertFalse(any(isinstance(v, list) for v in vars(totals).values()))

    def test_live_lock_rejects_duplicate_and_releases(self):
        path = self.path/'launcher.lock'
        with InstanceLock(path):
            with self.assertRaises(RuntimeError):
                with InstanceLock(path):
                    pass
        with InstanceLock(path):
            pass

    def test_worker_uses_existing_receiver_and_writes_summary(self):
        from host.live_camera import worker
        from host.multi_camera import Device
        from host.live_display import UserStop
        settings = load_settings(self.path/'missing.json')
        settings['display'] = False
        stop = threading.Event()
        device = Device('camera1', 'unused', 9000, self.path/'device.pub', self.fp)
        def receive(args):
            self.assertTrue(args.require_mutual)
            self.assertEqual(args._trusted_key, self.key)
            args._trace_observer({'event': 'camera_verified'})
            raise UserStop()
        with patch('host.live_camera.ROOT', self.path), \
             patch('host.wifi_recovery.rediscover_endpoint', return_value=('192.0.2.2', 9000)), \
             patch('host.pqc_camera_demo.run_once', side_effect=receive):
            worker(device, self.key, settings, stop)
        result = json.loads((self.path/'diagnostics/live/camera1/summary.json').read_text())
        self.assertEqual(result['verified_frames'], 1)
