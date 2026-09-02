from __future__ import annotations

import struct
import time
from dataclasses import dataclass

import serial


MAGIC = b"CAM1"
MAX_JPEG_SIZE = 1024 * 1024


@dataclass(frozen=True)
class CameraFrame:
    frame_id: int
    jpeg: bytes


def read_exact(port: serial.Serial, size: int) -> bytes:
    output = bytearray()
    while len(output) < size:
        chunk = port.read(size - len(output))
        if not chunk:
            raise TimeoutError(f"received {len(output)} of {size} bytes")
        output.extend(chunk)
    return bytes(output)


def wait_for_magic(port: serial.Serial, timeout: float = 10.0) -> None:
    window = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = port.read(1)
        if not value:
            continue
        window.extend(value)
        if len(window) > len(MAGIC):
            del window[0]
        if bytes(window) == MAGIC:
            return
    raise TimeoutError("timed out waiting for CAM1 frame")


def receive_frame(port: serial.Serial) -> CameraFrame:
    wait_for_magic(port)
    frame_id, jpeg_length = struct.unpack(">II", read_exact(port, 8))
    if not 4 <= jpeg_length <= MAX_JPEG_SIZE:
        raise ValueError(f"invalid JPEG length: {jpeg_length}")
    jpeg = read_exact(port, jpeg_length)
    if not (jpeg.startswith(b"\xff\xd8") and jpeg.endswith(b"\xff\xd9")):
        raise ValueError(f"frame {frame_id} has invalid JPEG markers")
    return CameraFrame(frame_id, jpeg)


def open_camera_port(name: str, baud: int) -> serial.Serial:
    port = serial.Serial(
        name,
        baudrate=baud,
        timeout=5.0,
        write_timeout=5.0,
        rtscts=False,
        dsrdtr=False,
        xonxoff=False,
    )
    port.dtr = False
    port.rts = False
    time.sleep(2.5)
    port.reset_input_buffer()
    port.reset_output_buffer()
    return port
