"""Reconnect whole authenticated runs; never resume a partial encrypted frame."""
import copy
import json
import time
import hashlib
from datetime import datetime
from pathlib import Path
from contextlib import nullcontext
from host.tcp_connection import TcpTransportError
from host.live_display import UserStop


def rediscover_endpoint(args, timeout):
    """Resolve a pinned identity; advertisements never replace the trust key."""
    from host.discover_devices import discover
    fingerprint = hashlib.sha256(args._trusted_key).hexdigest()
    try:
        advertisements = discover(timeout)
    except OSError as error:
        raise TcpTransportError(f'Discovery network unavailable: {error}') from error
    matches = {(item.host, item.port) for item in advertisements
               if item.fingerprint == fingerprint}
    if len(matches) > 1:
        raise ValueError('Ambiguous discovery for pinned device; refusing to choose an endpoint')
    if not matches:
        raise TcpTransportError('Pinned device not discovered yet')
    return matches.pop()


def supervise(args, run_once, cv2):
    continuous = getattr(args, 'continuous', False)
    deadline = float('inf') if continuous else time.monotonic() + args.seconds
    base = args.diagnostics or Path('diagnostics') / ('camera_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.jsonl')
    base.parent.mkdir(parents=True, exist_ok=True)
    journal = base.with_name(base.stem + '_reconnect.jsonl')
    attempt = 0
    failures = 0
    endpoint = (getattr(args, 'host', None), getattr(args, 'tcp_port', 9000))
    view = getattr(args, "_live_display", None)
    stop = getattr(args, '_stop_event', None)
    shared_journal = getattr(args, '_journal_file', None)
    with (nullcontext(shared_journal) if shared_journal is not None else journal.open('x', encoding='utf-8')) as log:
        def record(event, **fields):
            log.write(json.dumps(dict(event=event, attempt=attempt,
                                     wall_time=datetime.now().astimezone().isoformat(), **fields)) + '\n')
            log.flush()
        while time.monotonic() < deadline:
            if stop is not None and stop.is_set():
                record('user_stop')
                return 0
            if view is not None:
                if view.stop.is_set():
                    record("user_stop")
                    return 0
                view.set_status("Connecting / authenticating" if attempt == 0 else "Reconnecting / authenticating")
            current = copy.copy(args)
            current.seconds = deadline - time.monotonic()
            current.diagnostics = base if continuous or attempt == 0 else base.with_name(f'{base.stem}_attempt{attempt}.jsonl')
            record('connection_attempt', trace=str(current.diagnostics))
            try:
                if getattr(args, 'rediscover', False) and (endpoint[0] == 'auto' or failures >= 2):
                    if view is not None:
                        view.set_status('Searching for trusted camera')
                    budget = min(getattr(args, 'discovery_timeout', 5.0), deadline - time.monotonic())
                    if budget <= 0:
                        break
                    endpoint = rediscover_endpoint(args, budget)
                    record('endpoint_discovered', host=endpoint[0], port=endpoint[1], authenticated=False)
                    if view is not None:
                        view.set_status('Connecting / authenticating')
                current.host, current.tcp_port = endpoint
                current.seconds = deadline - time.monotonic()
                if current.seconds <= 0:
                    break
                result = run_once(current)
            except UserStop:
                record("user_stop")
                return 0
            except TcpTransportError as error:
                failures += 1
                # ProtocolError/InvalidTag/signature failures deliberately escape.
                # Only transport failures may trigger a fresh authenticated run.
                record('transport_interrupted', reason=str(error))
                print('[WiFi] Connection interrupted; retrying with fresh device authentication.', flush=True)
                if view is not None:
                    view.set_status("Disconnected - reconnecting")
                elif args.display:
                    import numpy as np
                    panel = np.zeros((240, 640, 3), dtype=np.uint8)
                    cv2.putText(panel, 'Disconnected - reconnecting (Q / Esc to quit)',
                                (12, 120), cv2.FONT_HERSHEY_SIMPLEX, .55, (0, 200, 255), 1)
                    cv2.imshow(f"Encrypted {getattr(args, 'device_name', 'ESP32-S3-CAM')} stream", panel)
                delay_end = min(deadline, time.monotonic() + min(2 ** min(attempt, 3), 8))
                while time.monotonic() < delay_end:
                    if stop is not None and stop.is_set():
                        record('user_stop')
                        return 0
                    if view is not None and view.stop.wait(.05):
                        record('user_stop')
                        return 0
                    if view is None and args.display and cv2.waitKey(50) & 0xff in (ord('q'), 27):
                        record('user_stop')
                        return 0
                    if not args.display:
                        time.sleep(.05)
                attempt += 1
            except Exception as error:
                if view is not None:
                    view.set_status('Stopped: validation or local error')
                record('fatal_error', error_type=type(error).__name__, reason=str(error))
                raise
            else:
                record('completed', exit_code=result)
                return result
        record('duration_expired_disconnected')
        print('[WiFi] Test duration expired before recovery completed; see reconnect log.')
        return 1
