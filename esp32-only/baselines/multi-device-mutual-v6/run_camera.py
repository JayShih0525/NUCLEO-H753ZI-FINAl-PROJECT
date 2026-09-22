"""Frozen v6 live viewer: discovery, mutual authentication, no duration or log files."""
from __future__ import annotations
import contextlib
import json
import math
import multiprocessing as mp
from pathlib import Path
import queue
import sys
import time
from types import SimpleNamespace

import settings

ROOT = Path(__file__).resolve().parent


class SilentOutput:
    def write(self, text):
        return len(text)

    def flush(self):
        pass


def validate_settings():
    if settings.RESOLUTION not in ('qvga', 'vga', 'svga'):
        raise ValueError('RESOLUTION must be qvga, vga or svga')
    if type(settings.REKEY_EVERY) is not int or not 3 <= settings.REKEY_EVERY <= 100000:
        raise ValueError('REKEY_EVERY must be 3..100000')
    for name in ('RESPONSE_TIMEOUT', 'DISCOVERY_TIMEOUT', 'FPS_INTERVAL'):
        value = getattr(settings, name)
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f'{name} must be positive and finite')


def configured_devices():
    from host.multi_camera import load_devices
    path = ROOT / 'devices.json'
    config = json.loads(path.read_text(encoding='utf-8-sig'))
    overrides = {item['name']: item['name'] + '.invalid' for item in config['devices']}
    # Explicit no-argument startup selects every registered entry, like --discover.
    return load_devices(path, overrides)


def receive_forever(device, stop, view, fps_output):
    from host.discover_devices import discover, resolve_devices
    from host.host_identity import load_identity
    from host.live_display import UserStop
    from host.tcp_connection import TcpConnection, TcpTransportError
    from host.serial_protocol import SerialProtocol, ProtocolError
    from host.pqc_host_demo import request_info, establish_session
    from host.pqc_camera_demo import configure_camera_mode, request_encrypted_frame, decode_jpeg
    from host.rekey_pipeline import RekeyPipeline
    from host.camera_replay import CameraReplayGuard
    from host.device_auth import load_trusted_key, fingerprint

    # Validate local credentials once before any endless discovery retries.
    load_identity()
    trusted = load_trusted_key(device.trust_key)
    if fingerprint(trusted) != device.fingerprint:
        raise ProtocolError('Pinned public key changed since startup')
    failures = 0
    next_report = time.monotonic() + settings.FPS_INTERVAL
    interval_start = time.monotonic()
    interval_frames = total_frames = 0

    def report():
        nonlocal next_report, interval_start, interval_frames
        now = time.monotonic()
        if now >= next_report:
            fps_output.write(f'[{device.name}] FPS={interval_frames / max(now - interval_start, .001):.2f}\n')
            fps_output.flush()
            interval_start, interval_frames = now, 0
            next_report = now + settings.FPS_INTERVAL

    while not stop.is_set():
        view.set_status('Searching / reconnecting (Q or Esc: stop all)')
        records = discover(settings.DISCOVERY_TIMEOUT)
        if stop.is_set():
            break
        matches = [record for record in records if record.fingerprint == device.fingerprint]
        report()
        if not matches:
            stop.wait(1)
            continue
        endpoint, = resolve_devices([device], matches)  # Ambiguity is fatal, never guess.
        try:
            with TcpConnection(endpoint.host, endpoint.port, timeout=settings.RESPONSE_TIMEOUT,
                               command_timeout=settings.RESPONSE_TIMEOUT, stop_event=stop) as connection:
                protocol = SerialProtocol(connection, trusted_key=trusted)
                protocol.profile = False
                request_info(protocol)
                if not protocol.mutual_auth or not protocol.pipeline_rekey:
                    raise ProtocolError('This milestone requires mutual-auth v6 and pipeline; refusing downgrade')
                protocol.rekey_interval = settings.REKEY_EVERY
                view.set_status('Mutual authentication (Q or Esc: stop all)')
                session = establish_session(protocol)
                configure_camera_mode(protocol, 'record', settings.RESOLUTION)
                pipeline = RekeyPipeline(protocol, session, settings.REKEY_EVERY)
                guard = CameraReplayGuard(settings.REKEY_EVERY)
                guard.begin_session(session[3])
                failures = 0
                while not stop.is_set():
                    result = request_encrypted_frame(protocol, pipeline.active[2], pipeline.active[3],
                                                     guard, pipeline=pipeline)
                    # Receive, authenticate and enforce ordering for every frame.
                    view.publish(decode_jpeg(result))
                    interval_frames += 1
                    total_frames += 1
                    report()
        except UserStop:
            break
        except TcpTransportError:
            view.set_status('Disconnected - searching again (Q or Esc: stop all)')
            stop.wait(min(2 ** min(failures, 3), 8))
            failures += 1
            report()
            # No old ciphertext is resumed. Re-discover IP and mutually authenticate.
    return 0


def worker(device, stop, errors):
    import cv2
    from host.live_display import run_with_display
    args = SimpleNamespace(device_name=device.name, diagnostics=None)
    fps_output = sys.stdout

    def receiver():
        # Share Q/Esc/window-close cancellation across all viewer processes.
        args._live_display.stop = stop
        args._stop_event = stop
        return receive_forever(device, stop, args._live_display, fps_output)

    try:
        with contextlib.redirect_stdout(SilentOutput()):
            run_with_display(args, receiver, cv2)
    except KeyboardInterrupt:
        stop.set()
    except BaseException as error:
        errors.put(f'{device.name}: {type(error).__name__}: {error}')
        stop.set()


def show_error(message):
    # Routine terminal output stays FPS-only. Failures remain visible in a dialog.
    try:
        import tkinter as tk
        from tkinter import messagebox
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror('Encrypted camera stopped', message, parent=root)
        root.destroy()
    except Exception:
        sys.stderr.write(message + '\n')  # Last resort if Windows cannot show dialogs.


def main():
    processes = []
    context = mp.get_context('spawn')
    stop = context.Event()
    errors = context.Queue()
    failure = None
    try:
        validate_settings()
        devices = configured_devices()
        for device in devices:
            process = context.Process(target=worker, args=(device, stop, errors), name=device.name)
            process.start()
            processes.append(process)
        while not stop.is_set() and any(process.is_alive() for process in processes):
            if sys.platform == 'win32':
                import msvcrt
                if msvcrt.kbhit() and msvcrt.getwch().lower() in ('q', '\x1b'):
                    stop.set()
            if any(process.exitcode is not None for process in processes):
                stop.set()
            time.sleep(.1)
    except KeyboardInterrupt:
        stop.set()
    except Exception as error:
        failure = str(error)
    finally:
        stop.set()
        for process in processes:
            process.join(timeout=settings.RESPONSE_TIMEOUT + settings.DISCOVERY_TIMEOUT + 3)
            if process.is_alive():
                process.terminate()
                process.join()
                failure = failure or 'A camera worker required forced shutdown.'
        try:
            while True:
                item = errors.get_nowait()
                failure = item if failure is None else failure + '\n' + item
        except queue.Empty:
            pass
        if failure is None and any(process.exitcode != 0 for process in processes):
            failure = 'A camera worker exited unexpectedly.'
        errors.close()
        errors.join_thread()
    if failure:
        show_error(failure)
        return 1
    return 0


if __name__ == '__main__':
    mp.freeze_support()
    raise SystemExit(main())
