from __future__ import annotations

import struct
import time
from dataclasses import dataclass

import serial


class ProtocolError(RuntimeError):
    pass


@dataclass
class SerialProtocol:
    serial_port: serial.Serial

    def send_line(self, line: str) -> None:
        self.serial_port.write(line.encode("ascii") + b"\n")
        self.serial_port.flush()

    def read_line(self) -> str:
        raw = self.serial_port.readline()
        if not raw:
            raise TimeoutError("Timed out waiting for a line from ESP32")
        return raw.decode("utf-8", errors="replace").strip()

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

    def receive_frame(self, maximum: int = 1 << 20) -> bytes:
        header = self._read_exact(4)
        length = struct.unpack(">I", header)[0]
        if length > maximum:
            raise ProtocolError(f"Frame length {length} exceeds maximum {maximum}")
        return self._read_exact(length)

    def _read_exact(self, length: int) -> bytes:
        output = bytearray()
        while len(output) < length:
            chunk = self.serial_port.read(length - len(output))
            if not chunk:
                raise TimeoutError(
                    f"Timed out after {len(output)} of {length} frame bytes"
                )
            output.extend(chunk)
        return bytes(output)
