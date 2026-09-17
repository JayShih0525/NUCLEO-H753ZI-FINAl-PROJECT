"""Launch independent authenticated camera workers from a device list."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class Device:
    name: str
    host: str
    port: int
    trust_key: Path
    fingerprint: str


def load_devices(path, overrides=None):
    path = Path(path).resolve()
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(data, dict) or set(data) != {'devices'} or not isinstance(data['devices'], list):
        raise ValueError('Configuration must contain a devices list only')
    if overrides:
        known = {item.get('name') for item in data['devices'] if isinstance(item, dict)}
        if set(overrides) - known:
            raise ValueError('Unknown device name in --device')
    devices, names, endpoints, identities = [], set(), set(), set()
    for item in data['devices']:
        if not isinstance(item, dict) or set(item) - {'name', 'host', 'port', 'trust_key', 'enabled'}:
            raise ValueError('Invalid device fields')
        if type(item.get('enabled', True)) is not bool:
            raise ValueError('enabled must be true or false')
        if overrides:
            if item.get('name') not in overrides:
                continue
            item = item | {'host': overrides[item['name']], 'enabled': True}
        if not item.get('enabled', True):
            continue
        name, host, key = (item.get(field) for field in ('name', 'host', 'trust_key'))
        port = item.get('port', 9000)
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,47}', name):
            raise ValueError('Device name must use 1-48 letters, digits, underscores or hyphens')
        if name.upper() in {'CON', 'PRN', 'AUX', 'NUL'} | {f'{p}{n}' for p in ('COM', 'LPT') for n in range(1, 10)}:
            raise ValueError('Device name is reserved on Windows')
        if not isinstance(host, str) or not host or host.strip() != host or any(c.isspace() for c in host):
            raise ValueError(f'{name}: host is required')
        if type(port) is not int or not 1 <= port <= 65535:
            raise ValueError(f'{name}: invalid port')
        if not isinstance(key, str) or not key:
            raise ValueError(f'{name}: trust_key is required (no default fallback)')
        key_path = (path.parent / key).resolve()
        public_key = key_path.read_bytes()
        if len(public_key) != 1312:
            raise ValueError(f'{name}: expected a 1312-byte ML-DSA-44 public key')
        fingerprint = hashlib.sha256(public_key).hexdigest()
        endpoint = (host.casefold(), port)
        if name.casefold() in names or endpoint in endpoints or fingerprint in identities:
            raise ValueError(f'{name}: duplicate name, endpoint or device identity')
        names.add(name.casefold())
        endpoints.add(endpoint)
        identities.add(fingerprint)
        devices.append(Device(name, host, port, key_path, fingerprint))
    if not devices:
        raise ValueError('No enabled devices; fill in the current IPs and enable the entries')
    return devices


def command_for(device, folder, seconds, rekey, memory, display, rekey_mode='blocking'):
    command = [sys.executable, '-B', '-u', '-m', 'host.pqc_camera_demo',
               '--host', device.host, '--tcp-port', str(device.port),
               '--trust-key', str(device.trust_key), '--device-name', device.name,
               '--mode', 'record', '--seconds', str(seconds), '--rekey-every', str(rekey),
               '--memory-every', str(memory), '--profile', '--diagnostics', str(folder / 'trace.jsonl')]
    return command + (['--rekey-mode', rekey_mode] if rekey_mode != 'blocking' else []) + (['--display'] if display else [])


def stop_workers(workers, grace=5):
    """Interrupt first; reap every worker, forcing only those that remain blocked."""
    for worker in workers:
        process = worker['process']
        if process.poll() is None:
            try:
                process.send_signal(signal.CTRL_BREAK_EVENT if os.name == 'nt' else signal.SIGINT)
            except (OSError, ValueError):
                process.terminate()
    deadline = time.monotonic() + grace
    for worker in workers:
        process = worker['process']
        try:
            process.wait(timeout=max(0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            worker['forced_stop'] = True
            process.kill()
            process.wait()


def report_fps(worker, *, final=False):
    """Read existing worker logs without changing their output or pipe behavior."""
    with worker['log_path'].open('r', encoding='utf-8', errors='replace') as source:
        source.seek(worker.get('log_offset', 0))
        while True:
            position = source.tell()
            line = source.readline()
            if not line:
                break
            if not line.endswith('\n') and not final:
                source.seek(position)
                break
            match = re.search(r'\[PASS\] frame=(\d+).*average=([0-9.]+) FPS', line)
            if match:
                worker['latest_fps'] = (match[1], match[2])
        worker['log_offset'] = source.tell()
    latest = worker.get('latest_fps')
    now = time.monotonic()
    if latest and latest != worker.get('reported_fps') and (final or now - worker.get('fps_report_at', -float('inf')) >= 1):
        print(f"[{worker['name']}] average={latest[1]} FPS | frame={latest[0]}", flush=True)
        worker['reported_fps'] = latest
        worker['fps_report_at'] = now


def run_devices(devices, output, seconds, rekey, memory, display, *, popen=subprocess.Popen, rekey_mode='blocking'):
    output.mkdir(parents=True, exist_ok=False)
    workers, interrupted = [], False
    failure = None
    try:
        for device in devices:
            folder = output / device.name
            folder.mkdir()
            # Snapshot public trust bytes so all workers use the preflight identity.
            public_key = device.trust_key.read_bytes()
            if hashlib.sha256(public_key).hexdigest() != device.fingerprint:
                raise ValueError(f'{device.name}: trust file changed after preflight')
            pinned = folder / 'device.pub'
            pinned.write_bytes(public_key)
            worker_device = Device(device.name, device.host, device.port, pinned, device.fingerprint)
            command = command_for(worker_device, folder, seconds, rekey, memory, display, rekey_mode)
            log = (folder / 'terminal.txt').open('w', encoding='utf-8')
            try:
                process = popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                env=os.environ | {'PYTHONIOENCODING': 'utf-8'},
                                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0)
            except BaseException:
                log.close()
                raise
            workers.append(dict(name=device.name, process=process, log=log,
                                log_path=folder / 'terminal.txt',
                                command=command, fingerprint=device.fingerprint, forced_stop=False))
            print(f'[{device.name}] started pid={process.pid}; logs: {folder}', flush=True)
        pending = list(workers)
        while pending:
            for worker in pending[:]:
                code = worker['process'].poll()
                report_fps(worker, final=code is not None)
                if code is not None:
                    print(f"[{worker['name']}] exited code={code}", flush=True)
                    pending.remove(worker)
            if pending:
                time.sleep(.1)
    except KeyboardInterrupt:
        interrupted = True
        print('Stopping all camera workers...', flush=True)
    except Exception as error:
        failure = str(error)
    finally:
        if any(w['process'].poll() is None for w in workers):
            stop_workers(workers)
        for worker in workers:
            worker['log'].close()
            report_fps(worker, final=True)
        summary = dict(interrupted=interrupted, launcher_error=failure,
                       devices=[dict(name=w['name'], pid=w['process'].pid,
                                     exit_code=w['process'].returncode, forced_stop=w['forced_stop'],
                                     fingerprint=w['fingerprint'], command=w['command']) for w in workers])
        # Offline only: all processes have exited and their logs are closed.
        from host.multi_summary import enrich_summary
        try:
            enrich_summary(output, summary)
        except Exception as error:
            summary['aggregation_error'] = str(error)
        (output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
        print(f'Summary: {output / "summary.json"}', flush=True)
    if failure:
        print(f'Launcher error: {failure}', file=sys.stderr)
    return 130 if interrupted else int(bool(failure) or any(w['process'].returncode != 0 for w in workers))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=ROOT / 'devices.json')
    parser.add_argument('--device', action='append', default=[], metavar='NAME=IP',
                        help='select a configured device and override its IP; repeat for N devices')
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--rekey-every', type=int, default=10)
    parser.add_argument('--rekey-mode', choices=('blocking', 'pipeline'), default='blocking')
    parser.add_argument('--memory-every', type=int, default=10)
    parser.add_argument('--display', action='store_true')
    parser.add_argument('--dry-run', action='store_true', help='validate configuration without connecting')
    args = parser.parse_args(argv)
    if args.rekey_mode == 'pipeline' and args.rekey_every < 3:
        parser.error('pipeline requires rekey-every >= 3')
    if not math.isfinite(args.seconds) or args.seconds <= 0 or not 1 <= args.rekey_every <= 100000 or args.memory_every < 0:
        parser.error('seconds must be positive/finite; rekey 1..100000; memory-every >= 0')
    try:
        overrides = {}
        for value in args.device:
            name, separator, host = value.partition('=')
            if not separator or not name or not host or name in overrides:
                raise ValueError('--device must be NAME=IP, with each name specified once')
            overrides[name] = host
        devices = load_devices(args.config, overrides)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    for device in devices:
        print(f'{device.name}: {device.host}:{device.port}, identity={device.fingerprint}')
    if args.dry_run:
        print(f'Configuration valid: {len(devices)} devices; no connections made.')
        return 0
    output = ROOT / 'diagnostics' / 'multi' / datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    return run_devices(devices, output, args.seconds, args.rekey_every, args.memory_every, args.display,
                       rekey_mode=args.rekey_mode)


if __name__ == '__main__':
    raise SystemExit(main())
