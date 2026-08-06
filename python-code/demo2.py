#!/usr/bin/env python3
"""
demo2.py

Talks to the ESP32 over the CAM1 packet protocol:
  - sends CONTROL_REQUEST packets to relay KEM handshake
    messages to the STM32 (via ESP32 passthrough)
  - receives DATA / FRAME_END / ERROR / PERFORMANCE packets
    for the AES-GCM-encrypted camera stream, decrypts and
    displays them

Usage:
    python demo2.py --port /dev/cu.usbmodem1101 --baud 115200

Requires: pyserial, opencv-python, numpy, cryptography, pqcrypto
    pip install pyserial opencv-python numpy cryptography pqcrypto
"""

import argparse
import struct
import sys
import time

import serial
import numpy as np
import cv2

from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.exceptions import InvalidTag
from pqcrypto.kem.ml_kem_768 import encrypt as ml_kem_encrypt

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
    are dropped."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        pkt = read_packet(ser, timeout_s=max(0.1, deadline - time.time()))
        if pkt is None:
            continue
        if pkt["type"] == PACKET_CONTROL_RESPONSE and pkt["id"] == request_id:
            return pkt
    return None


def _print_control_failure(label, response):
    """status != OK -> the ESP32 puts its raw SPI debug header
    (g_spiDebugHeader) in the payload - decode it so we can see
    exactly which SPI phase failed."""
    payload = response["payload"]
    print(f"{label} failed: status=0x{response['status']:02X}", file=sys.stderr)

    if len(payload) >= 16:
        magic, sequence, payload_len, command, spi_status, flags, reserved = \
            struct.unpack(">IIIBBBB", payload[:16])
        hex_bytes = " ".join(f"{b:02x}" for b in payload[:16])
        print(f"  raw SPI debug header: {hex_bytes}", file=sys.stderr)
        print(f"  decoded: magic=0x{magic:08X} sequence={sequence} "
              f"payloadLength={payload_len} command=0x{command:02X} "
              f"status=0x{spi_status:02X} flags=0x{flags:02X} "
              f"reserved=0x{reserved:02X}", file=sys.stderr)
    elif payload:
        hex_bytes = " ".join(f"{b:02x}" for b in payload)
        print(f"  raw payload: {hex_bytes}", file=sys.stderr)


def kem_get_public_key(ser, request_id):
    send_control_request(ser, request_id, SPI_CMD_KEM_GET_PUBKEY)
    response = wait_control_response(ser, request_id)

    if response is None:
        raise RuntimeError("KEM_GET_PUBKEY: no response (timeout)")
    if response["status"] != STATUS_OK:
        _print_control_failure("KEM_GET_PUBKEY", response)
        raise RuntimeError(
            f"KEM_GET_PUBKEY failed, status=0x{response['status']:02X}")

    return response["payload"]


def kem_decapsulate(ser, request_id, ciphertext):
    send_control_request(ser, request_id, SPI_CMD_KEM_DECAPSULATE, ciphertext)
    response = wait_control_response(ser, request_id)

    if response is None:
        raise RuntimeError("KEM_DECAPSULATE: no response (timeout)")
    if response["status"] != STATUS_OK:
        _print_control_failure("KEM_DECAPSULATE", response)
        raise RuntimeError(
            f"KEM_DECAPSULATE failed, status=0x{response['status']:02X}")

    return True


def ml_kem_encapsulate(public_key):
    """Real ML-KEM-768 encapsulate (pqcrypto), matching what
    your own ml_kem_app.py does: ciphertext, shared_secret =
    encrypt(public_key)."""
    ciphertext, shared_secret = ml_kem_encrypt(public_key)
    return ciphertext, shared_secret


def do_handshake(ser):
    """Runs the KEM handshake and returns (aes_key, kem_public_key)."""
    print("Requesting STM32 KEM public key...")
    public_key = kem_get_public_key(ser, request_id=1)
    print(f"  got public key, {len(public_key)} bytes")

    ciphertext, shared_secret = ml_kem_encapsulate(public_key)
    print(f"  encapsulated, ciphertext={len(ciphertext)} bytes, "
          f"shared_secret={len(shared_secret)} bytes")

    print("Sending KEM ciphertext for decapsulation...")
    kem_decapsulate(ser, request_id=2, ciphertext=ciphertext)
    print("  STM32 confirmed decapsulation OK")

    return shared_secret, public_key


# =====================================================
# AES-GCM decrypt - real, matches STM32's aes_gcm_lib.c
# (no AAD, ciphertext+tag concatenated for `cryptography`)
# =====================================================

