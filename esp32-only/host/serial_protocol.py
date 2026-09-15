from __future__ import annotations

import struct
import time
from dataclasses import dataclass, field
from typing import Callable

import serial


class ProtocolError(RuntimeError):
    pass


@dataclass
class SerialProtocol:
    serial_port: serial.Serial
    diagnostic: Callable[[dict], None] | None = None
    context: dict = field(default_factory=dict)
    command_id: int = 0
    trusted_key: bytes | None = None

    def trace(self, event: str, **values) -> None:
        if self.diagnostic is not None:
            self.diagnostic(dict(event=event, monotonic=time.monotonic(), command_id=self.command_id, **self.context, **values))

    def send_line(self, line: str) -> None:
        self.command_id += 1
        self.trace('command', command=line)
        started = time.monotonic()
        self.serial_port.write(line.encode("ascii") + b"\n")
        self.serial_port.flush()
        self.trace('command_sent', command=line, elapsed_ms=(time.monotonic() - started) * 1000)

    def read_line(self) -> str:
        started = time.monotonic()
        try:
            raw = self.serial_port.readline()
        except (TimeoutError, OSError) as error:
            self.trace('line_read_error', elapsed_ms=(time.monotonic() - started) * 1000,
                       error_type=type(error).__name__, reason=str(error))
            raise
        if not raw:
            raise TimeoutError("Timed out waiting for a line from ESP32")
        line = raw.decode("utf-8", errors="replace").strip()
        self.trace('response', line=line)
        return line

    def read_until_prefix(self, prefix: str, timeout: float = 10.0) -> str:
        deadline = time.monotonic() + timeout
        observed: list[str] = []
        while time.monotonic() < deadline:
            try:
                line = self.read_line()
            except TimeoutError:
                continue
            if line:
                observed.append(line)
            if line.startswith(prefix):
                return line
        raise TimeoutError(
            f"Timed out waiting for {prefix!r}; observed: {observed[-8:]}"
        )

    def expect(self, expected: str) -> str:
        line = self.read_line()
        if line != expected:
            raise ProtocolError(f"Expected {expected!r}, received {line!r}")
        return line

    def expect_prefix(self, prefix: str) -> str:
        line = self.read_line()
        if not line.startswith(prefix):
            raise ProtocolError(f"Expected prefix {prefix!r}, received {line!r}")
        return line

    def send_frame(self, data: bytes) -> None:
        payload = bytes(data)
        self.serial_port.write(struct.pack(">I", len(payload)))
        self.serial_port.write(payload)
        self.serial_port.flush()

    def receive_frame(self, maximum: int = 1 << 20, *, label: str = 'binary') -> bytes:
        header = self._read_exact(4, label=label, phase='header')
        length = struct.unpack(">I", header)[0]
        self.trace('frame_header', label=label, header_hex=header.hex(), declared=length, maximum=maximum)
        if length > maximum:
            raise ProtocolError(f"Frame length {length} exceeds maximum {maximum}")
        return self._read_exact(length, label=label, phase='payload')

    def _read_exact(self, length: int, *, label: str = 'binary', phase: str = 'payload') -> bytes:
        output = bytearray()
        started = time.monotonic()
        try:
            while len(output) < length:
                chunk = self.serial_port.read(length - len(output))
                if not chunk:
                    raise TimeoutError(f'{label}.{phase}: received {len(output)}/{length} bytes')
                output.extend(chunk)
        except Exception as error:
            self.trace('read_error', label=label, phase=phase, expected=length,
                       received=len(output), elapsed_ms=(time.monotonic()-started)*1000,
                       header_hex=output.hex() if phase == 'header' else None,
                       error=str(error))
            raise
        self.trace('read_complete', label=label, phase=phase, expected=length,
                   received=len(output), elapsed_ms=(time.monotonic()-started)*1000)
        return bytes(output)
