#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
import sys
import time
from typing import Optional, Tuple

import cv2
import numpy as np
import serial
from serial.tools import list_ports


MAGIC = b"CAM1"
ERROR_MAGIC = b"ERR1"

HEADER_REMAINDER_SIZE = 8
MAX_JPEG_SIZE = 1024 * 1024

DEFAULT_BAUD = 460800
DEFAULT_TIMEOUT = 2.0


def list_available_ports() -> list[str]:
    """列出目前可用的 serial ports。"""

    ports = list(list_ports.comports())

    print("Available serial ports:")

    if not ports:
        print("  No serial ports found.")
        return []

    result: list[str] = []

    for port in ports:
        description = port.description or "n/a"
        hardware_id = port.hwid or "n/a"

        print(
            f"  {port.device} | "
            f"{description} | "
            f"{hardware_id}"
        )

        result.append(port.device)

    return result


def auto_select_port(ports: list[str]) -> Optional[str]:
    """
    優先選擇常見 USB UART port。
    macOS 通常是 /dev/cu.usbserial-* 或 /dev/cu.SLAB_USBtoUART。
    """

    preferred_patterns = (
        "usbserial",
        "usbmodem",
        "SLAB_USBtoUART",
        "wchusbserial",
    )

    for pattern in preferred_patterns:
        for port in ports:
            if pattern.lower() in port.lower():
                return port

    return None


def read_exact(
    ser: serial.Serial,
    size: int,
) -> Optional[bytes]:
    """
    精確讀取指定長度。
    timeout 前未收齊則回傳 None。
    """

    if size <= 0:
        return b""

    data = bytearray()
    deadline = time.monotonic() + ser.timeout

    while len(data) < size:
        remaining = size - len(data)
        chunk = ser.read(remaining)

        if chunk:
            data.extend(chunk)
            continue

        if time.monotonic() >= deadline:
            return None

    return bytes(data)


def wait_for_magic(
    ser: serial.Serial,
) -> Optional[bytes]:
    """
    從任意位置搜尋 CAM1。
    即使開啟 serial 時剛好落在一張影像中間，
    也能重新同步到下一個 frame。
    """

    window = bytearray()

    deadline = time.monotonic() + ser.timeout

    while time.monotonic() < deadline:
        byte = ser.read(1)

        if not byte:
            continue

        window.extend(byte)

        if len(window) > 4:
            del window[0]

        if bytes(window) == MAGIC:
            return MAGIC

        if bytes(window) == ERROR_MAGIC:
            return ERROR_MAGIC

    return None


def read_frame(
    ser: serial.Serial,
) -> Optional[Tuple[int, bytes]]:
    """
    讀取一個完整封包：

    CAM1
    frameId      uint32, big endian
    jpegLength   uint32, big endian
    jpegData     N bytes
    """

    magic = wait_for_magic(ser)

    if magic is None:
        return None

    if magic == ERROR_MAGIC:
        raise RuntimeError(
            "ESP32 reports camera initialization failure."
        )

    header = read_exact(ser, HEADER_REMAINDER_SIZE)

    if header is None:
        print("Timeout while reading frame header.")
        return None

    frame_id, jpeg_length = struct.unpack(">II", header)

    if jpeg_length <= 0 or jpeg_length > MAX_JPEG_SIZE:
        print(
            f"Invalid JPEG length: {jpeg_length}. "
            "Resynchronizing..."
        )
        return None

    jpeg_data = read_exact(ser, jpeg_length)

    if jpeg_data is None:
        print(
            f"Timeout while reading JPEG data: "
            f"frame={frame_id}, expected={jpeg_length}"
        )
        return None

    return frame_id, jpeg_data


def validate_jpeg(data: bytes) -> bool:
    """檢查 JPEG SOI 和 EOI markers。"""

    return (
        len(data) >= 4
        and data[0:2] == b"\xFF\xD8"
        and data[-2:] == b"\xFF\xD9"
    )


def decode_jpeg(data: bytes) -> Optional[np.ndarray]:
    """將 JPEG bytes 解碼成 OpenCV image。"""

    encoded = np.frombuffer(data, dtype=np.uint8)

    image = cv2.imdecode(
        encoded,
        cv2.IMREAD_COLOR,
    )

    return image


