#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import time
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Dict, Optional, Tuple

import serial

try:
    import cv2
    import numpy as np
except ImportError:
    cv2 = None
    np = None


MAGIC = b"CAM1"
HEADER_SIZE = 24
PROTOCOL_VERSION = 1

PACKET_DATA = 0x01
PACKET_FRAME_END = 0x02
PACKET_ERROR = 0x03
PACKET_PERFORMANCE = 0x04

STATUS_NAMES = {
    0x00: "OK",
    0x01: "CAMERA_FAILED",
    0x02: "FRAME_INVALID",
    0x03: "SPI_FAILED",
    0x04: "LENGTH_ERROR",
    0x05: "OUTPUT_FAILED",
}

SPI_PHASE_NAMES = {
    0: "NONE",
    1: "WAIT_REQUEST_HEADER",
    2: "WAIT_HEADER_ACK",
    3: "INVALID_HEADER_ACK",
    4: "WAIT_REQUEST_PAYLOAD",
    5: "WAIT_FINAL_HEADER",
    6: "INVALID_FINAL_HEADER",
    7: "WAIT_RESPONSE_PAYLOAD",
    8: "WAIT_FINAL_READY_LOW",
}


@dataclass
class PacketHeader:
    version: int
    packet_type: int
    status: int
    frame_id: int
    total_length: int
    chunk_index: int
    chunk_count: int
    payload_length: int


@dataclass
class FrameAssembly:
    total_length: int
    chunk_count: int
    chunks: Dict[int, bytes]


def read_exact(port: BinaryIO, size: int) -> bytes:
    data = bytearray()

    while len(data) < size:
        chunk = port.read(size - len(data))

        if not chunk:
            raise TimeoutError(
                f"serial timeout: wanted={size}, received={len(data)}"
            )

        data.extend(chunk)

    return bytes(data)


def find_magic(port: BinaryIO) -> None:
    matched = 0

    while True:
        byte = port.read(1)

        if not byte:
            print("waiting for next CAM1 packet...")
            continue

        if byte[0] == MAGIC[matched]:
            matched += 1

            if matched == len(MAGIC):
                return
        else:
            matched = 1 if byte[0] == MAGIC[0] else 0


def read_packet(port: BinaryIO) -> Tuple[PacketHeader, bytes]:
    find_magic(port)

    rest = read_exact(port, HEADER_SIZE - len(MAGIC))
    raw_header = MAGIC + rest

    version = raw_header[4]
    packet_type = raw_header[5]
    status = raw_header[6]
    reserved = raw_header[7]

    frame_id = struct.unpack(">I", raw_header[8:12])[0]
    total_length = struct.unpack(">I", raw_header[12:16])[0]
    chunk_index = struct.unpack(">H", raw_header[16:18])[0]
    chunk_count = struct.unpack(">H", raw_header[18:20])[0]
    payload_length = struct.unpack(">I", raw_header[20:24])[0]

    if version != PROTOCOL_VERSION:
        raise ValueError(f"unsupported protocol version: {version}")

    if reserved != 0:
        raise ValueError(f"reserved field is not zero: {reserved}")

    if payload_length > 1024 * 1024:
        raise ValueError(f"payload too large: {payload_length}")

    payload = read_exact(port, payload_length) if payload_length else b""

    return (
        PacketHeader(
            version=version,
            packet_type=packet_type,
            status=status,
            frame_id=frame_id,
            total_length=total_length,
            chunk_index=chunk_index,
            chunk_count=chunk_count,
            payload_length=payload_length,
        ),
        payload,
    )


def decode_spi_header(raw: bytes) -> str:
    if len(raw) != 16:
        return f"SPI debug payload length={len(raw)}, expected=16"

    magic = struct.unpack(">I", raw[0:4])[0]
    sequence = struct.unpack(">I", raw[4:8])[0]
    payload_length = struct.unpack(">I", raw[8:12])[0]
    command = raw[12]
    status = raw[13]
    flags = raw[14]
    reserved = raw[15]

    return (
        f"magic=0x{magic:08X} "
        f"sequence={sequence} "
        f"payloadLength={payload_length} "
        f"command=0x{command:02X} "
        f"status=0x{status:02X} "
        f"flags=0x{flags:02X} "
        f"reserved=0x{reserved:02X}"
    )


def xor_a5(data: bytes) -> bytes:
    return bytes(value ^ 0xA5 for value in data)


