"""Synthetic protocol fixtures only; these are NOT ESP32 benchmark results."""
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_benchmark import OPS, PHASES, Run, collect, percentile, save_result
from compare import load_results
import run_benchmark

TEMP_ROOT = Path(__file__).resolve().parents[1] / ".test-tmp"
TEMP_ROOT.mkdir(exist_ok=True)


def metadata():
    return {"type": "metadata", "schema": 1, "firmware": "esp32-crypto-benchmark", "build_id": "synthetic-test-only",
            "chip": "ESP32-S3", "cpu_mhz": 240, "arduino_core": "test", "idf": "test", "psram_bytes": 8388608,
            "worker_core": 1, "worker_stack_reserved_bytes": 98304, "mlkem_sha256": "test-kem", "mldsa_sha256": "test-dsa",
            "rng": "test", "heap_method": "test"}


def begin(rounds=2):
    return {"type": "begin", "run_id": "abc", "rounds": rounds, "warmups": 1, "message_bytes": 32, "expected_samples": rounds * 14}


def sample(op="mldsa44_sign", phase="memory", iteration=1):
    io_bytes = {"baseline": 0, "mlkem768_keygen": 3584, "mlkem768_encaps": 2304, "mlkem768_decaps": 3520,
                "mldsa44_keygen": 3872, "mldsa44_sign": 5012, "mldsa44_verify": 3764}
    row = {"type": "sample", "run_id": "abc", "operation": op, "phase": phase, "iteration": iteration,
           "elapsed_us": 100 * iteration, "rc": 0, "validation_ok": True, "stack_used_bytes": 1024,
           "stack_free_bytes": 97280, "primitive_io_bytes": io_bytes[op]}
    if phase == "memory":
        row.update(internal_free_before_bytes=10000, internal_free_after_bytes=10000, internal_low_bytes=9800,
                   internal_largest_after_bytes=8000, psram_free_before_bytes=8000000,
                   psram_free_after_bytes=8000000, psram_low_bytes=8000000, monitor_start=0, monitor_stop=0)
    return row


