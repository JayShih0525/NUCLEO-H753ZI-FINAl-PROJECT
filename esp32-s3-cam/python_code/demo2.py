#!/usr/bin/env python3
"""
computer_client.py

Talks to the ESP32 over the CAM1 packet protocol:
  - sends CONTROL_REQUEST packets to relay KEM handshake
    messages to the STM32 (via ESP32 passthrough)
  - receives DATA / FRAME_END / ERROR / PERFORMANCE packets
    for the (currently fake-)encrypted camera stream

Usage:
    python computer_client.py --port /dev/cu.usbmodem1101 --baud 115200

NOTE: the actual ML-KEM encapsulate + AES-GCM decrypt calls
below are marked TODO, mirroring the STM32-side crypto_app.c
placeholders. Until both sides are wired to a real crypto
library, this script will complete the handshake and receive
frames, but the "AES key" is meaningless and frames are only
stripped of a 28-byte zero suffix, not actually decrypted.
"""

import argparse
import struct
import sys
import time

import serial
import numpy as np
import cv2

# =====================================================
# CAM1 protocol constants (must match protocol.h / spi_link.h)
# =====================================================

MAGIC = b"CAM1"
HEADER_SIZE = 24
VERSION = 1

PACKET_DATA = 0x01
PACKET_FRAME_END = 0x02
PACKET_ERROR = 0x03
PACKET_PERFORMANCE = 0x04
PACKET_CONTROL_REQUEST = 0x05
PACKET_CONTROL_RESPONSE = 0x06

STATUS_OK = 0x00
STATUS_CAMERA_FAILED = 0x01
STATUS_FRAME_INVALID = 0x02
STATUS_SPI_FAILED = 0x03
STATUS_LENGTH_ERROR = 0x04
STATUS_OUTPUT_FAILED = 0x05

SPI_CMD_PING = 0x01
SPI_CMD_PROCESS = 0x02
SPI_CMD_KEM_GET_PUBKEY = 0x03
SPI_CMD_KEM_DECAPSULATE = 0x04

AES_GCM_NONCE_BYTES = 12
AES_GCM_TAG_BYTES = 16

