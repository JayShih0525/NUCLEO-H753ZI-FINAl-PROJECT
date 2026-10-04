"""Bounded UTF-8 log files for the long-running entry point only."""
import json
import re
import time
from pathlib import Path
import os


class InstanceLock:
    """OS-held lock; automatically released after a crash, no stale PID recovery."""
    def __init__(self, path, message='Another live camera launcher is already running'):
        self.path = Path(path)
        self.message = message

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.file = self.path.open('a+b')
        if self.path.stat().st_size == 0:
            self.file.write(b'0')
            self.file.flush()
        self.file.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            self.file.close()
            raise RuntimeError(self.message) from error
        return self

    def __exit__(self, *_):
        self.file.close()


class RollingFile:
    def __init__(self, path, max_bytes=2 * 1024 * 1024, backups=3):
        if max_bytes < 1024 or not 1 <= backups <= 10:
            raise ValueError('Invalid log capacity')
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.limit, self.backups = max_bytes, backups
        self.size = self.path.stat().st_size if self.path.exists() else 0
        self.file = self.path.open('a', encoding='utf-8', newline='')

    def write(self, text):
        data = text.encode('utf-8')
        if len(data) > self.limit:
            text = '[oversized log record omitted]\n'
            data = text.encode('utf-8')
        if self.size + len(data) > self.limit:
            self.file.close()
            for i in range(self.backups, 0, -1):
                source = self.path if i == 1 else Path(f'{self.path}.{i-1}')
                target = Path(f'{self.path}.{i}')
                if source.exists():
                    source.replace(target)
            self.file = self.path.open('w', encoding='utf-8', newline='')
            self.size = 0
        self.file.write(text)
        self.size += len(data)
        return len(text)

    def flush(self):
        self.file.flush()

    def close(self):
        self.file.close()


class LiveOutput:
    """Coalesce print fragments; normal mode keeps FPS samples and errors."""
    def __init__(self, file, console, name, mode='normal'):
        self.file, self.console, self.name, self.mode = file, console, name, mode
        self.pending = ''
        self.last_fps = -float('inf')

    def write(self, text):
        self.pending += text
        while '\n' in self.pending:
            line, self.pending = self.pending.split('\n', 1)
            fps = re.search(r'average=([0-9.]+) FPS', line)
            now = time.monotonic()
            sampled = fps is not None and now - self.last_fps >= 1
            if sampled:
                self.last_fps = now
                self.console.write(f'[{self.name}] average={fps[1]} FPS\n')
                self.console.flush()
            if self.mode == 'diagnostic' or sampled or any(x in line for x in ('[ERROR]', '[WiFi]', '[LIVE ERROR]')):
                self.file.write(line + '\n')
                self.file.flush()
        if len(self.pending) > 8192:
            self.pending = '[oversized console line omitted]'
        return len(text)

    def flush(self):
        self.file.flush()


class LiveTotals:
    """Constant-memory counters, including reconnect gaps; no sample arrays."""
    def __init__(self):
        self.started = time.monotonic()
        self.frames = self.errors = self.handshakes = 0

    def observe(self, event):
        self.frames += event['event'] == 'camera_verified'
        self.errors += event['event'] == 'run_error'
        self.handshakes += event['event'] == 'mutual_auth_verified'

    def save(self, path, termination):
        elapsed = time.monotonic() - self.started
        Path(path).write_text(json.dumps(dict(termination=termination, verified_frames=self.frames,
            elapsed_seconds=elapsed, total_avg_fps=self.frames / elapsed if elapsed else 0,
            attempt_errors=self.errors, successful_handshakes=self.handshakes,
            note='Current invocation only; includes discovery and reconnect gaps. No percentile samples retained.'),
            indent=2), encoding='utf-8')