def display_or_save_frame(
    frame_id: int,
    encrypted_data: bytes,
    save_dir: Optional[Path],
    no_display: bool,
) -> bool:
    jpeg_data = xor_a5(encrypted_data)

    if len(jpeg_data) < 4:
        print(f"Frame {frame_id}: JPEG too short")
        return False

    if not (
        jpeg_data.startswith(b"\xFF\xD8")
        and jpeg_data.endswith(b"\xFF\xD9")
    ):
        print(
            f"Frame {frame_id}: invalid JPEG markers "
            f"start={jpeg_data[:2].hex()} "
            f"end={jpeg_data[-2:].hex()}"
        )
        return False

    if save_dir is not None:
        save_dir.mkdir(parents=True, exist_ok=True)
        filename = save_dir / f"frame_{frame_id:08d}.jpg"
        filename.write_bytes(jpeg_data)

    if not no_display:
        if cv2 is None or np is None:
            print(
                "OpenCV/Numpy not installed; "
                "use --no-display or install them"
            )
            return True

        image_array = np.frombuffer(jpeg_data, dtype=np.uint8)
        image = cv2.imdecode(image_array, cv2.IMREAD_COLOR)

        if image is None:
            print(f"Frame {frame_id}: cv2.imdecode failed")
            return False

        cv2.imshow("ESP32-S3 Camera", image)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            raise KeyboardInterrupt

    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Receive CAM1 packets and performance data."
    )

    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--save-dir", type=Path)
    parser.add_argument("--no-display", action="store_true")
    parser.add_argument("--quiet-packets", action="store_true")

    args = parser.parse_args()

    assemblies: Dict[int, FrameAssembly] = {}
    packet_count = 0
    completed_frames = 0
    invalid_frames = 0
    start_time = time.monotonic()

    try:
        with serial.Serial(
            args.port,
            args.baud,
            timeout=1.0,
            write_timeout=1.0,
        ) as port:
            port.reset_input_buffer()

            print(f"Opened {args.port} at {args.baud} baud")

            while True:
                try:
                    header, payload = read_packet(port)
                except TimeoutError as exc:
                    print(exc)
                    continue
                except ValueError as exc:
                    print(f"Packet parse error: {exc}")
                    continue

                packet_count += 1

                if not args.quiet_packets:
                    print(
                        f"packet={packet_count} "
                        f"type={header.packet_type} "
                        f"status={header.status} "
                        f"frame={header.frame_id} "
                        f"total={header.total_length} "
                        f"chunk={header.chunk_index}/{header.chunk_count} "
                        f"payload={header.payload_length}"
                    )

                if header.packet_type == PACKET_ERROR:
                    status_name = STATUS_NAMES.get(
                        header.status,
                        f"UNKNOWN_{header.status}",
                    )

                    phase_name = SPI_PHASE_NAMES.get(
                        header.chunk_index,
                        f"UNKNOWN_PHASE_{header.chunk_index}",
                    )

                    print(
                        f"ESP32 ERROR frame={header.frame_id}: "
                        f"{status_name}, "
                        f"phase={header.chunk_index} ({phase_name})"
                    )

                    if payload:
                        print(f"SPI raw header: {payload.hex(' ')}")
                        print(f"SPI decoded: {decode_spi_header(payload)}")

                    assemblies.pop(header.frame_id, None)
                    continue

                if header.packet_type == PACKET_PERFORMANCE:
                    if len(payload) != 20:
                        print(
                            f"Invalid performance payload: "
                            f"{len(payload)} bytes"
                        )
                        continue

                    capture_us = struct.unpack(">I", payload[0:4])[0]
                    spi_us = struct.unpack(">I", payload[4:8])[0]
                    usb_us = struct.unpack(">I", payload[8:12])[0]
                    total_us = struct.unpack(">I", payload[12:16])[0]
                    jpeg_size = struct.unpack(">I", payload[16:20])[0]

                    fps = (
                        1_000_000.0 / total_us
                        if total_us > 0
                        else 0.0
                    )

                    print(
                        f"PERF frame={header.frame_id} "
                        f"JPEG={jpeg_size} "
                        f"capture={capture_us / 1000.0:.2f}ms "
                        f"SPI={spi_us / 1000.0:.2f}ms "
                        f"USB={usb_us / 1000.0:.2f}ms "
                        f"total={total_us / 1000.0:.2f}ms "
                        f"FPS={fps:.2f}"
                    )
                    continue

                if header.packet_type == PACKET_DATA:
                    if header.chunk_count == 0:
                        print(f"Frame {header.frame_id}: chunk_count is zero")
                        continue

                    if header.chunk_index >= header.chunk_count:
                        print(
                            f"Frame {header.frame_id}: invalid chunk index "
                            f"{header.chunk_index}/{header.chunk_count}"
                        )
                        continue

                    assembly = assemblies.get(header.frame_id)

                    if assembly is None:
                        assembly = FrameAssembly(
                            total_length=header.total_length,
                            chunk_count=header.chunk_count,
                            chunks={},
                        )
                        assemblies[header.frame_id] = assembly

                    if (
                        assembly.total_length != header.total_length
                        or assembly.chunk_count != header.chunk_count
                    ):
                        print(f"Frame {header.frame_id}: inconsistent metadata")
                        assemblies.pop(header.frame_id, None)
                        continue

                    assembly.chunks[header.chunk_index] = payload
                    continue

                if header.packet_type == PACKET_FRAME_END:
                    assembly = assemblies.pop(header.frame_id, None)

                    if assembly is None:
                        print(
                            f"FRAME_END without DATA for frame={header.frame_id}"
                        )
                        continue

                    if len(assembly.chunks) != assembly.chunk_count:
                        print(
                            f"Frame {header.frame_id}: incomplete chunks "
                            f"{len(assembly.chunks)}/{assembly.chunk_count}"
                        )
                        invalid_frames += 1
                        continue

                    encrypted_data = b"".join(
                        assembly.chunks[index]
                        for index in range(assembly.chunk_count)
                    )

                    if len(encrypted_data) != assembly.total_length:
                        print(
                            f"Frame {header.frame_id}: length mismatch "
                            f"expected={assembly.total_length} "
                            f"actual={len(encrypted_data)}"
                        )
                        invalid_frames += 1
                        continue

                    if display_or_save_frame(
                        header.frame_id,
                        encrypted_data,
                        args.save_dir,
                        args.no_display,
                    ):
                        completed_frames += 1

                        elapsed = max(
                            time.monotonic() - start_time,
                            0.001,
                        )
                        average_fps = completed_frames / elapsed

                        print(
                            f"FRAME OK id={header.frame_id} "
                            f"bytes={len(encrypted_data)} "
                            f"completed={completed_frames} "
                            f"invalid={invalid_frames} "
                            f"avgFPS={average_fps:.2f}"
                        )
                    else:
                        invalid_frames += 1

                    continue

                print(f"Unknown packet type: {header.packet_type}")

    except KeyboardInterrupt:
        print("\nStopped")
    finally:
        if cv2 is not None:
            cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
