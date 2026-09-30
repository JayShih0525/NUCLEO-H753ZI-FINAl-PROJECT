"""Bounded file buffering: flush periodic samples in batches, failures immediately."""
import json
import time


class TraceWriter:
    IMPORTANT = frozenset({'run_error', 'user_stop', 'run_complete', 'recovery_error',
                           'stream_end', 'tcp_write_error', 'tcp_command_deadline',
                           'transport_interrupted', 'port_open_begin'})

    def __init__(self, file, interval=1.0):
        self.file = file
        self.interval = interval
        self.last_flush = time.monotonic()

    def write(self, event):
        # TextIO's fixed-size buffer, not an ever-growing in-memory event list.
        self.file.write(json.dumps(event, ensure_ascii=False) + '\n')
        now = time.monotonic()
        if event.get('event') in self.IMPORTANT or now - self.last_flush >= self.interval:
            self.file.flush()
            self.last_flush = now
