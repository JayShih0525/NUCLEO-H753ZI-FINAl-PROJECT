"""Reconnect whole authenticated runs; never resume a partial encrypted frame."""
import copy
import json
import time
from datetime import datetime
from pathlib import Path
from host.tcp_connection import TcpTransportError
from host.live_display import UserStop


def supervise(args, run_once, cv2):
    deadline = time.monotonic() + args.seconds
    base = args.diagnostics or Path('diagnostics') / ('camera_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.jsonl')
    base.parent.mkdir(parents=True, exist_ok=True)
    journal = base.with_name(base.stem + '_reconnect.jsonl')
    attempt = 0
    view = getattr(args, "_live_display", None)
    with journal.open('x', encoding='utf-8') as log:
        def record(event, **fields):
            log.write(json.dumps(dict(event=event, attempt=attempt,
                                     wall_time=datetime.now().astimezone().isoformat(), **fields)) + '\n')
            log.flush()
        while time.monotonic() < deadline:
            if view is not None:
                if view.stop.is_set():
                    record("user_stop")
                    return 0
                view.set_status("Connecting / authenticating" if attempt == 0 else "Reconnecting / authenticating")
            current = copy.copy(args)
            current.seconds = deadline - time.monotonic()
            current.diagnostics = base if attempt == 0 else base.with_name(f'{base.stem}_attempt{attempt}.jsonl')
            record('connection_attempt', trace=str(current.diagnostics))
            try:
                result = run_once(current)
            except UserStop:
                record("user_stop")
                return 0
            except TcpTransportError as error:
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
                record('fatal_error', error_type=type(error).__name__, reason=str(error))
                raise
            else:
                record('completed', exit_code=result)
                return result
        record('duration_expired_disconnected')
        print('[WiFi] Test duration expired before recovery completed; see reconnect log.')
        return 1
