from __future__ import annotations

import argparse
import hashlib
import struct
import time
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np
import serial
from cryptography.exceptions import InvalidTag

from host.crypto_ops import aes_decrypt
from host.pqc_host_demo import (
    AesStatus,
    add_transcript_field,
    establish_session,
    format_memory_status,
    parse_aes_status,
    request_info,
    request_memory_status,
    test_device_signature,
)
from host.serial_protocol import ProtocolError, SerialProtocol


CAMERA_METADATA_SIZE = 20
CAMERA_MAGIC = b"CAM2"
MAX_JPEG_SIZE = 1024 * 1024


@dataclass(frozen=True)
class CameraMetadata:
    epoch: int
    frame_id: int
    width: int
    height: int
    jpeg_length: int


@dataclass(frozen=True)
class CameraResult:
    metadata: CameraMetadata
    metadata_bytes: bytes
    nonce: bytes
    ciphertext: bytes
    tag: bytes
    jpeg: bytes
    status: AesStatus


def parse_camera_metadata(data: bytes) -> CameraMetadata:
    if len(data) != CAMERA_METADATA_SIZE:
        raise ProtocolError(f"Camera metadata has {len(data)} bytes, expected 20")
    magic, epoch, frame_id, width, height, jpeg_length = struct.unpack(
        ">4sIIHHI", data
    )
    if magic != CAMERA_MAGIC:
        raise ProtocolError(f"Unexpected camera metadata magic: {magic!r}")
    if epoch < 1 or width < 1 or height < 1:
        raise ProtocolError("Camera metadata contains invalid dimensions or epoch")
    if not 4 <= jpeg_length <= MAX_JPEG_SIZE:
        raise ProtocolError(f"Invalid encrypted JPEG length: {jpeg_length}")
    return CameraMetadata(epoch, frame_id, width, height, jpeg_length)


def request_encrypted_frame(
    protocol: SerialProtocol, shared_secret: bytes, expected_epoch: int
) -> CameraResult:
    protocol.send_line("CAMERA_CAPTURE_ENCRYPTED")
    status_line = protocol.expect_prefix("OK ")
    status = parse_aes_status(status_line)
    metadata_bytes = protocol.receive_frame(CAMERA_METADATA_SIZE)
    nonce = protocol.receive_frame(12)
    ciphertext = protocol.receive_frame(MAX_JPEG_SIZE)
    tag = protocol.receive_frame(16)
    metadata = parse_camera_metadata(metadata_bytes)

    if status.epoch != expected_epoch or metadata.epoch != expected_epoch:
        raise ProtocolError(
            f"Camera frame epoch mismatch: session={expected_epoch}, "
            f"status={status.epoch}, metadata={metadata.epoch}"
        )
    if len(nonce) != 12 or len(tag) != 16:
        raise ProtocolError("Camera AES nonce or tag has the wrong size")
    if len(ciphertext) != metadata.jpeg_length:
        raise ProtocolError(
            f"Ciphertext length {len(ciphertext)} != JPEG length {metadata.jpeg_length}"
        )

    jpeg = aes_decrypt(
        shared_secret, nonce, ciphertext, tag, metadata_bytes
    )
    if not (jpeg.startswith(b"\xff\xd8") and jpeg.endswith(b"\xff\xd9")):
        raise ProtocolError("Decrypted camera data is not a complete JPEG")

    return CameraResult(
        metadata, metadata_bytes, nonce, ciphertext, tag, jpeg, status
    )


def decode_jpeg(result: CameraResult) -> np.ndarray:
    image = cv2.imdecode(
        np.frombuffer(result.jpeg, dtype=np.uint8), cv2.IMREAD_COLOR
    )
    if image is None:
        raise ProtocolError(f"OpenCV could not decode frame {result.metadata.frame_id}")
    height, width = image.shape[:2]
    if (width, height) != (result.metadata.width, result.metadata.height):
        raise ProtocolError(
            f"Decoded size {width}x{height} != signed metadata "
            f"{result.metadata.width}x{result.metadata.height}"
        )
    return image


def add_session_to_transcript(
    transcript: "hashlib._Hash",
    public_key: bytes,
    ciphertext: bytes,
    epoch: int,
) -> None:
    add_transcript_field(transcript, b"kem-public-key", public_key)
    add_transcript_field(transcript, b"kem-ciphertext", ciphertext)
    add_transcript_field(transcript, b"epoch", epoch.to_bytes(4, "big"))


