"""
UDP receiver for the ESP32-S3 -> STM32H753 (XOR) -> UDP camera pipeline.

Protocol (must match esp32_s3_camera_h753_udp_stable.ino exactly):

    UdpFrameHeader (16 bytes, NETWORK / big-endian byte order -- the ESP32
    side builds it with htonl()/htons(), which is different from the
    little-endian header used on the SPI link):

        uint32_t magic;        // 0x43414D31 ("CAM1")
        uint32_t frameId;
        uint16_t packetId;
        uint16_t packetCount;
        uint16_t payloadSize;
        uint16_t flags;        // bit0 = first packet, bit1 = last packet

    Each UDP datagram is: header + up to 1400 bytes of XOR-"encrypted"
    JPEG payload. XOR key is 0xA5 (must match XOR_KEY in the STM32
    firmware). Decrypting is therefore just re-XORing with the same key.

Usage:
    pip install opencv-python numpy
    python3 udp_camera_receiver.py [--port 5005] [--timeout-ms 400]
"""

import argparse
import socket
import struct
import time
from collections import defaultdict

import cv2
import numpy as np

UDP_MAGIC = 0x43414D31
HEADER_FMT = ">IIHHHH"  # magic, frameId, packetId, packetCount, payloadSize, flags
HEADER_SIZE = struct.calcsize(HEADER_FMT)
assert HEADER_SIZE == 16

XOR_KEY = 0xA5

FLAG_FIRST_PACKET = 0x0001
FLAG_LAST_PACKET = 0x0002


class FrameAssembler:
    """Buffers incoming packets per frameId and reassembles complete frames."""

    def __init__(self, frame_timeout_s: float):
        self.frame_timeout_s = frame_timeout_s
        self.pending = {}  # frameId -> {"packets": {id: bytes}, "count": int, "t0": float}

    def add_packet(self, frame_id, packet_id, packet_count, payload):
        now = time.monotonic()
        entry = self.pending.get(frame_id)
        if entry is None:
            entry = {"packets": {}, "count": packet_count, "t0": now}
            self.pending[frame_id] = entry
        entry["packets"][packet_id] = payload

        completed = None
        if len(entry["packets"]) == entry["count"]:
            completed = self._assemble(entry)
            del self.pending[frame_id]

        self._drop_stale(now, keep=frame_id)
        return completed

    def _assemble(self, entry):
        try:
            ordered = [entry["packets"][i] for i in range(entry["count"])]
        except KeyError:
            return None  # shouldn't happen given the length check above
        return b"".join(ordered)

    def _drop_stale(self, now, keep):
        stale_ids = [
            fid for fid, e in self.pending.items()
            if fid != keep and (now - e["t0"]) > self.frame_timeout_s
        ]
        for fid in stale_ids:
            del self.pending[fid]


def xor_decrypt(data: bytes, key: int) -> bytes:
    return bytes(b ^ key for b in data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=5005,
                         help="UDP port to listen on (must match RECEIVER_PORT on the ESP32)")
    parser.add_argument("--timeout-ms", type=int, default=400,
                         help="Drop an incomplete frame's buffered packets after this long")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(1.0)

    print(f"Listening for camera frames on UDP port {args.port} ...")
    print("Press 'q' in the video window (or Ctrl+C here) to quit.")

    assembler = FrameAssembler(frame_timeout_s=args.timeout_ms / 1000.0)

    frames_shown = 0
    frames_dropped_decode = 0
    last_frame_id_shown = -1
    last_stats_t = time.monotonic()
    bytes_in_window = 0

    window_name = "ESP32-S3 Camera Feed"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)

    try:
        while True:
            try:
                data, _addr = sock.recvfrom(2048)
            except socket.timeout:
                continue

            if len(data) < HEADER_SIZE:
                continue

            magic, frame_id, packet_id, packet_count, payload_size, _flags = \
                struct.unpack(HEADER_FMT, data[:HEADER_SIZE])

            if magic != UDP_MAGIC:
                continue  # not our protocol / garbage packet

            payload = data[HEADER_SIZE:HEADER_SIZE + payload_size]
            if len(payload) != payload_size or packet_count == 0:
                continue

            bytes_in_window += len(data)

            # Ignore stragglers from frames older than the one already shown;
            # always keep the freshest frame on screen rather than stalling
            # on a frame whose packets never fully arrived.
            if frame_id < last_frame_id_shown:
                continue

            jpeg_encrypted = assembler.add_packet(
                frame_id, packet_id, packet_count, payload)

            if jpeg_encrypted is None:
                continue

            jpeg_bytes = xor_decrypt(jpeg_encrypted, XOR_KEY)
            img = cv2.imdecode(
                np.frombuffer(jpeg_bytes, dtype=np.uint8), cv2.IMREAD_COLOR)

            if img is None:
                frames_dropped_decode += 1
                continue

            last_frame_id_shown = frame_id
            frames_shown += 1

            now = time.monotonic()
            if now - last_stats_t >= 1.0:
                fps = frames_shown / (now - last_stats_t) if frames_shown else 0.0
                kbps = (bytes_in_window * 8 / 1000.0) / (now - last_stats_t)
                overlay = f"frame={frame_id} shown={frames_shown} decodeFail={frames_dropped_decode} ~{fps:.1f}fps ~{kbps:.0f}kbps"
                print(overlay)
                frames_shown = 0
                bytes_in_window = 0
                last_stats_t = now
            else:
                overlay = f"frame={frame_id}"

            cv2.putText(img, overlay, (8, 20), cv2.FONT_HERSHEY_SIMPLEX,
                        0.5, (0, 255, 0), 1, cv2.LINE_AA)
            cv2.imshow(window_name, img)

            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()