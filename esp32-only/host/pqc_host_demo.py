from __future__ import annotations

import argparse
import hashlib
import os
import time
from dataclasses import dataclass

import serial
from pqcrypto.kem import ml_kem_768

from host.crypto_ops import aes_decrypt, aes_encrypt, verify_dsa
from host.serial_protocol import ProtocolError, SerialProtocol


KEM_PUBLIC_KEY_SIZE = 1184
KEM_CIPHERTEXT_SIZE = 1088
SHARED_SECRET_SIZE = 32
DSA_PUBLIC_KEY_SIZE = 1312
DSA_SIGNATURE_SIZE = 2420
AAD = b"esp32-only/interop/v1"


@dataclass(frozen=True)
class AesStatus:
    epoch: int
    count: int
    rekey: bool


@dataclass(frozen=True)
class MemoryStatus:
    internal_free: int
    internal_min: int
    internal_largest: int
    psram_free: int
    psram_min: int
    psram_largest: int
    pqc_stack_min: int


def parse_key_values(line: str, expected_prefix: str) -> dict[str, str]:
    if not line.startswith(expected_prefix):
        raise ProtocolError(f"Expected prefix {expected_prefix!r}, received {line!r}")
    values: dict[str, str] = {}
    for item in line[len(expected_prefix) :].split():
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        values[key] = value
    return values


def parse_aes_status(line: str) -> AesStatus:
    values = parse_key_values(line, "OK ")
    try:
        epoch = int(values["epoch"])
        count = int(values["count"])
        rekey_value = int(values["rekey"])
    except (KeyError, ValueError) as error:
        raise ProtocolError(f"Malformed AES status: {line!r}") from error
    if epoch < 1 or count < 1 or rekey_value not in (0, 1):
        raise ProtocolError(f"Invalid AES status: {line!r}")
    return AesStatus(epoch, count, bool(rekey_value))


def parse_memory_status(line: str) -> MemoryStatus:
    values = parse_key_values(line, "MEM ")
    field_names = (
        "internal_free",
        "internal_min",
        "internal_largest",
        "psram_free",
        "psram_min",
        "psram_largest",
        "pqc_stack_min",
    )
    try:
        parsed = {name: int(values[name]) for name in field_names}
    except (KeyError, ValueError) as error:
        raise ProtocolError(f"Malformed memory status: {line!r}") from error
    if any(value < 0 for value in parsed.values()):
        raise ProtocolError(f"Memory status contains a negative value: {line!r}")
    return MemoryStatus(**parsed)


def request_memory_status(protocol: SerialProtocol) -> MemoryStatus:
    protocol.send_line("MEMORY_INFO")
    return parse_memory_status(protocol.expect_prefix("MEM "))


def format_memory_status(status: MemoryStatus) -> str:
    def kib(value: int) -> str:
        return f"{value / 1024:.1f} KiB"

    return (
        "[MEM] Internal "
        f"free={kib(status.internal_free)}, "
        f"minimum={kib(status.internal_min)}, "
        f"largest={kib(status.internal_largest)} | "
        "PSRAM "
        f"free={kib(status.psram_free)}, "
        f"minimum={kib(status.psram_min)}, "
        f"largest={kib(status.psram_largest)} | "
        f"PQC stack minimum={kib(status.pqc_stack_min)}"
    )


def add_transcript_field(transcript: "hashlib._Hash", label: bytes, data: bytes) -> None:
    transcript.update(len(label).to_bytes(2, "big"))
    transcript.update(label)
    transcript.update(len(data).to_bytes(4, "big"))
    transcript.update(data)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="ESP32-S3 ML-KEM/AES-GCM/ML-DSA interoperability test"
    )
    parser.add_argument("--port", default="COM5", help="TTL serial port")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument(
        "--message",
        default="Hello from the PC to ESP32-S3-CAM",
        help="UTF-8 plaintext used by the AES tests",
    )
    parser.add_argument(
        "--skip-device-selftest",
        action="store_true",
        help="skip the ESP32-only crypto self-test",
    )
    parser.add_argument(
        "--rekey-every",
        type=int,
        default=10,
        help="rotate the ML-KEM/AES session after this many successful messages",
    )
    parser.add_argument(
        "--exchange-count",
        type=int,
        default=12,
        help="total successful AES messages to exchange (alternating directions)",
    )
    return parser.parse_args()


def request_info(protocol: SerialProtocol) -> str:
    protocol.send_line("INFO")
    return protocol.read_until_prefix("INFO ", timeout=10.0)


def get_frame(protocol: SerialProtocol, command: str, expected_size: int) -> bytes:
    protocol.send_line(command)
    protocol.expect("OK")
    value = protocol.receive_frame(expected_size)
    if len(value) != expected_size:
        raise ProtocolError(
            f"{command} returned {len(value)} bytes; expected {expected_size}"
        )
    return value


