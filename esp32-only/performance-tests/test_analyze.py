import json
from pathlib import Path
import tempfile
import unittest
from analyze_trace import analyze, stats


class AnalysisTests(unittest.TestCase):
    def summary(self, rows, tail=''):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text(''.join(json.dumps(row) + '\n' for row in rows) + tail, encoding='utf-8')
            return analyze(path)

    def test_intervals_recovery_and_stream_fps(self):
        rows = [dict(event='camera_verified', monotonic=1),
                dict(event='camera_verified', monotonic=1.1),
                dict(event='recovery_start'), dict(event='camera_verified', monotonic=20),
                dict(event='stream_end', frames_received=3, elapsed_ms=20000),
                dict(event='run_complete')]
        result = self.summary(rows)
        self.assertAlmostEqual(result['interval_fps'], 10)
        self.assertEqual(result['stream_fps'], .15)
        self.assertEqual(result['recoveries'], 1)
        self.assertNotIn('decode_ms', result['metrics'])

    def test_truncation_and_error_never_pass(self):
        self.assertFalse(self.summary([dict(event='run_complete')], '{')['complete'])
        self.assertFalse(self.summary([dict(event='run_error'), dict(event='run_complete')])['complete'])
        self.assertFalse(self.summary([])['complete'])

    def test_nearest_rank(self):
        self.assertEqual(stats(list(range(1, 21)))['p95'], 19)
        self.assertIsNone(stats([]))


if __name__ == '__main__':
    unittest.main()