SPI_ERROR_PHASE_NAMES = {
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


# =====================================================
# Packet framing
# =====================================================

def build_packet(packet_type, status, id_field, total_len,
                  field16_1, field16_2, payload=b""):
    header = struct.pack(
        ">4sBBBBIIHHI",
        MAGIC, VERSION, packet_type, status, 0,
        id_field, total_len, field16_1, field16_2, len(payload),
    )
    assert len(header) == HEADER_SIZE
    return header + payload


def read_exact(ser, length, timeout_s=5.0):
    deadline = time.time() + timeout_s
    data = bytearray()
    while len(data) < length:
        chunk = ser.read(length - len(data))
        if chunk:
            data += chunk
        elif time.time() > deadline:
            return None
    return bytes(data)


def resync_to_magic(ser, timeout_s=5.0):
    """Slides byte-by-byte until the last 4 bytes read equal
    MAGIC, so a single lost/extra byte anywhere in the stream
    can't permanently wedge the reader. Returns True once
    re-aligned, False on timeout."""
    window = bytearray()
    deadline = time.time() + timeout_s

    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue

        window += b
        if len(window) > len(MAGIC):
            del window[0]

        if bytes(window) == MAGIC:
            return True

    return False


def read_packet(ser, timeout_s=5.0):
    """Reads one full CAM1 packet (header + payload).
    Returns a dict, or None on timeout/framing error. On a
    bad magic, resyncs to the next MAGIC occurrence instead
    of just dropping 24 bytes and hoping - a single stray or
    missing byte anywhere in the stream would otherwise wedge
    every future read permanently."""
    magic = read_exact(ser, len(MAGIC), timeout_s)
    if magic is None:
        return None

    if magic != MAGIC:
        print(f"WARN: bad magic {magic!r}, resyncing", file=sys.stderr)
        if not resync_to_magic(ser, timeout_s):
            return None
        magic = MAGIC

    rest = read_exact(ser, HEADER_SIZE - len(MAGIC), timeout_s)
    if rest is None:
        return None

    header = magic + rest

    version, packet_type, status, _reserved, id_field, \
        total_len, field16_1, field16_2, payload_len = struct.unpack(
            ">BBBBIIHHI", header[len(MAGIC):])

    payload = b""
    if payload_len > 0:
        payload = read_exact(ser, payload_len, timeout_s)
        if payload is None:
            return None

    return {
        "type": packet_type,
        "status": status,
        "id": id_field,
        "total_len": total_len,
        "field16_1": field16_1,
        "field16_2": field16_2,
        "payload": payload,
    }


# =====================================================
# Control channel (KEM handshake relay)
# =====================================================

def send_control_request(ser, request_id, spi_command, payload=b""):
    packet = build_packet(
        PACKET_CONTROL_REQUEST, spi_command, request_id,
        0, 0, 0, payload)
    ser.write(packet)


def wait_control_response(ser, request_id, timeout_s=5.0):
    """Reads packets until the matching CONTROL_RESPONSE shows
    up. Any DATA/FRAME_END/etc. packets seen in the meantime
    are dropped - during handshake the camera loop shouldn't
    be pushing frames yet, but this keeps us robust either way."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        pkt = read_packet(ser, timeout_s=max(0.1, deadline - time.time()))
        if pkt is None:
            continue
        if pkt["type"] == PACKET_CONTROL_RESPONSE and pkt["id"] == request_id:
            return pkt
    return None


def kem_get_public_key(ser, request_id):
    send_control_request(ser, request_id, SPI_CMD_KEM_GET_PUBKEY)
    response = wait_control_response(ser, request_id)

    if response is None:
        raise RuntimeError("KEM_GET_PUBKEY: no response (timeout)")
    if response["status"] != STATUS_OK:
        raise RuntimeError(
            f"KEM_GET_PUBKEY failed, status=0x{response['status']:02X}")

    return response["payload"]


def kem_decapsulate(ser, request_id, ciphertext):
    send_control_request(ser, request_id, SPI_CMD_KEM_DECAPSULATE, ciphertext)
    response = wait_control_response(ser, request_id)

    if response is None:
        raise RuntimeError("KEM_DECAPSULATE: no response (timeout)")
    if response["status"] != STATUS_OK:
        raise RuntimeError(
            f"KEM_DECAPSULATE failed, status=0x{response['status']:02X}")

    return True


def ml_kem_encapsulate(public_key):
    """
    TODO: replace with a real ML-KEM encapsulate call, e.g.
        from your_kem_library import encapsulate
        ciphertext, shared_secret = encapsulate(public_key)
        return ciphertext, shared_secret

    Placeholder below produces deterministic garbage so the
    handshake flow can be exercised end-to-end before a real
    KEM library is wired in on both sides.
    """
    print("WARNING: ml_kem_encapsulate() is a placeholder - "
          "the AES key derived from this handshake is NOT secure.",
          file=sys.stderr)

    fake_ciphertext = b"\x00" * 1088   # TODO: real KEM_CIPHERTEXTBYTES
    fake_shared_secret = b"\x00" * 32  # TODO: real shared secret
    return fake_ciphertext, fake_shared_secret


def do_handshake(ser):
    """Runs the KEM handshake and returns the AES key to use
    for decrypting frames. See TODOs above and in
    stm32_src/crypto_app.c - both sides are placeholders
    until a real ML-KEM library is wired in."""
    print("Requesting STM32 KEM public key...")
    public_key = kem_get_public_key(ser, request_id=1)
    print(f"  got public key, {len(public_key)} bytes")

    ciphertext, shared_secret = ml_kem_encapsulate(public_key)

    print("Sending KEM ciphertext for decapsulation...")
    kem_decapsulate(ser, request_id=2, ciphertext=ciphertext)
    print("  STM32 confirmed decapsulation OK")

    return shared_secret


# =====================================================
# AES-GCM decrypt (placeholder, see crypto_app.c on STM32)
# =====================================================

def aes_gcm_decrypt(ciphertext, nonce, tag, key):
    """
    TODO: replace with a real AES-GCM decrypt, e.g. using the
    `cryptography` package:
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        return AESGCM(key).decrypt(nonce, ciphertext + tag, None)

    Right now the STM32 side doesn't really encrypt (see
    crypto_app.c), so this just returns the ciphertext as-is.
    """
    return ciphertext


# =====================================================
# Frame receiver
# =====================================================

def receive_loop(ser, aes_key):
    frames = {}  # frameId -> {chunks: {index: bytes}, total, count}

    print("waiting for next CAM1 packet...")

    while True:
        pkt = read_packet(ser, timeout_s=10.0)
        if pkt is None:
            print("waiting for next CAM1 packet...")
            continue

        if pkt["type"] == PACKET_DATA:
            frame_id = pkt["id"]
            chunk_index = pkt["field16_1"]
            chunk_count = pkt["field16_2"]

            entry = frames.setdefault(
                frame_id,
                {"chunks": {}, "total": pkt["total_len"], "count": chunk_count})

            entry["chunks"][chunk_index] = pkt["payload"]

            print(f"packet frame={frame_id} chunk={chunk_index + 1}/{chunk_count} "
                  f"payload={len(pkt['payload'])}")

        elif pkt["type"] == PACKET_FRAME_END:
            frame_id = pkt["id"]
            entry = frames.pop(frame_id, None)

            if entry is None:
                continue

            chunk_count = entry["count"]
            ordered = [entry["chunks"].get(i) for i in range(chunk_count)]

            if any(c is None for c in ordered):
                print(f"frame={frame_id}: missing chunk(s), dropping", file=sys.stderr)
                continue

            raw = b"".join(ordered)

            # Last chunk carries [ciphertext_tail][nonce(12)][tag(16)]
            # appended by CryptoApp_EncryptChunk on the STM32 side.
            if len(raw) < (AES_GCM_NONCE_BYTES + AES_GCM_TAG_BYTES):
                print(f"frame={frame_id}: too short for nonce+tag, dropping",
                      file=sys.stderr)
                continue

            ciphertext = raw[:-(AES_GCM_NONCE_BYTES + AES_GCM_TAG_BYTES)]
            nonce = raw[-(AES_GCM_NONCE_BYTES + AES_GCM_TAG_BYTES):-AES_GCM_TAG_BYTES]
            tag = raw[-AES_GCM_TAG_BYTES:]

            jpeg_bytes = aes_gcm_decrypt(ciphertext, nonce, tag, aes_key)

            # out_name = f"frame_{frame_id:06d}.jpg"
            # with open(out_name, "wb") as f:
            #     f.write(jpeg_bytes)

            # print(f"frame={frame_id}: saved {out_name} ({len(jpeg_bytes)} bytes)")

            img_array = np.frombuffer(jpeg_bytes, dtype=np.uint8)
            img = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

            if img is None:
                print(f"frame={frame_id}: not valid JPEG data yet "
                      f"(still fake-encrypted / no real decrypt wired in?)",
                      file=sys.stderr)
            else:
                cv2.imshow("ESP32 Camera", img)

            # Must be called regularly for the window to
            # actually repaint/respond, even if img was None.
            key = cv2.waitKey(1) & 0xFF
            if key in (ord('q'), 27):  # 'q' or ESC
                print("quit requested from image window")
                return

        elif pkt["type"] == PACKET_ERROR:
            phase_name = SPI_ERROR_PHASE_NAMES.get(pkt["field16_1"], "?")
            print(f"ESP32 ERROR frame={pkt['id']}: "
                  f"status=0x{pkt['status']:02X}, phase={pkt['field16_1']} "
                  f"({phase_name})")

            if pkt["payload"]:
                hex_bytes = " ".join(f"{b:02x}" for b in pkt["payload"])
                print(f"  raw debug payload: {hex_bytes}")

        elif pkt["type"] == PACKET_PERFORMANCE:
            if len(pkt["payload"]) >= 20:
                capture_us, spi_us, usb_us, total_us, jpeg_size = struct.unpack(
                    ">IIIII", pkt["payload"][:20])
                print(f"frame={pkt['id']} perf: capture={capture_us}us "
                      f"spi={spi_us}us usb={usb_us}us total={total_us}us "
                      f"size={jpeg_size}")

        else:
            print(f"WARN: unexpected packet type 0x{pkt['type']:02X}",
                  file=sys.stderr)


# =====================================================
# Entry point
# =====================================================

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--skip-handshake", action="store_true",
                         help="skip the KEM handshake and just receive frames "
                              "(useful while crypto_app.c is still placeholder code)")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    print(f"Opened {args.port} at {args.baud} baud")

    aes_key = None
    if not args.skip_handshake:
        aes_key = do_handshake(ser)

    try:
        receive_loop(ser, aes_key)
    finally:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()