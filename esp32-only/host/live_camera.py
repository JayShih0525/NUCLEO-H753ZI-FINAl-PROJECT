"""No-argument, continuous, independently recovering cameras with bounded logs."""
import contextlib
import hashlib
import json
import multiprocessing as mp
from pathlib import Path
import sys
import time

from host.multi_camera import ROOT, load_devices


def load_settings(path):
    settings = dict(resolution='qvga', rekey_every=10, response_timeout=10.0,
                    discovery_timeout=5.0, display=True, log_mode='normal',
                    log_max_bytes=2097152, log_backups=3)
    if Path(path).exists():
        custom = json.loads(Path(path).read_text(encoding='utf-8-sig'))
        if not isinstance(custom, dict) or set(custom) - set(settings):
            raise ValueError('Unknown live settings')
        settings.update(custom)
    import math
    if settings['resolution'] not in ('qvga', 'vga', 'svga') or settings['log_mode'] not in ('normal', 'diagnostic'):
        raise ValueError('Invalid resolution or log mode')
    if type(settings['display']) is not bool:
        raise ValueError('display must be boolean')
    for key in ('response_timeout', 'discovery_timeout'):
        value = settings[key]
        if type(value) not in (int, float) or not math.isfinite(value) or not 0 < value <= 60:
            raise ValueError(f'{key} must be finite and in (0, 60]')
    for key, low, high in (('rekey_every', 3, 100000), ('log_max_bytes', 1024, 104857600), ('log_backups', 1, 10)):
        if type(settings[key]) is not int or not low <= settings[key] <= high:
            raise ValueError(f'Invalid {key}')
    return settings


def worker(device, key, settings, stop):
    import cv2
    from host.pqc_camera_demo import parse_arguments, run_once
    from host.live_display import run_with_display, UserStop
    from host.wifi_recovery import supervise
    from host.live_logging import RollingFile, LiveOutput, LiveTotals
    folder = ROOT / 'diagnostics' / 'live' / device.name
    files = []
    totals = LiveTotals()
    termination = 'failed'
    try:
        for name in ('terminal.txt', 'trace.jsonl', 'reconnect.jsonl'):
            files.append(RollingFile(folder / name, settings['log_max_bytes'], settings['log_backups']))
        totals.save(folder / 'summary.json', 'running')
        args = parse_arguments(['--host', 'auto', '--device-name', device.name,
            '--trust-key', str(device.trust_key), '--rekey-mode', 'pipeline', '--memory-every', '0',
            '--resolution', settings['resolution'], '--rekey-every', str(settings['rekey_every']),
            '--response-timeout', str(settings['response_timeout']), '--rediscover',
            '--discovery-timeout', str(settings['discovery_timeout'])])
        args.continuous = args.auto_reconnect = args.require_mutual = True
        args._trusted_key, args._stop_event = key, stop
        args.display = settings['display']
        args.log_mode = settings['log_mode']
        args.profile = settings['log_mode'] == 'diagnostic'
        args.diagnostics = folder / 'trace.jsonl'
        args._trace_file, args._journal_file = files[1:]
        args._trace_observer = totals.observe
        output = LiveOutput(files[0], sys.stdout, device.name, settings['log_mode'])
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            result = (run_with_display(args, lambda: supervise(args, run_once, cv2), cv2)
                      if args.display else supervise(args, run_once, cv2))
        termination = 'user_stop' if stop.is_set() else 'completed'
        return result
    except (UserStop, KeyboardInterrupt):
        termination = 'user_stop'
    except Exception as error:
        message = f'[{device.name}] [LIVE ERROR] {type(error).__name__}: {error}'
        print(message, file=sys.stderr, flush=True)
        if files:
            files[0].write(message + '\n')
        raise
    finally:
        for file in files:
            file.close()
        if folder.exists():
            totals.save(folder / 'summary.json', termination)


def run():
    settings = load_settings(ROOT / 'live_settings.json')
    config = ROOT / 'devices.json'
    data = json.loads(config.read_text(encoding='utf-8-sig'))
    # Same meaning as multi_camera --discover: all registered entries.
    overrides = {item['name']: f"{item['name']}.invalid" for item in data['devices']}
    devices = load_devices(config, overrides)
    keys = [device.trust_key.read_bytes() for device in devices]
    if any(hashlib.sha256(key).hexdigest() != device.fingerprint for device, key in zip(devices, keys)):
        raise ValueError('Trust file changed during preflight')
    ctx = mp.get_context('spawn')
    stop = ctx.Event()
    processes = []
    print('Live cameras: Q / Esc in a camera window or Ctrl+C in terminal stops all. Logs: diagnostics/live/')
    try:
        for device, key in zip(devices, keys):
            process = ctx.Process(target=worker, args=(device, key, settings, stop), name=device.name)
            process.start()
            processes.append(process)
        reported = set()
        while any(p.is_alive() for p in processes) and not stop.is_set():
            for process in processes:
                if process.exitcode is not None and process.pid not in reported:
                    print(f'{process.name}: stopped code={process.exitcode}', flush=True)
                    reported.add(process.pid)
            time.sleep(.1)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        deadline = time.monotonic() + settings['discovery_timeout'] + settings['response_timeout'] + 5
        for process in processes:
            process.join(max(0, deadline - time.monotonic()))
            if process.is_alive():
                print(f'{process.name}: forced stop; final summary may be incomplete', file=sys.stderr)
                process.terminate()
                process.join()
    return int(any(p.exitcode != 0 for p in processes))


def main():
    from host.live_logging import InstanceLock
    with InstanceLock(ROOT / 'diagnostics' / 'live' / 'launcher.lock'):
        return run()


if __name__ == '__main__':
    raise SystemExit(main())
