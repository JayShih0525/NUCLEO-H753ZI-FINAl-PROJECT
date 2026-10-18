"""Collect ESP32 primitive measurements, preserve raw evidence and summarize."""
from __future__ import annotations

import argparse
from collections import defaultdict
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent
OPS = ("baseline", "mlkem768_keygen", "mlkem768_encaps", "mlkem768_decaps", "mldsa44_keygen", "mldsa44_sign", "mldsa44_verify")
PHASES = ("timing", "memory")


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    low = math.floor(index)
    return ordered[low] + (ordered[math.ceil(index)] - ordered[low]) * (index - low)


class Run:
    def __init__(self, run_id: str, rounds: int, warmups: int, message_bytes: int):
        self.run_id, self.rounds, self.warmups, self.message_bytes = run_id, rounds, warmups, message_bytes
        self.metadata = self.begin = self.end = None
        self.samples = []
        self.seen = set()
        self.expected_build_id = None

    def accept(self, obj: dict) -> None:
        kind = obj.get("type")
        if kind == "metadata":
            if self.metadata is not None or self.begin is not None:
                raise ValueError("Repeated metadata: board may have restarted")
            if obj.get("firmware") != "esp32-crypto-benchmark" or obj.get("schema") != 1:
                raise ValueError("Wrong benchmark firmware/schema")
            if self.expected_build_id is not None and obj.get("build_id") != self.expected_build_id:
                raise ValueError("Build changed after HELLO")
            if type(obj.get("worker_stack_reserved_bytes")) is not int or obj["worker_stack_reserved_bytes"] <= 0:
                raise ValueError("Missing stack reservation metadata")
            self.metadata = obj
            return
        if kind == "ready":
            raise ValueError("Board rebooted during the run")
        if kind not in ("begin", "sample", "end", "error"):
            raise ValueError(f"Unexpected firmware response: {kind}")
        if obj.get("run_id") != self.run_id:
            raise ValueError("Unexpected run_id / stale serial data")
        if kind == "error":
            raise ValueError(f"Firmware error: {obj}")
        if kind == "begin":
            expected = {"rounds": self.rounds, "warmups": self.warmups, "message_bytes": self.message_bytes, "expected_samples": self.rounds * len(OPS) * len(PHASES)}
            if self.begin is not None or not self.metadata or any(obj.get(k) != v for k, v in expected.items()):
                raise ValueError("Invalid begin/configuration mismatch")
            self.begin = obj
        elif kind == "sample":
            if not self.begin or self.end:
                raise ValueError("Sample outside active run")
            op, phase, iteration = obj.get("operation"), obj.get("phase"), obj.get("iteration")
            if op not in OPS or phase not in PHASES or type(iteration) is not int or not 1 <= iteration <= self.rounds:
                raise ValueError("Invalid operation/phase/iteration")
            key = (op, phase, iteration)
            if key in self.seen:
                raise ValueError("Duplicate measurement")
            if obj.get("rc") != 0 or obj.get("validation_ok") is not True:
                raise ValueError("Failed crypto validation")
            fields = ["elapsed_us", "stack_used_bytes", "stack_free_bytes", "primitive_io_bytes"]
            if phase == "memory":
                fields += [f"{pool}_{metric}_bytes" for pool in ("internal", "psram") for metric in ("free_before", "free_after", "low")]
                fields += ["internal_largest_after_bytes"]
                if obj.get("monitor_start") != 0 or obj.get("monitor_stop") != 0:
                    raise ValueError("Local heap monitor failed")
            for field in fields:
                if type(obj.get(field)) is not int or obj[field] < 0:
                    raise ValueError(f"Invalid/missing integer {field}")
            if obj["stack_used_bytes"] + obj["stack_free_bytes"] != self.metadata["worker_stack_reserved_bytes"]:
                raise ValueError("Stack measurement inconsistent with reservation")
            expected_io = {"baseline": 0, "mlkem768_keygen": 3584, "mlkem768_encaps": 2304,
                           "mlkem768_decaps": 3520, "mldsa44_keygen": 3872,
                           "mldsa44_sign": 4980 + self.message_bytes, "mldsa44_verify": 3732 + self.message_bytes}
            if obj["primitive_io_bytes"] != expected_io[op]:
                raise ValueError("Unexpected primitive buffer sizes")
            self.seen.add(key)
            self.samples.append(obj)
        elif kind == "end":
            expected = self.rounds * len(OPS) * len(PHASES)
            if self.end or not self.begin or obj.get("ok") is not True or obj.get("samples") != expected or len(self.seen) != expected:
                raise ValueError("Incomplete/failed run (missing measurements)")
            self.end = obj

    def summary(self) -> list[dict]:
        groups = defaultdict(list)
        for sample in self.samples:
            groups[(sample["operation"], sample["phase"])].append(sample)
        rows = []
        for (op, phase), samples in sorted(groups.items()):
            times = [s["elapsed_us"] for s in samples]
            row = {"operation": op, "phase": phase, "n": len(samples), "time_min_us": min(times),
                   "time_mean_us": statistics.mean(times), "time_median_us": statistics.median(times),
                   "time_p95_us": percentile(times, .95), "time_max_us": max(times),
                   "time_stdev_us": statistics.stdev(times) if len(times) > 1 else None,
                   "stack_used_max_bytes": max(s["stack_used_bytes"] for s in samples),
                   "stack_free_min_bytes": min(s["stack_free_bytes"] for s in samples),
                   "primitive_io_bytes": samples[0]["primitive_io_bytes"]}
            if phase == "memory":
                for pool in ("internal", "psram"):
                    # Aggregate per-region low watermarks, not exact per-function allocation.
                    row[f"{pool}_heap_peak_drop_max_bytes"] = max(max(0, s[f"{pool}_free_before_bytes"] - s[f"{pool}_low_bytes"]) for s in samples)
                    row[f"{pool}_heap_retained_delta_max_bytes"] = max(s[f"{pool}_free_before_bytes"] - s[f"{pool}_free_after_bytes"] for s in samples)
                row["internal_largest_after_min_bytes"] = min(s["internal_largest_after_bytes"] for s in samples)
            rows.append(row)
        return rows


