"""Compare complete summary.json files; reject incompatible conditions by default."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from run_benchmark import OPS, write_csv


def load_results(paths: list[Path], allow_different: bool = False) -> tuple[list[dict], list[dict]]:
    conditions, rows = [], []
    for path in paths:
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("schema") != 1 or data.get("complete") is not True:
            raise ValueError(f"Incomplete or unsupported result: {path}")
        meta, config = data["metadata"], data["configuration"]
        condition = {k: meta[k] for k in ("chip", "cpu_mhz", "arduino_core", "idf", "psram_bytes", "worker_core", "worker_stack_reserved_bytes", "mlkem_sha256", "mldsa_sha256", "build_id", "rng", "heap_method")}
        condition.update(message_bytes=config["message_bytes"], warmups=config["warmups"])
        if conditions and condition != conditions[0] and not allow_different:
            changed = [k for k in condition if condition[k] != conditions[0][k]]
            raise ValueError(f"Incompatible conditions in {path}: {', '.join(changed)}. Use --allow-different only for intentional cross-configuration comparison.")
        conditions.append(condition)
        groups = {(r["operation"], r["phase"]): r for r in data["summary"]}
        if len(groups) != len(OPS) * 2 or any((op, phase) not in groups for op in OPS for phase in ("timing", "memory")):
            raise ValueError(f"Missing summary groups: {path}")
        for op in OPS:
            timing, memory = groups[(op, "timing")], groups[(op, "memory")]
            rows.append({"source": str(path.resolve()), "label": data["host"].get("label", ""),
                         "run_id": config["run_id"], "operation": op,
                         "message_bytes": config["message_bytes"], "cpu_mhz": meta["cpu_mhz"], "build_id": meta["build_id"],
                         "n_timing": timing["n"], "n_memory": memory["n"],
                         "time_mean_us": timing["time_mean_us"], "time_median_us": timing["time_median_us"],
                         "time_p95_us": timing["time_p95_us"], "time_max_us": timing["time_max_us"],
                         "stack_used_max_bytes": memory["stack_used_max_bytes"],
                         "internal_heap_peak_drop_max_bytes": memory["internal_heap_peak_drop_max_bytes"],
                         "psram_heap_peak_drop_max_bytes": memory["psram_heap_peak_drop_max_bytes"],
                         "primitive_io_bytes": memory["primitive_io_bytes"]})
    return conditions, rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summaries", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "results/comparison.csv")
    parser.add_argument("--allow-different", action="store_true")
    args = parser.parse_args()
    try:
        conditions, rows = load_results(args.summaries, args.allow_different)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        write_csv(args.output, rows)
        args.output.with_suffix(".json").write_text(json.dumps({"conditions": conditions, "rows": rows, "allow_different": args.allow_different}, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"Comparison: {args.output}")
        return 0
    except (ValueError, KeyError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