def aes_gcm_decrypt(ciphertext, nonce, tag, key):
    aesgcm = AESGCM(key)
    return aesgcm.decrypt(nonce, ciphertext + tag, None)


# =====================================================
# Debug info window (speed / nonce / tag / keys)
# =====================================================

def _hex_preview(data, max_bytes=32):
    """Full hex for short data, truncated preview for long
    data (e.g. a 1184-byte KEM public key) - showing the
    whole thing wouldn't fit on screen anyway."""
    if data is None:
        return "-"

    hex_str = data[:max_bytes].hex()

    if len(data) > max_bytes:
        return f"{hex_str}... ({len(data)} bytes total)"

    return f"{hex_str} ({len(data)} bytes)"


def draw_info_window(state):
    canvas = np.zeros((420, 620, 3), dtype=np.uint8)
    lines = ["-- performance (last frame) --"]

    perf = state.get("perf")
    if perf:
        lines.append(f"frame:   {perf['frame_id']}")
        lines.append(f"capture: {perf['capture_us']} us")
        lines.append(f"spi:     {perf['spi_us']} us")
        lines.append(f"usb:     {perf['usb_us']} us")
        lines.append(f"total:   {perf['total_us']} us")
        lines.append(f"size:    {perf['jpeg_size']} bytes")
    else:
        lines.append("(none yet)")

    lines.append("")
    lines.append("-- last frame AES-GCM nonce/tag --")
    lines.append(f"nonce: {_hex_preview(state.get('nonce'))}")
    lines.append(f"tag:   {_hex_preview(state.get('tag'))}")

    lines.append("")
    lines.append("-- session AES key --")
    lines.append(_hex_preview(state.get("aes_key")))

    lines.append("")
    lines.append("-- STM32 ML-KEM public key --")
    lines.append(_hex_preview(state.get("kem_pub"), max_bytes=24))

    y = 24
    for line in lines:
        cv2.putText(canvas, line, (10, y), cv2.FONT_HERSHEY_SIMPLEX,
                    0.5, (0, 255, 0), 1, cv2.LINE_AA)
        y += 22

    cv2.imshow("Debug Info", canvas)


# =====================================================
# Frame receiver
# =====================================================

def receive_loop(ser, aes_key, kem_pub):
    frames = {}  # frameId -> {chunks: {index: bytes}, total, count}
    state = {"perf": None, "nonce": None, "tag": None,
             "aes_key": aes_key, "kem_pub": kem_pub}

    print("waiting for next CAM1 packet...")
    draw_info_window(state)

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

            state["nonce"] = nonce
            state["tag"] = tag

            try:
                jpeg_bytes = aes_gcm_decrypt(ciphertext, nonce, tag, aes_key)
            except InvalidTag:
                print(f"frame={frame_id}: AES-GCM auth failed (bad tag) - "
                      f"dropping frame, likely a corrupted transfer",
                      file=sys.stderr)
                draw_info_window(state)
                cv2.waitKey(1)
                continue

            img_array = np.frombuffer(jpeg_bytes, dtype=np.uint8)
            img = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

            if img is None:
                print(f"frame={frame_id}: decrypted but not valid JPEG data",
                      file=sys.stderr)
            else:
                cv2.imshow("ESP32 Camera", img)

            draw_info_window(state)

            # Must be called regularly for the windows to
            # actually repaint/respond.
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
            # Speed info - kept exactly as before, now also
            # mirrored into the debug info window.
            if len(pkt["payload"]) >= 20:
                capture_us, spi_us, usb_us, total_us, jpeg_size = struct.unpack(
                    ">IIIII", pkt["payload"][:20])
                print(f"frame={pkt['id']} perf: capture={capture_us}us "
                      f"spi={spi_us}us usb={usb_us}us total={total_us}us "
                      f"size={jpeg_size}")

                state["perf"] = {
                    "frame_id": pkt["id"],
                    "capture_us": capture_us,
                    "spi_us": spi_us,
                    "usb_us": usb_us,
                    "total_us": total_us,
                    "jpeg_size": jpeg_size,
                }
                draw_info_window(state)
                cv2.waitKey(1)

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
                              "(will fail to decrypt - only useful for wire-"
                              "level debugging)")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    print(f"Opened {args.port} at {args.baud} baud")

    aes_key = None
    kem_pub = None
    if not args.skip_handshake:
        aes_key, kem_pub = do_handshake(ser)

    try:
        receive_loop(ser, aes_key, kem_pub)
    finally:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()