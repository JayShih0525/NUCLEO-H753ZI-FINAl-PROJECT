"""Main-thread GUI with a single latest verified image shared by the receiver."""
import threading
import time
import json
from pathlib import Path


class UserStop(Exception):
    pass


class LatestDisplay:
    def __init__(self):
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.image = None
        self.version = 0
        self.consumed = 0
        self.skipped = 0
        self.status = 'Connecting / authenticating'

    def publish(self, image):
        with self.lock:
            if self.version != self.consumed:
                self.skipped += 1
            self.image = image
            self.version += 1
            self.status = ''

    def set_status(self, status):
        with self.lock:
            self.status = status

    def snapshot(self):
        with self.lock:
            self.consumed = self.version
            return self.image, self.version, self.status


def run_with_display(args, receiver, cv2):
    import numpy as np
    view = LatestDisplay()
    args._live_display = view
    args._stop_event = view.stop
    outcome = []
    def work():
        try:
            outcome.append((receiver(), None))
        except BaseException as error:
            outcome.append((None, error))
    worker = threading.Thread(target=work, name='camera-receiver')
    worker.start()
    title = f'Encrypted {args.device_name} stream'
    shown = None
    shown_at = time.monotonic()
    displayed = 0
    display_ms = 0.0
    try:
        while worker.is_alive():
            image, version, status = view.snapshot()
            if shown is not None and version == shown[0] and not status and time.monotonic() - shown_at > 1:
                status = 'Waiting for data'
            if shown != (version, status):
                panel = image.copy() if image is not None else np.zeros((240, 640, 3), dtype=np.uint8)
                if status:
                    cv2.rectangle(panel, (0, 0), (panel.shape[1], 28), (0, 0, 0), -1)
                    cv2.putText(panel, status, (6, 19), cv2.FONT_HERSHEY_SIMPLEX, .45, (0, 200, 255), 1)
                started = time.perf_counter()
                cv2.imshow(title, panel)
                display_ms += (time.perf_counter() - started) * 1000
                if shown is None or version != shown[0]:
                    shown_at = time.monotonic()
                    displayed += int(image is not None)
                shown = (version, status)
            if cv2.waitKey(1) & 0xff in (ord('q'), 27) or cv2.getWindowProperty(title, cv2.WND_PROP_VISIBLE) < 1:
                view.stop.set()
            time.sleep(.005)
    finally:
        view.stop.set()
        worker.join()  # Receiver polls cancellation during socket reads and retry waits.
        cv2.destroyAllWindows()
        print(f'[DISPLAY] shown={displayed} skipped={view.skipped} imshow_ms={display_ms:.3f}')
        if args.diagnostics is not None:
            path = Path(args.diagnostics).with_name(Path(args.diagnostics).stem + '_display.json')
            path.write_text(json.dumps(dict(shown=displayed, skipped=view.skipped,
                imshow_total_ms=display_ms, queue_capacity=1,
                note='Display overlaps reception; skipped means verified images not displayed, not lost packets.'),
                indent=2), encoding='utf-8')
    result, error = outcome[0]
    if error is not None:
        raise error
    return result