def add_camera_to_transcript(
    transcript: "hashlib._Hash", result: CameraResult
) -> None:
    add_transcript_field(transcript, b"camera-metadata", result.metadata_bytes)
    add_transcript_field(transcript, b"camera-nonce", result.nonce)
    add_transcript_field(transcript, b"camera-ciphertext", result.ciphertext)
    add_transcript_field(transcript, b"camera-tag", result.tag)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Encrypted ESP32-S3-CAM photo and recording demo"
    )
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--mode", choices=("photo", "record"), default="photo")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--output-fps", type=float, default=12.0)
    parser.add_argument("--rekey-every", type=int, default=10)
    parser.add_argument(
        "--memory-every",
        type=int,
        default=10,
        help="query ESP32 memory every N frames; use 0 to disable",
    )
    parser.add_argument("--display", action="store_true")
    parser.add_argument("--skip-device-selftest", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    if not 1 <= args.rekey_every <= 100000:
        raise ValueError("rekey-every must be between 1 and 100000")
    if args.seconds <= 0:
        raise ValueError("seconds must be greater than zero")
    if args.memory_every < 0:
        raise ValueError("memory-every must be zero or greater")

    output = args.output
    if output is None:
        output = Path(
            "encrypted_photo.jpg" if args.mode == "photo"
            else "encrypted_camera_recording.avi"
        )

    print(f"Opening {args.port} at {args.baud} baud...")
    writer: cv2.VideoWriter | None = None
    frames_received = 0
    last_memory_frame = -1
    started: float | None = None

    with serial.Serial(
        args.port,
        args.baud,
        timeout=5.0,
        write_timeout=10.0,
    ) as serial_port:
        serial_port.dtr = False
        serial_port.rts = False
        time.sleep(3.0)
        serial_port.reset_input_buffer()
        serial_port.reset_output_buffer()
        protocol = SerialProtocol(serial_port)

        print(request_info(protocol))
        protocol.send_line("CAMERA_INFO")
        print(protocol.expect_prefix("CAMERA "))

        if not args.skip_device_selftest:
            protocol.send_line("SELFTEST")
            selftest = protocol.read_until_prefix("SELFTEST ", timeout=30.0)
            if "FAIL" in selftest:
                raise ProtocolError(selftest)
            print(f"[PASS] Device crypto self-test: {selftest}")

        if args.memory_every:
            print(format_memory_status(request_memory_status(protocol)))
            last_memory_frame = 0

        protocol.send_line(f"SET_REKEY_INTERVAL {args.rekey_every}")
        print(f"[PASS] {protocol.expect_prefix('OK rekey_every=')}")
        protocol.send_line(
            "CAMERA_MODE PHOTO" if args.mode == "photo" else "CAMERA_MODE STREAM"
        )
        print(f"[PASS] {protocol.expect_prefix('OK camera_mode=')}")

        transcript = hashlib.sha256()
        transcript.update(b"esp32-only/camera-transcript/v1")
        public_key, kem_ciphertext, shared_secret, epoch = establish_session(protocol)
        add_session_to_transcript(
            transcript, public_key, kem_ciphertext, epoch
        )
        previous_public_key = public_key
        previous_shared_secret = shared_secret
        started = time.monotonic()

        while args.mode == "photo" or time.monotonic() - started < args.seconds:
            result = request_encrypted_frame(protocol, shared_secret, epoch)
            image = decode_jpeg(result)
            add_camera_to_transcript(transcript, result)
            frames_received += 1

            if frames_received == 1:
                bad_tag = bytearray(result.tag)
                bad_tag[0] ^= 1
                try:
                    aes_decrypt(
                        shared_secret,
                        result.nonce,
                        result.ciphertext,
                        bytes(bad_tag),
                        result.metadata_bytes,
                    )
                except InvalidTag:
                    print("[PASS] Modified camera AES-GCM tag rejected on PC")
                else:
                    raise ProtocolError("Modified camera tag was accepted")

            elapsed = time.monotonic() - started
            print(
                f"[PASS] frame={result.metadata.frame_id} "
                f"{result.metadata.width}x{result.metadata.height} "
                f"JPEG={len(result.jpeg)} epoch={result.status.epoch} "
                f"count={result.status.count} rekey={int(result.status.rekey)} "
                f"average={frames_received / elapsed:.2f} FPS"
            )

            if (
                args.memory_every
                and frames_received % args.memory_every == 0
            ):
                print(format_memory_status(request_memory_status(protocol)))
                last_memory_frame = frames_received

            if args.mode == "photo":
                output.write_bytes(result.jpeg)
                if args.display:
                    cv2.imshow("Encrypted ESP32-S3-CAM photo", image)
                    cv2.waitKey(0)
                break

            if writer is None:
                height, width = image.shape[:2]
                writer = cv2.VideoWriter(
                    str(output),
                    cv2.VideoWriter_fourcc(*"MJPG"),
                    args.output_fps,
                    (width, height),
                )
                if not writer.isOpened():
                    raise RuntimeError("Could not open encrypted camera AVI writer")
            writer.write(image)
            if args.display:
                cv2.imshow("Encrypted ESP32-S3-CAM stream", image)
                if cv2.waitKey(1) & 0xFF in (ord("q"), 27):
                    break

            if result.status.rekey:
                public_key, kem_ciphertext, shared_secret, epoch = establish_session(
                    protocol
                )
                if public_key == previous_public_key:
                    raise ProtocolError("ESP32 did not rotate its camera ML-KEM key")
                if shared_secret == previous_shared_secret:
                    raise ProtocolError("Camera rekey produced the same secret")
                add_session_to_transcript(
                    transcript, public_key, kem_ciphertext, epoch
                )
                previous_public_key = public_key
                previous_shared_secret = shared_secret
                print(f"[PASS] Camera session automatically rekeyed to epoch {epoch}")

        if args.memory_every and last_memory_frame != frames_received:
            print(format_memory_status(request_memory_status(protocol)))

        test_device_signature(protocol, transcript.digest())
        protocol.send_line("RESET_SESSION")
        protocol.expect("OK")

    if writer is not None:
        writer.release()
    cv2.destroyAllWindows()

    if started is None or frames_received == 0:
        raise RuntimeError("No encrypted camera frames were received")
    elapsed = time.monotonic() - started
    print(
        f"\nSaved {frames_received} decrypted frame(s), "
        f"average {frames_received / elapsed:.2f} FPS -> {output.resolve()}"
    )
    print("Full ML-KEM + AES-GCM + rekey + ML-DSA camera flow passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