def establish_session(protocol: SerialProtocol) -> tuple[bytes, bytes, bytes, int]:
    kem_public_key = get_frame(
        protocol, "GET_KEM_PUBLIC_KEY", KEM_PUBLIC_KEY_SIZE
    )

    started = time.perf_counter()
    kem_ciphertext, shared_secret = ml_kem_768.encrypt(kem_public_key)
    host_elapsed = (time.perf_counter() - started) * 1000

    if len(kem_ciphertext) != KEM_CIPHERTEXT_SIZE:
        raise ProtocolError(f"Unexpected KEM ciphertext size: {len(kem_ciphertext)}")
    if len(shared_secret) != SHARED_SECRET_SIZE:
        raise ProtocolError(f"Unexpected shared secret size: {len(shared_secret)}")

    protocol.send_line("KEM_DECAPSULATE")
    protocol.expect("READY")
    protocol.send_frame(kem_ciphertext)
    device_result = protocol.expect_prefix("KEM_OK ")
    values = parse_key_values(device_result, "KEM_OK ")
    try:
        epoch = int(values["epoch"])
    except (KeyError, ValueError) as error:
        raise ProtocolError(f"Malformed KEM response: {device_result!r}") from error

    print(f"[PASS] ML-KEM-768 host encapsulation: {host_elapsed:.3f} ms")
    print(f"[PASS] ML-KEM-768 ESP32 decapsulation: {device_result}")
    return kem_public_key, kem_ciphertext, shared_secret, epoch


def test_host_to_device_aes(
    protocol: SerialProtocol, shared_secret: bytes, plaintext: bytes
) -> tuple[bytes, bytes, bytes, AesStatus]:
    nonce = os.urandom(12)
    ciphertext, tag = aes_encrypt(shared_secret, nonce, plaintext, AAD)

    protocol.send_line("AES_DECRYPT")
    protocol.expect("READY")
    for frame in (nonce, AAD, ciphertext, tag):
        protocol.send_frame(frame)
    status = parse_aes_status(protocol.expect_prefix("OK "))
    recovered = protocol.receive_frame(len(plaintext))

    if recovered != plaintext:
        raise ProtocolError("ESP32 AES plaintext does not match host plaintext")
    print(
        f"[PASS] message {status.count}: PC encrypt -> ESP32 decrypt "
        f"(epoch {status.epoch}, rekey={int(status.rekey)})"
    )
    return nonce, ciphertext, tag, status


def test_modified_tag_rejected(
    protocol: SerialProtocol, shared_secret: bytes, plaintext: bytes
) -> None:
    nonce = os.urandom(12)
    ciphertext, tag = aes_encrypt(shared_secret, nonce, plaintext, AAD)
    bad_tag = bytearray(tag)
    bad_tag[0] ^= 0x01
    protocol.send_line("AES_DECRYPT")
    protocol.expect("READY")
    for frame in (nonce, AAD, ciphertext, bytes(bad_tag)):
        protocol.send_frame(frame)
    protocol.expect("ERR AES_AUTH_FAILED")
    print("[PASS] Modified AES-GCM tag rejected and not counted")


def test_device_to_host_aes(
    protocol: SerialProtocol, shared_secret: bytes, plaintext: bytes
) -> tuple[bytes, bytes, bytes, AesStatus]:
    protocol.send_line("AES_ENCRYPT")
    protocol.expect("READY")
    protocol.send_frame(plaintext)
    protocol.send_frame(AAD)
    status = parse_aes_status(protocol.expect_prefix("OK "))

    nonce = protocol.receive_frame(12)
    ciphertext = protocol.receive_frame(len(plaintext))
    tag = protocol.receive_frame(16)
    recovered = aes_decrypt(shared_secret, nonce, ciphertext, tag, AAD)

    if recovered != plaintext:
        raise ProtocolError("Host AES plaintext does not match ESP32 plaintext")
    print(
        f"[PASS] message {status.count}: ESP32 encrypt -> PC decrypt "
        f"(epoch {status.epoch}, rekey={int(status.rekey)})"
    )
    return nonce, ciphertext, tag, status


