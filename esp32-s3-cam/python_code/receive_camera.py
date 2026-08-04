#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

import serial

try:
    import cv2
    import numpy as np
except ImportError:
    cv2 = None
    np = None

MAGIC = b"CAM1"
HEADER_FORMAT = ">4sBBBBIIHHI"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)  # 24
VERSION = 1
PACKET_DATA = 1
PACKET_FRAME_END = 2
PACKET_ERROR = 3
MAX_FRAME_SIZE = 1024 * 1024
MAX_CHUNK_SIZE = 16384

STATUS_NAMES = {
    0x00: "OK",
    0x01: "CAMERA_FAILED",
    0x02: "FRAME_INVALID",
    0x03: "SPI_FAILED",
    0x04: "LENGTH_ERROR",
    0x05: "UART_FAILED",
}

@dataclass
class Frame:
    total_length: int
    chunk_count: int
    chunks: dict[int, bytes] = field(default_factory=dict)
    created: float = field(default_factory=time.monotonic)


def read_exact(port: serial.Serial, size: int) -> bytes:
    out = bytearray()
    while len(out) < size:
        part = port.read(size - len(out))
        if not part:
            raise TimeoutError(f"timeout: need {size}, got {len(out)}")
        out.extend(part)
    return bytes(out)


def find_magic(port: serial.Serial) -> None:
    window = bytearray()
    while True:
        b = port.read(1)
        if not b:
            raise TimeoutError("waiting for CAM1")
        window += b
        if len(window) > 4:
            del window[0]
        if bytes(window) == MAGIC:
            return


def decode_jpeg(encrypted: bytes) -> bytes:
    return bytes(b ^ 0xA5 for b in encrypted)


def show_or_save(jpeg: bytes, frame_id: int, save_dir: Path | None, no_display: bool) -> None:
    if save_dir is not None:
        save_dir.mkdir(parents=True, exist_ok=True)
        path = save_dir / f"frame_{frame_id:010d}.jpg"
        path.write_bytes(jpeg)
        print(f"saved: {path}", flush=True)

    if no_display or cv2 is None or np is None:
        return

    image = cv2.imdecode(np.frombuffer(jpeg, dtype=np.uint8), cv2.IMREAD_COLOR)
    if image is None:
        print(f"frame={frame_id}: OpenCV decode failed", file=sys.stderr, flush=True)
        return
    cv2.imshow("ESP32 -> H753 -> UART", image)
    cv2.waitKey(1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--save-dir", type=Path)
    ap.add_argument("--no-display", action="store_true")
    ap.add_argument("--clear-buffer", action="store_true", help="discard old serial bytes on startup")
    args = ap.parse_args()

    frames: dict[int, Frame] = {}
    packet_count = 0
    frame_count = 0
    last_activity = time.monotonic()

    with serial.Serial(args.port, args.baud, timeout=1.0, write_timeout=1.0) as port:
        if args.clear_buffer:
            port.reset_input_buffer()
        print(f"Opened {args.port} at {args.baud} baud", flush=True)

        while True:
            try:
                find_magic(port)
                rest = read_exact(port, HEADER_SIZE - 4)
                fields = struct.unpack(HEADER_FORMAT, MAGIC + rest)
                (_, version, packet_type, status, reserved,
                 frame_id, total_length, chunk_index, chunk_count,
                 payload_length) = fields

                packet_count += 1
                last_activity = time.monotonic()
                print(
                    f"packet={packet_count} type={packet_type} status={status} "
                    f"frame={frame_id} total={total_length} "
                    f"chunk={chunk_index}/{chunk_count} payload={payload_length}",
                    flush=True,
                )

                # Validate before trusting payload length.
                if version != VERSION or reserved != 0:
                    print("invalid header version/reserved; resync", file=sys.stderr, flush=True)
                    continue
                if total_length > MAX_FRAME_SIZE or payload_length > MAX_CHUNK_SIZE:
                    print("invalid header length; resync", file=sys.stderr, flush=True)
                    continue

                payload = read_exact(port, payload_length) if payload_length else b""

                if packet_type == PACKET_ERROR:
                    frames.pop(frame_id, None)
                    print(
                        f"ESP32 ERROR frame={frame_id}: "
                        f"{STATUS_NAMES.get(status, hex(status))}",
                        file=sys.stderr,
                        flush=True,
                    )
                    continue

                if status != 0:
                    print(f"nonzero packet status={status}", file=sys.stderr, flush=True)
                    frames.pop(frame_id, None)
                    continue

                if packet_type == PACKET_DATA:
                    if chunk_count == 0 or chunk_index >= chunk_count:
                        print("invalid chunk metadata", file=sys.stderr, flush=True)
                        continue
                    frame = frames.get(frame_id)
                    if frame is None:
                        frame = Frame(total_length, chunk_count)
                        frames[frame_id] = frame
                    if frame.total_length != total_length or frame.chunk_count != chunk_count:
                        print("frame metadata changed; restart frame", file=sys.stderr, flush=True)
                        frame = Frame(total_length, chunk_count)
                        frames[frame_id] = frame
                    frame.chunks[chunk_index] = payload
                    print(f"stored frame={frame_id}: {len(frame.chunks)}/{frame.chunk_count}", flush=True)

                elif packet_type == PACKET_FRAME_END:
                    frame = frames.pop(frame_id, None)
                    if frame is None:
                        print(f"FRAME_END without DATA for frame={frame_id}; waiting next frame", flush=True)
                        continue
                    missing = [i for i in range(frame.chunk_count) if i not in frame.chunks]
                    if missing:
                        print(f"frame={frame_id} missing chunks={missing}", file=sys.stderr, flush=True)
                        continue
                    encrypted = b"".join(frame.chunks[i] for i in range(frame.chunk_count))
                    if len(encrypted) != frame.total_length:
                        print(
                            f"frame={frame_id} length mismatch expected={frame.total_length} got={len(encrypted)}",
                            file=sys.stderr,
                            flush=True,
                        )
                        continue
                    jpeg = decode_jpeg(encrypted)
                    if len(jpeg) < 4 or jpeg[:2] != b"\xff\xd8" or jpeg[-2:] != b"\xff\xd9":
                        print(
                            f"frame={frame_id} invalid JPEG markers: "
                            f"start={jpeg[:2].hex()} end={jpeg[-2:].hex()}",
                            file=sys.stderr,
                            flush=True,
                        )
                        continue
                    frame_count += 1
                    print(f"FRAME OK id={frame_id} bytes={len(jpeg)} completed={frame_count}", flush=True)
                    show_or_save(jpeg, frame_id, args.save_dir, args.no_display)
                else:
                    print(f"unknown packet type={packet_type}", file=sys.stderr, flush=True)

                # Remove abandoned frames.
                now = time.monotonic()
                for fid in list(frames):
                    if now - frames[fid].created > 10:
                        print(f"drop stale frame={fid}", file=sys.stderr, flush=True)
                        del frames[fid]

            except TimeoutError:
                if time.monotonic() - last_activity >= 1:
                    print("waiting for next CAM1 packet...", flush=True)
                    last_activity = time.monotonic()
            except KeyboardInterrupt:
                break
            except serial.SerialException as exc:
                print(f"serial error: {exc}", file=sys.stderr)
                return 1

    if cv2 is not None:
        cv2.destroyAllWindows()
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