def completed_run():
    run = Run("abc", 2, 1, 32)
    run.accept(metadata())
    run.accept(begin())
    for phase in PHASES:
        for op in OPS:
            for iteration in (1, 2):
                run.accept(sample(op, phase, iteration))
    run.accept({"type": "end", "run_id": "abc", "ok": True, "samples": 28})
    return run


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.run = Run("abc", 2, 1, 32)
        self.run.accept(metadata())
        self.run.accept(begin())

    def test_complete_and_statistics(self):
        run = completed_run()
        self.assertEqual(len(run.summary()), 14)
        row = next(r for r in run.summary() if r["operation"] == "mldsa44_sign" and r["phase"] == "memory")
        self.assertEqual(row["time_median_us"], 150)
        self.assertEqual(row["time_p95_us"], 195)
        self.assertEqual(row["internal_heap_peak_drop_max_bytes"], 200)
        self.assertEqual(row["internal_heap_retained_delta_max_bytes"], 0)

    def test_duplicate_rejected(self):
        self.run.accept(sample())
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.run.accept(sample())

    def test_missing_samples_not_success(self):
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            self.run.accept({"type": "end", "run_id": "abc", "ok": True, "samples": 28})

    def test_reset_rejected(self):
        with self.assertRaisesRegex(ValueError, "rebooted"):
            self.run.accept({"type": "ready"})
        with self.assertRaisesRegex(ValueError, "Repeated metadata"):
            self.run.accept(metadata())

    def test_wrong_run(self):
        row = sample()
        row["run_id"] = "other"
        with self.assertRaisesRegex(ValueError, "run_id"):
            self.run.accept(row)

    def test_invalid_fields_rejected(self):
        for field, value in (("elapsed_us", -1), ("iteration", 3), ("validation_ok", False),
                             ("rc", -1), ("monitor_start", -1), ("stack_used_bytes", 200000),
                             ("primitive_io_bytes", 1), ("elapsed_us", True), ("phase", "bad")):
            with self.subTest(field=field, value=value):
                row = sample()
                row[field] = value
                with self.assertRaises(ValueError):
                    self.run.accept(row)

    def test_wrong_configuration(self):
        run = Run("abc", 2, 1, 64)
        run.accept(metadata())
        with self.assertRaisesRegex(ValueError, "configuration"):
            run.accept(begin())

    def test_build_changed_after_hello(self):
        run = Run("abc", 2, 1, 32)
        run.expected_build_id = "different-build"
        with self.assertRaisesRegex(ValueError, "Build changed"):
            run.accept(metadata())

    def test_uninitialized_and_error(self):
        with self.assertRaises(ValueError):
            Run("abc", 2, 1, 32).accept(sample())
        with self.assertRaisesRegex(ValueError, "Firmware error"):
            self.run.accept({"type": "error", "run_id": "abc", "stage": "validate"})

    def test_single_observation_no_fake_stddev(self):
        self.run.accept(sample())
        self.assertIsNone(self.run.summary()[0]["time_stdev_us"])
        self.assertEqual(percentile([4], .95), 4)

    def test_heap_gain_does_not_make_negative_peak(self):
        row = sample()
        row["internal_low_bytes"] = 10004
        row["internal_free_after_bytes"] = 10004
        self.run.accept(row)
        result = self.run.summary()[0]
        self.assertEqual(result["internal_heap_peak_drop_max_bytes"], 0)
        self.assertEqual(result["internal_heap_retained_delta_max_bytes"], -4)

    def test_saved_complete_and_incomplete(self):
        for run, error in ((completed_run(), None), (self.run, "test interruption")):
            with self.subTest(error=error), tempfile.TemporaryDirectory(dir=TEMP_ROOT) as folder:
                path = Path(folder)
                save_result(path, run, error, {"label": "synthetic test"})
                result = json.loads((path / "summary.json").read_text(encoding="utf-8"))
                self.assertEqual(result["complete"], error is None)
                self.assertIn("Stack", (path / "report.md").read_text(encoding="utf-8"))
                if error is None:
                    self.assertEqual((path / "samples.csv").read_bytes()[:3], b"\xef\xbb\xbf")

    def test_comparison_conditions_and_partial(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as folder:
            root = Path(folder)
            one, two = root / "one", root / "two"
            one.mkdir()
            two.mkdir()
            run = completed_run()
            save_result(one, run, None, {"label": "A"})
            save_result(two, run, None, {"label": "B"})
            paths = [one / "summary.json", two / "summary.json"]
            self.assertEqual(len(load_results(paths)[1]), 14)
            data = json.loads(paths[1].read_text(encoding="utf-8"))
            data["metadata"]["cpu_mhz"] = 160
            paths[1].write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "cpu_mhz"):
                load_results(paths)
            self.assertEqual(len(load_results(paths, True)[1]), 14)
            data["complete"] = False
            paths[1].write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Incomplete"):
                load_results(paths, True)

    def test_serial_collection_and_raw_evidence(self):
        run = Run("abc", 2, 1, 32)
        ready_run = completed_run()
        records = [metadata(), begin(), *ready_run.samples, ready_run.end]
        class FakePort:
            def __init__(self):
                self.lines = iter([b"ESP-ROM boot log\n"] + [(json.dumps(r) + "\n").encode() for r in records])
            def readline(self):
                return next(self.lines, b"")
        raw = io.StringIO()
        collect(FakePort(), run, raw, 1, 10)
        self.assertTrue(run.end)
        self.assertIn("ESP-ROM boot log", raw.getvalue())
        self.assertEqual(len(raw.getvalue().splitlines()), 32)

    def test_malformed_json_aborts(self):
        class FakePort:
            def readline(self):
                return b'{"type":"sample",broken\n'
        with self.assertRaises(json.JSONDecodeError):
            collect(FakePort(), self.run, io.StringIO(), 1, 10)

    def test_timeout(self):
        class FakePort:
            def readline(self):
                return b""
        with self.assertRaises(TimeoutError):
            collect(FakePort(), self.run, io.StringIO(), .001, 1)

    def test_main_serial_handshake_and_saved_results(self):
        class FakeSerial:
            def __init__(self, **kwargs):
                self.lines = []
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def open(self):
                pass
            def reset_input_buffer(self):
                self.lines.clear()
            def write(self, data):
                if data.startswith(b"HELLO "):
                    startup = dict(metadata(), type="ready", probe_id="")
                    response = dict(startup, probe_id="abc")
                    self.lines.extend([startup, response])
                elif data.startswith(b"RUN abc 2 1 32"):
                    run = completed_run()
                    self.lines.extend([metadata(), begin(), *run.samples, run.end])
                else:
                    raise AssertionError(f"Unexpected command {data!r}")
            def readline(self):
                return (json.dumps(self.lines.pop(0)) + "\n").encode() if self.lines else b""
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as folder:
            root = Path(folder)
            argv = ["run_benchmark.py", "--port", "FAKE", "--rounds", "2", "--warmups", "1", "--output", str(root / "results")]
            with patch.object(run_benchmark, "ROOT", root), patch.object(sys, "argv", argv), patch("serial.Serial", FakeSerial), patch.object(run_benchmark.uuid, "uuid4", return_value=SimpleNamespace(hex="abc")), patch("sys.stdout", io.StringIO()):
                self.assertEqual(run_benchmark.main(), 0)
            result_path = next((root / "results").glob("*/summary.json"))
            result = json.loads(result_path.read_text(encoding="utf-8"))
            self.assertTrue(result["complete"])
            self.assertEqual(result["received_samples"], 28)
            self.assertEqual(result["host"]["ready"]["probe_id"], "abc")
            self.assertTrue(result_path.with_name("raw.jsonl").exists())


if __name__ == "__main__":
    unittest.main()