def test_device_signature(protocol: SerialProtocol, digest: bytes) -> None:
    public_key = get_frame(protocol, "GET_DSA_PUBLIC_KEY", DSA_PUBLIC_KEY_SIZE)

    protocol.send_line("DSA_SIGN")
    protocol.expect("READY")
    protocol.send_frame(digest)
    result = protocol.expect_prefix("OK ")
    signature = protocol.receive_frame(DSA_SIGNATURE_SIZE)
    if len(signature) != DSA_SIGNATURE_SIZE:
        raise ProtocolError(f"Unexpected ML-DSA signature size: {len(signature)}")

    if not verify_dsa(public_key, digest, signature):
        raise ProtocolError("PC rejected the ESP32 ML-DSA-44 signature")
    print(f"[PASS] ML-DSA-44 ESP32 sign -> PC verify: {result}")

    modified = bytearray(digest)
    modified[0] ^= 0x01
    if verify_dsa(public_key, bytes(modified), signature):
        raise ProtocolError("Modified ML-DSA message was incorrectly accepted")
    print("[PASS] ML-DSA-44 modified message rejected by PC")


def main() -> int:
    args = parse_arguments()
    plaintext = args.message.encode("utf-8")
    if len(plaintext) > 4096:
        raise ValueError("message must be at most 4096 UTF-8 bytes")
    if not 1 <= args.rekey_every <= 100000:
        raise ValueError("rekey-every must be between 1 and 100000")
    if args.exchange_count < 1:
        raise ValueError("exchange-count must be at least 1")

    print(f"Opening {args.port} at {args.baud} baud...")
    with serial.Serial(
        args.port,
        args.baud,
        timeout=2.0,
        write_timeout=10.0,
    ) as serial_port:
        serial_port.dtr = False
        serial_port.rts = False
        time.sleep(2.0)
        serial_port.reset_input_buffer()
        serial_port.reset_output_buffer()

        protocol = SerialProtocol(serial_port)
        info = request_info(protocol)
        print(info)

        if not args.skip_device_selftest:
            protocol.send_line("SELFTEST")
            selftest = protocol.read_until_prefix("SELFTEST ", timeout=30.0)
            if "FAIL" in selftest:
                raise ProtocolError(selftest)
            print(f"[PASS] Device local test: {selftest}")

        protocol.send_line(f"SET_REKEY_INTERVAL {args.rekey_every}")
        setting = protocol.expect_prefix("OK rekey_every=")
        print(f"[PASS] Rekey policy configured: {setting}")

        transcript = hashlib.sha256()
        transcript.update(b"esp32-only/transcript/v2")
        kem_public_key, kem_ciphertext, shared_secret, epoch = establish_session(protocol)
        add_transcript_field(transcript, b"kem-public-key", kem_public_key)
        add_transcript_field(transcript, b"kem-ciphertext", kem_ciphertext)
        add_transcript_field(transcript, b"epoch", epoch.to_bytes(4, "big"))
        previous_public_key = kem_public_key
        previous_shared_secret = shared_secret

        test_modified_tag_rejected(protocol, shared_secret, plaintext)

        for message_index in range(1, args.exchange_count + 1):
            numbered_plaintext = plaintext + f" #{message_index}".encode("ascii")
            if len(numbered_plaintext) > 4096:
                raise ValueError("numbered message must be at most 4096 bytes")

            if message_index % 2:
                nonce, ciphertext, tag, status = test_host_to_device_aes(
                    protocol, shared_secret, numbered_plaintext
                )
                direction = b"pc-to-esp32"
            else:
                nonce, ciphertext, tag, status = test_device_to_host_aes(
                    protocol, shared_secret, numbered_plaintext
                )
                direction = b"esp32-to-pc"

            if status.epoch != epoch:
                raise ProtocolError(
                    f"AES response epoch {status.epoch} does not match session {epoch}"
                )
            add_transcript_field(transcript, b"direction", direction)
            add_transcript_field(
                transcript, b"message-index", message_index.to_bytes(4, "big")
            )
            add_transcript_field(transcript, b"nonce", nonce)
            add_transcript_field(transcript, b"ciphertext", ciphertext)
            add_transcript_field(transcript, b"tag", tag)

            if status.rekey and message_index < args.exchange_count:
                kem_public_key, kem_ciphertext, shared_secret, epoch = establish_session(
                    protocol
                )
                if kem_public_key == previous_public_key:
                    raise ProtocolError("ESP32 did not rotate its ML-KEM public key")
                if shared_secret == previous_shared_secret:
                    raise ProtocolError("Rekey produced the same shared secret")
                print(f"[PASS] Automatic rekey completed; session epoch is now {epoch}")
                add_transcript_field(transcript, b"kem-public-key", kem_public_key)
                add_transcript_field(transcript, b"kem-ciphertext", kem_ciphertext)
                add_transcript_field(transcript, b"epoch", epoch.to_bytes(4, "big"))
                previous_public_key = kem_public_key
                previous_shared_secret = shared_secret

        test_device_signature(protocol, transcript.digest())

        protocol.send_line("RESET_SESSION")
        protocol.expect("OK")

    print("\nAll ESP32 <-> PC interoperability tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
