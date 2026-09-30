"""Run the shared camera Host and retain reproducible local results, without video."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

from analyze_trace import analyze


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', required=True)
    parser.add_argument('--trust-key', help='device .pub file (relative to current directory)')
    parser.add_argument('--tcp-port', type=int, default=9000)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--rekey-every', type=int, default=10)
    parser.add_argument('--memory-every', type=int, default=10)
    parser.add_argument('--display', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = root / 'performance-tests' / 'results' / datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    output.mkdir(parents=True)
    trace = output / 'trace.jsonl'
    command = [sys.executable, '-u', '-m', 'host.pqc_camera_demo',
               '--host', args.host, '--tcp-port', str(args.tcp_port), '--mode', 'record',
               '--seconds', str(args.seconds), '--rekey-every', str(args.rekey_every),
               '--memory-every', str(args.memory_every), '--profile', '--diagnostics', str(trace)]
    if args.display:
        command.append('--display')
    if args.trust_key:
        command.extend(['--trust-key', str(Path(args.trust_key).resolve())])
    def git(*arguments):
        result = subprocess.run(['git', *arguments], cwd=root, capture_output=True, text=True)
        return result.stdout.strip() if result.returncode == 0 else None
    metadata = dict(command=command, python=sys.version, git_commit=git('rev-parse', 'HEAD'),
                    git_changes=git('status', '--porcelain'), options=vars(args))
    (output / 'manifest.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print(f'Results: {output}', flush=True)
    with (output / 'terminal.txt').open('w', encoding='utf-8') as log:
        with subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding='utf-8', errors='replace',
                              env=os.environ | {'PYTHONIOENCODING': 'utf-8'}) as process:
            try:
                for line in process.stdout:
                    print(line, end='', flush=True)
                    log.write(line)
                    log.flush()
                code = process.wait()
            except KeyboardInterrupt:
                process.terminate()
                code = process.wait()
    summary = analyze(trace) if trace.exists() else dict(complete=False, reason='No trace created')
    journal = trace.with_name(trace.stem + '_reconnect.jsonl')
    if journal.exists():
        events = [json.loads(line) for line in journal.read_text(encoding='utf-8').splitlines()]
        segments = [analyze(Path(event['trace'])) for event in events
                    if event['event'] == 'connection_attempt' and Path(event['trace']).exists()]
        summary = dict(complete=bool(events and events[-1]['event'] == 'completed' and code == 0),
                       reconnects=sum(e['event'] == 'transport_interrupted' for e in events),
                       verified_frames=sum(s['verified_frames'] for s in segments),
                       segments=segments, termination=events[-1]['event'] if events else 'missing')
    summary['host_exit_code'] = code
    (output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2))
    return code or (0 if summary['complete'] else 1)


if __name__ == '__main__':
    raise SystemExit(main())