def clean_serial_buffers(ser: serial.Serial) -> None:
    """
    開始接收前清空電腦端 serial buffers。

    注意：
    reset_input_buffer() 只會清除 Python / OS 已收到但尚未讀取的資料。
    ESP32 之後仍會繼續送新 frame。
    """

    print("Cleaning serial buffers...")

    # 暫停一下，讓 port 開啟時可能觸發的 reset / boot data 進入 buffer
    time.sleep(1.0)

    ser.reset_input_buffer()
    ser.reset_output_buffer()

    # 再稍微等待，然後清一次，
    # 避免 ESP32 開機訊息或舊 frame 殘留
    time.sleep(0.2)

    ser.reset_input_buffer()

    print("Serial buffers cleaned.")


def run(
    port: str,
    baud: int,
    timeout: float,
) -> None:
    print(f"Opening {port} at {baud} baud...")

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout,
            write_timeout=timeout,
            rtscts=False,
            dsrdtr=False,
            xonxoff=False,
        )

    except serial.SerialException as exc:
        print(f"Cannot open serial port: {exc}")
        print("Close Arduino Serial Monitor and other serial programs.")
        sys.exit(1)

    try:
        clean_serial_buffers(ser)

        print("Waiting for camera frames...")
        print("Press Q or ESC to stop.")

        received_frames = 0
        invalid_frames = 0
        decode_failures = 0

        fps_start_time = time.monotonic()
        fps_frame_count = 0
        current_fps = 0.0

        while True:
            try:
                packet = read_frame(ser)

            except RuntimeError as exc:
                print(f"ESP32 error: {exc}")
                break

            if packet is None:
                print("UART timeout while waiting for CAM1")
                continue

            frame_id, jpeg_data = packet

            if not validate_jpeg(jpeg_data):
                invalid_frames += 1

                print(
                    f"Frame {frame_id}: invalid JPEG markers, "
                    f"size={len(jpeg_data)}"
                )

                # 清除目前已累積但尚未解析的資料，
                # 下一輪重新尋找 CAM1
                ser.reset_input_buffer()
                continue

            image = decode_jpeg(jpeg_data)

            if image is None:
                decode_failures += 1

                print(
                    f"Frame {frame_id}: OpenCV decode failed, "
                    f"size={len(jpeg_data)}"
                )
                continue

            received_frames += 1
            fps_frame_count += 1

            now = time.monotonic()
            elapsed = now - fps_start_time

            if elapsed >= 1.0:
                current_fps = fps_frame_count / elapsed
                fps_frame_count = 0
                fps_start_time = now

            height, width = image.shape[:2]

            text = (
                f"Frame {frame_id} | "
                f"{width}x{height} | "
                f"{len(jpeg_data)} bytes | "
                f"{current_fps:.2f} FPS"
            )

            cv2.putText(
                image,
                text,
                (10, 25),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (255, 255, 255),
                1,
                cv2.LINE_AA,
            )

            cv2.imshow("ESP32 UART Camera", image)

            print(
                f"Frame={frame_id} "
                f"JPEG={len(jpeg_data)} bytes "
                f"resolution={width}x{height} "
                f"FPS={current_fps:.2f}"
            )

            key = cv2.waitKey(1) & 0xFF

            if key in (ord("q"), ord("Q"), 27):
                break

        print()
        print("Statistics:")
        print(f"  Received frames : {received_frames}")
        print(f"  Invalid frames  : {invalid_frames}")
        print(f"  Decode failures : {decode_failures}")

    except KeyboardInterrupt:
        print("\nStopped by user.")

    finally:
        cv2.destroyAllWindows()

        if ser.is_open:
            ser.close()

        print("Serial port closed.")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive JPEG camera frames from ESP32 over UART."
    )

    parser.add_argument(
        "--port",
        type=str,
        default=None,
        help="Serial port, for example /dev/cu.usbserial-110",
    )

    parser.add_argument(
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help=f"UART baud rate, default: {DEFAULT_BAUD}",
    )

    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help=f"UART timeout in seconds, default: {DEFAULT_TIMEOUT}",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_arguments()

    available_ports = list_available_ports()

    selected_port = args.port

    if selected_port is None:
        selected_port = auto_select_port(available_ports)

        if selected_port is None:
            print()
            print("Could not automatically select a USB serial port.")
            print("Use:")
            print(
                "  python camera_uart.py "
                "--port /dev/cu.usbserial-110"
            )
            sys.exit(1)

        print()
        print(f"Automatically selected port: {selected_port}")

    run(
        port=selected_port,
        baud=args.baud,
        timeout=args.timeout,
    )


if __name__ == "__main__":
    main()