"""Standard-library-only camera trace summary; never reads image/key payloads."""
import argparse
import json
import math
from pathlib import Path


def stats(values):
    if not values:
        return None
    ordered = sorted(values)
    return dict(n=len(values), mean=sum(values) / len(values),
                p95=ordered[math.ceil(len(values) * .95) - 1], max=ordered[-1])


def analyze(path):
    metrics = {}
    counts = {}
    previous = None
    invalid = []
    stream = None
    configuration = {}
    for number, line in enumerate(Path(path).read_text(encoding='utf-8-sig').splitlines(), 1):
        try:
            row = json.loads(line)
            event = row['event']
        except (ValueError, KeyError, TypeError):
            invalid.append(number)
            continue
        counts[event] = counts.get(event, 0) + 1
        def add(name, value):
            if isinstance(value, (int, float)) and math.isfinite(value) and value >= 0:
                metrics.setdefault(name, []).append(value)
        if event in ('port_open_begin', 'stream_start'):
            configuration.update({k: row[k] for k in
                ('transport', 'seconds', 'rekey_every', 'memory_every', 'display', 'profile') if k in row})
        if event == 'recovery_start':
            previous = None  # Do not join independent authenticated stream segments.
        if event == 'camera_verified':
            now = row['monotonic']
            if previous is not None:
                add('verified_interval_ms', (now - previous) * 1000)
            previous = now
        if event == 'camera_timing':
            for name in ('request_to_status_ms', 'receive_ms', 'validate_ms', 'jpeg_bytes'):
                add(name, row.get(name))
        if event in ('decode_timing', 'display_timing', 'rekey_timing'):
            add(event.replace('_timing', '_ms'), row.get('elapsed_ms'))
        if event == 'camera_status':
            for token in row.get('status', '').split():
                if token.startswith('elapsed_ms='):
                    try:
                        add('device_capture_encrypt_ms', float(token.split('=', 1)[1]))
                    except ValueError:
                        pass
        if event == 'stream_end':
            stream = row
    intervals = metrics.get('verified_interval_ms', [])
    return dict(source=Path(path).name,
        complete=bool(counts.get('run_complete')) and not counts.get('run_error') and not invalid,
        invalid_lines=invalid, configuration=configuration,
        verified_frames=counts.get('camera_verified', 0),
        recoveries=counts.get('recovery_start', 0),
        stream_fps=(stream['frames_received'] * 1000 / stream['elapsed_ms']
                    if stream and stream.get('elapsed_ms', 0) > 0 else None),
        interval_fps=(1000 * len(intervals) / sum(intervals) if intervals and sum(intervals) > 0 else None),
        metrics={name: stats(values) for name, values in metrics.items()})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = analyze(args.trace)
    rendered = json.dumps(result, indent=2, ensure_ascii=False)
    print(rendered)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + '\n', encoding='utf-8')
    return 0 if result['complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
