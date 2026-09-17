import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from host.multi_camera import load_devices, command_for, run_devices, stop_workers, report_fps


class MultiCameraTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / 'devices.json'
        self.items = []
        for index in range(3):
            key = self.root / f'camera{index}.pub'
            key.write_bytes(bytes([index]) * 1312)
            self.items.append(dict(name=f'camera{index}', host=f'192.0.2.{index+1}', trust_key=key.name))
        self.save()

    def save(self):
        self.config.write_text(json.dumps(dict(devices=self.items)), encoding='utf-8')

    def test_n_devices_and_key_paths_relative_to_config(self):
        devices = load_devices(self.config)
        self.assertEqual(len(devices), 3)
        self.assertEqual(devices[2].trust_key, self.root / 'camera2.pub')

    def test_fps_only_throttled_and_partial_line_retained(self):
        path = self.root / 'terminal.txt'
        path.write_text('[PASS] identity verified\n[PASS] frame=10 epoch=1 average=12.30 FPS\n', encoding='utf-8')
        worker = dict(name='camera2', log_path=path)
        with patch('host.multi_camera.time.monotonic', return_value=10), patch('builtins.print') as output:
            report_fps(worker)
            output.assert_called_once_with('[camera2] average=12.30 FPS | frame=10', flush=True)
        with path.open('a', encoding='utf-8') as log:
            log.write('[PASS] frame=20 epoch=2 average=13.')
        with patch('builtins.print') as output:
            report_fps(worker)
            output.assert_not_called()
        with path.open('a', encoding='utf-8') as log:
            log.write('40 FPS\n[MEM] Internal free=100\n')
        with patch('host.multi_camera.time.monotonic', return_value=10.5), patch('builtins.print') as output:
            report_fps(worker)
            output.assert_not_called()
            report_fps(worker, final=True)
            output.assert_called_once_with('[camera2] average=13.40 FPS | frame=20', flush=True)

    def test_dynamic_ip_selects_only_requested_even_when_disabled(self):
        self.items[1]['enabled'] = False
        self.save()
        devices = load_devices(self.config, {'camera1': '10.0.0.7'})
        self.assertEqual([(d.name, d.host) for d in devices], [('camera1', '10.0.0.7')])
        with self.assertRaises(ValueError):
            load_devices(self.config, {'missing': '10.0.0.8'})

    def test_duplicates_and_bad_config_rejected_before_spawn(self):
        for field in ('name', 'host', 'trust_key'):
            with self.subTest(field=field):
                original = self.items[1][field]
                self.items[1][field] = self.items[0][field]
                self.save()
                with self.assertRaises(ValueError):
                    load_devices(self.config)
                self.items[1][field] = original
        self.items[0]['name'] = '../escape'
        self.save()
        with self.assertRaises(ValueError):
            load_devices(self.config)

    def test_missing_or_short_key_no_fallback(self):
        key = self.root / 'camera0.pub'
        key.write_bytes(b'x')
        with self.assertRaises(ValueError):
            load_devices(self.config)
        key.unlink()
        with self.assertRaises(OSError):
            load_devices(self.config)

    def test_command_has_explicit_identity_and_diagnostics(self):
        device = load_devices(self.config)[0]
        command = command_for(device, self.root, 60, 10, 10, True)
        self.assertEqual(command[command.index('--trust-key')+1], str(device.trust_key))
        self.assertIn('--display', command)
        self.assertIn(str(self.root / 'trace.jsonl'), command)
        self.assertNotIn('--rekey-mode', command)
        experiment = command_for(device, self.root, 60, 10, 10, True, 'pipeline')
        self.assertEqual(experiment[experiment.index('--rekey-mode')+1], 'pipeline')

    def test_failed_worker_does_not_stop_others(self):
        devices = load_devices(self.config)
        processes = [Mock(pid=i, returncode=code) for i, code in enumerate((1, 0, 0))]
        for process in processes:
            process.poll.return_value = process.returncode
        output = self.root / 'run'
        with patch('builtins.print'):
            result = run_devices(devices, output, 60, 10, 10, False, popen=Mock(side_effect=processes))
        self.assertEqual(result, 1)
        summary = json.loads((output / 'summary.json').read_text())
        self.assertEqual([d['exit_code'] for d in summary['devices']], [1, 0, 0])
        for process in processes:
            process.terminate.assert_not_called()
        for device in devices:
            self.assertTrue((output / device.name / 'terminal.txt').exists())
            self.assertEqual((output / device.name / 'device.pub').read_bytes(), device.trust_key.read_bytes())

    def test_partial_spawn_failure_reaps_started_worker(self):
        devices = load_devices(self.config)
        process = Mock(pid=1, returncode=-1)
        process.poll.return_value = None
        with patch('builtins.print'):
            result = run_devices(devices, self.root/'run', 60, 10, 10, False,
                                 popen=Mock(side_effect=[process, OSError('spawn failed')]))
        self.assertEqual(result, 1)
        process.send_signal.assert_called_once()
        process.wait.assert_called()

    def test_forced_shutdown_after_grace(self):
        process = Mock()
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired('worker', 0), 0]
        worker = dict(process=process, forced_stop=False)
        stop_workers([worker], grace=0)
        process.kill.assert_called_once()
        self.assertTrue(worker['forced_stop'])

    def test_changed_identity_between_preflight_and_launch_rejected(self):
        devices = load_devices(self.config)
        devices[0].trust_key.write_bytes(b'z' * 1312)
        spawn = Mock()
        with patch('builtins.print'):
            result = run_devices(devices, self.root/'run', 60, 10, 10, False, popen=spawn)
        self.assertEqual(result, 1)
        spawn.assert_not_called()

    def test_real_processes_keep_logs_and_continue_after_one_failure(self):
        devices = load_devices(self.config)
        def spawn(command, **kwargs):
            name = command[command.index('--device-name') + 1]
            script = "import sys,time; print(sys.argv[1],flush=True); time.sleep(.1); sys.exit(int(sys.argv[2]))"
            return subprocess.Popen([sys.executable, '-c', script, name, '1' if name == 'camera0' else '0'], **kwargs)
        output = self.root / 'real-run'
        with patch('builtins.print'):
            self.assertEqual(run_devices(devices, output, 60, 10, 10, False, popen=spawn), 1)
        for device in devices:
            self.assertEqual((output/device.name/'terminal.txt').read_text().strip(), device.name)


if __name__ == '__main__':
    unittest.main()
