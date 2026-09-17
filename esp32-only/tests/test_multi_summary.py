import json
from pathlib import Path
import tempfile
import unittest

from host.multi_summary import enrich_summary


class SummaryTests(unittest.TestCase):
    def test_pipeline_metrics_keep_overlap_separate_from_blocking_rekey(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/'camera1').mkdir()
            rows = [dict(event='pipeline_offer', keygen_ms=8, sign_ms=30),
                    dict(event='pipeline_prepared', decap_ms=10, prepare_wall_ms=150, worker_stack_min=6000),
                    dict(event='pipeline_boundary', wait_ms=0),
                    dict(event='pipeline_switch', ready_ahead=True, boundary_to_verified_ms=25),
                    dict(event='pipeline_switch', ready_ahead=False, boundary_to_verified_ms=125)]
            (root/'camera1'/'trace.jsonl').write_text('\n'.join(map(json.dumps, rows)), encoding='utf-8')
            result = enrich_summary(root, dict(devices=[dict(name='camera1')]))['devices'][0]['performance']
            self.assertEqual(result['pipeline_ready_ahead_ratio'], .5)
            self.assertEqual(result['pipeline_worker_stack_min_bytes'], 6000)
            self.assertEqual(result['metrics']['pipeline_boundary_to_verified_ms']['mean'], 75)
            self.assertNotIn('rekey_ms', result['metrics'])

    def test_pooled_samples_and_reconnect_gap(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ('camera1', 'camera2'):
                (root/name).mkdir()
            def write(camera, filename, rows):
                (root/camera/filename).write_text('\n'.join(json.dumps(r) for r in rows), encoding='utf-8')
            write('camera1', 'trace.jsonl', [
                dict(event='camera_verified', wall_time='2026-09-16T00:00:00+00:00', monotonic=0),
                dict(event='camera_timing', receive_ms=10),
                dict(event='handshake_timing', outcome='error', total_ms=10000)])
            write('camera1', 'trace_attempt1.jsonl', [
                dict(event='camera_verified', wall_time='2026-09-16T00:00:10+00:00', monotonic=10),
                dict(event='camera_timing', receive_ms=20)])
            write('camera2', 'trace.jsonl', [
                dict(event='camera_verified', wall_time='2026-09-16T00:00:10+00:00', monotonic=10),
                dict(event='camera_timing', receive_ms=90)])
            summary = enrich_summary(root, dict(devices=[dict(name='camera1'), dict(name='camera2')]))
            self.assertAlmostEqual(summary['performance']['total_avg_fps'], .3)
            self.assertEqual(summary['performance']['metrics']['receive_ms']['mean'], 40)
            first = summary['devices'][0]['performance']
            self.assertEqual(first['total_avg_fps'], .2)
            self.assertNotIn('verified_interval_ms', first['metrics'])
            self.assertEqual(first['handshake_errors'], 1)

    def test_missing_data_is_unknown(self):
        with tempfile.TemporaryDirectory() as temp:
            result = enrich_summary(Path(temp), dict(devices=[dict(name='missing')]))
            self.assertIsNone(result['performance']['total_avg_fps'])
            self.assertEqual(result['performance']['verified_frames'], 0)
