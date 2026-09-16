"""Offline aggregation only: run after all camera workers have exited."""

import argparse
from datetime import datetime
import json
import math
from pathlib import Path

# output the summary after work

def stats(values):
    ordered = sorted(values)
    return dict(n=len(values), mean=sum(values)/len(values),
                p95=ordered[math.ceil(len(values)*.95)-1], max=ordered[-1], total=sum(values))


def aggregate(directory):
    directory = Path(directory)
    metrics, times, segments, invalid = {}, [], [], []
    frames = interruptions = completed = handshake_errors = 0
    termination = None
    def add(name, value):
        if type(value) in (int, float) and math.isfinite(value) and value >= 0:
            metrics.setdefault(name, []).append(value)
    for path in sorted(directory.glob('trace*.jsonl')):
        previous = None
        count = 0
        segment_complete = False
        journal = path.name.endswith('_reconnect.jsonl')
        for number, line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
            try:
                row = json.loads(line)
                event = row['event']
                if 'wall_time' in row:
                    times.append(datetime.fromisoformat(row['wall_time']).timestamp())
            except (ValueError, KeyError, TypeError):
                invalid.append(f'{path.name}:{number}')
                continue
            if journal:
                interruptions += event == 'transport_interrupted'
                termination = event
                continue
            if event == 'recovery_start':
                previous = None
            if event == 'camera_verified':
                frames += 1
                count += 1
                now = row.get('monotonic')
                if isinstance(now, (int, float)):
                    if previous is not None:
                        add('verified_interval_ms', (now-previous)*1000)
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
            if event == 'handshake_timing':
                if row.get('outcome') == 'ok':
                    for key, value in row.items():
                        if key.endswith('_ms'):
                            add('handshake_' + key, value)
                else:
                    handshake_errors += 1
            if event == 'run_complete':
                segment_complete = True
            if event == 'run_error':
                segment_complete = False
        if not journal:
            completed += segment_complete
            segments.append(dict(source=path.name, verified_frames=count, complete=segment_complete))
    start, end = (min(times), max(times)) if times else (None, None)
    elapsed = end-start if times else None
    return dict(verified_frames=frames, observed_elapsed_seconds=elapsed,
                total_avg_fps=frames/elapsed if elapsed and elapsed > 0 else None,
                transport_interruptions=interruptions, termination=termination,
                completed_segments=completed, handshake_errors=handshake_errors,
                invalid_lines=invalid, segments=segments,
                metrics={name: stats(values) for name, values in metrics.items()}), metrics, start, end


def enrich_summary(directory, summary):
    metrics, starts, ends = {}, [], []
    frames = 0
    for device in summary['devices']:
        result, samples, start, end = aggregate(Path(directory)/device['name'])
        device['performance'] = result
        frames += result['verified_frames']
        if start is not None:
            starts.append(start)
            ends.append(end)
        for name, values in samples.items():
            metrics.setdefault(name, []).extend(values)
    elapsed = max(ends)-min(starts) if starts else None
    summary['performance'] = dict(
        verified_frames=frames, observed_elapsed_seconds=elapsed,
        total_avg_fps=frames/elapsed if elapsed and elapsed > 0 else None,
        metrics={name: stats(values) for name, values in metrics.items()},
        notes='FPS = verified frames / observed wall time, including startup, reconnect gaps and finalize. '
              'Overall FPS is combined throughput of all devices, not per-camera FPS. '
              'Metric means pool individual samples; handshake metrics include successful handshakes only. '
              'Durations overlap; do not sum all metric totals. Missing data is not zero or proof of success.')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    path = args.directory/'summary.json'
    summary = enrich_summary(args.directory, json.loads(path.read_text(encoding='utf-8-sig')))
    path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding='utf-8')
    print(path.resolve())


if __name__ == '__main__':
    main()