def write_csv(path: Path, rows: list[dict]) -> None:
    if not rows:
        return
    keys = list(dict.fromkeys(k for row in rows for k in row))
    with path.open("w", encoding="utf-8-sig", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def save_result(out: Path, run: Run, error: str | None, host: dict) -> None:
    rows = run.summary()
    result = {"schema": 1, "complete": bool(run.end) and error is None, "error": error,
              "host": host, "metadata": run.metadata, "configuration": run.begin,
              "requested": {"rounds": run.rounds, "warmups": run.warmups, "message_bytes": run.message_bytes},
              "received_samples": len(run.samples), "end": run.end, "summary": rows}
    (out / "summary.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    write_csv(out / "samples.csv", [dict(s, complete_run=result["complete"]) for s in run.samples])
    write_csv(out / "summary.csv", [dict(r, complete_run=result["complete"], expected_n=run.rounds) for r in rows])
    lines = ["# ESP32 密碼演算法 benchmark", "", f"狀態：{'完整完成' if result['complete'] else '未完成，不能當成正式比較結果'}", "",
             f"DSA 訊息長度：{run.message_bytes} bytes；每項每階段 {run.rounds} 次，先暖機 {run.warmups} 次。", "",
             "時間只含 ESP32 本地函式呼叫，不含 UART、輸入準備或正確性驗證。以下時間採 timing 階段，記憶體採 memory 階段。", "",
             "| 操作 | 次數 time/mem | 中位數 ms | 平均 ms | P95 ms | 最大 ms | Stack 最大 KiB | Internal Heap 最大降幅 KiB | PSRAM Heap 最大降幅 KiB | I/O buffers KiB |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    by_key = {(r["operation"], r["phase"]): r for r in rows}
    for op in OPS:
        t, m = by_key.get((op, "timing")), by_key.get((op, "memory"))
        if not t or not m:
            continue
        lines.append(f"| {op} | {t['n']}/{m['n']} | {t['time_median_us']/1000:.3f} | {t['time_mean_us']/1000:.3f} | {t['time_p95_us']/1000:.3f} | {t['time_max_us']/1000:.3f} | {m['stack_used_max_bytes']/1024:.2f} | {m['internal_heap_peak_drop_max_bytes']/1024:.2f} | {m['psram_heap_peak_drop_max_bytes']/1024:.2f} | {m['primitive_io_bytes']/1024:.2f} |")
    lines += ["", "注意：Heap 是整個系統各 region 低水位的加總，可混入背景任務，並非精確配置歸屬；baseline 提供觀測底噪，不自動扣除。Stack 是新建獨立任務的填色高水位估計，含量測框架，未觸寫區域可能低估。固定 Stack 保留量及共用 buffers 請看 metadata，不能與 Heap 降幅混為一談。KiB=1024 bytes。", "",
              "ML-KEM-768 與 ML-DSA-44 用途不同，分別約 NIST category 3 與 2；本表不是同等安全強度的演算法排名。", ""]
    if error:
        lines += [f"中止原因：{error}", ""]
    (out / "report.md").write_text("\n".join(lines), encoding="utf-8")


def collect(port, run: Run, raw, timeout: float, total_timeout: float) -> None:
    start = last = time.monotonic()
    while not run.end:
        if time.monotonic() - last > timeout:
            raise TimeoutError("No valid benchmark record before idle timeout")
        if time.monotonic() - start > total_timeout:
            raise TimeoutError("Total run deadline exceeded")
        data = port.readline()
        if not data:
            continue
        line = data.decode("utf-8", errors="replace").rstrip("\r\n")
        raw.write(json.dumps({"received_utc": datetime.now(timezone.utc).isoformat(), "line": line}, ensure_ascii=False) + "\n")
        raw.flush()
        if not line.startswith("{"):
            continue
        obj = json.loads(line)
        run.accept(obj)
        last = time.monotonic()
        if obj.get("type") == "sample" and (obj["iteration"] == 1 or obj["iteration"] == run.rounds):
            print(f"{obj['phase']:6s} {obj['operation']:19s} {obj['iteration']}/{run.rounds}: {obj['elapsed_us']/1000:.3f} ms", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--rounds", type=int, default=30)
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--message-bytes", type=int, default=32)
    parser.add_argument("--timeout", type=float, default=30, help="Seconds without a valid JSON record")
    parser.add_argument("--total-timeout", type=float, default=1800)
    parser.add_argument("--output", type=Path, default=ROOT / "results")
    parser.add_argument("--label", default="", help="Board/run note saved with results")
    args = parser.parse_args()
    if not 1 <= args.rounds <= 1000 or not 0 <= args.warmups <= 20 or not 0 <= args.message_bytes <= 4096 or min(args.timeout, args.total_timeout) <= 0:
        parser.error("rounds 1..1000, warmups 0..20, message bytes 0..4096; positive timeouts")
    import serial
    run = Run(uuid.uuid4().hex, args.rounds, args.warmups, args.message_bytes)
    out = args.output / (datetime.now().strftime("%Y%m%d_%H%M%S") + "_" + run.run_id[:8])
    out.mkdir(parents=True, exist_ok=False)
    host = {"port": args.port, "baud": args.baud, "label": args.label, "started_utc": datetime.now(timezone.utc).isoformat(), "python": sys.version}
    build_info = ROOT / "build/build_info.json"
    if build_info.exists():
        host["local_build_info"] = json.loads(build_info.read_text(encoding="utf-8"))
    error = None
    try:
        # No intentional reset on UART open, and never send RUN to wrong firmware.
        with serial.Serial(port=None, baudrate=args.baud, timeout=.5, write_timeout=3) as port:
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()
            port.reset_input_buffer()
            port.write(f"HELLO {run.run_id}\n".encode("ascii"))
            deadline = time.monotonic() + args.timeout
            with (out / "raw.jsonl").open("w", encoding="utf-8") as raw:
                ready = None
                while time.monotonic() < deadline:
                    line = port.readline().decode("utf-8", errors="replace").rstrip("\r\n")
                    if not line:
                        continue
                    raw.write(json.dumps({"received_utc": datetime.now(timezone.utc).isoformat(), "line": line}, ensure_ascii=False) + "\n")
                    raw.flush()
                    if line.startswith("{"):
                        obj = json.loads(line)
                        if obj.get("type") == "ready" and obj.get("firmware") == "esp32-crypto-benchmark" and obj.get("schema") == 1 and obj.get("probe_id") == run.run_id:
                            ready = obj
                            break
                if not ready:
                    raise TimeoutError("Benchmark READY not received; check firmware, TTL port, baud, and press RESET")
                host["ready"] = ready
                run.expected_build_id = ready["build_id"]
                if "local_build_info" in host and (not host["local_build_info"].get("ok") or ready["build_id"] != host["local_build_info"]["build_id"]):
                    raise ValueError("Board build_id does not match successful local build; flash correct firmware first")
                port.write(f"RUN {run.run_id} {args.rounds} {args.warmups} {args.message_bytes}\n".encode("ascii"))
                collect(port, run, raw, args.timeout, args.total_timeout)
    except (Exception, KeyboardInterrupt) as exc:
        error = f"{type(exc).__name__}: {exc}"
        print(error, file=sys.stderr)
    finally:
        host["finished_utc"] = datetime.now(timezone.utc).isoformat()
        save_result(out, run, error, host)
        print(f"Results: {out}", flush=True)
    return 0 if run.end and error is None else 1


if __name__ == "__main__":
    sys.exit(main())
