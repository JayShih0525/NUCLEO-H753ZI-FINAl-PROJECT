"""
WSRP v2 TCP Server
==================
Reliable high-throughput receiver for:
    STM32H753ZI -> UART5 -> Ai-WB2 transparent TCP -> this Python server

Wire frame (big-endian):
    MAGIC(2) VERSION(1) TYPE(1) OBJECT_ID(4) SEQ(4) PAYLOAD_LEN(2)
    HEADER_CRC16(2) PAYLOAD(N) FRAME_CRC32(4)

Protocol:
    HELLO / HELLO_ACK
    START / START_ACK
    DATA... / cumulative ACK or NACK per window
    END / END_ACK

Run:
    python3 server.py --host 0.0.0.0 --port 5555 --save-dir ./received
"""
from __future__ import annotations

import argparse
import logging
import socket
import struct
import threading
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

# ============================================================================
# Protocol constants: must match stm32/wsrp_protocol.h
# ============================================================================
MAGIC = b"\xAB\xCD"
VERSION = 0x02
PREFIX_SIZE = 14
HEADER_SIZE = 16
TRAILER_SIZE = 4
PAYLOAD_MAX = 4096
CONTROL_MAX = 64
WINDOW_DEFAULT = 4
WINDOW_MAX = 16

TYPE_HELLO = 0x01
TYPE_HELLO_ACK = 0x02
TYPE_START = 0x10
TYPE_START_ACK = 0x11
TYPE_DATA = 0x12
TYPE_ACK = 0x13
TYPE_NACK = 0x14
TYPE_END = 0x15
TYPE_END_ACK = 0x16
TYPE_PING = 0x20
TYPE_PONG = 0x21
TYPE_ABORT = 0xFF

STATUS_OK = 0
STATUS_PROTOCOL_ERROR = 1
STATUS_BAD_SEQUENCE = 2
STATUS_LENGTH_ERROR = 3
STATUS_OBJECT_CRC_ERROR = 4
STATUS_BUSY = 5

TYPE_NAMES = {
    TYPE_HELLO: "HELLO", TYPE_HELLO_ACK: "HELLO_ACK", TYPE_START: "START",
    TYPE_START_ACK: "START_ACK", TYPE_DATA: "DATA", TYPE_ACK: "ACK",
    TYPE_NACK: "NACK", TYPE_END: "END", TYPE_END_ACK: "END_ACK",
    TYPE_PING: "PING", TYPE_PONG: "PONG", TYPE_ABORT: "ABORT",
}

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("WSRP-Server")

# ============================================================================
# CRC and wire codec
# ============================================================================
def crc16_xmodem(data: bytes) -> int:
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def crc32(data: bytes, value: int = 0) -> int:
    return zlib.crc32(data, value) & 0xFFFFFFFF


@dataclass(frozen=True)
class Frame:
    frame_type: int
    object_id: int
    seq: int
    payload: bytes


def build_frame(frame_type: int, object_id: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > PAYLOAD_MAX:
        raise ValueError(f"payload too large: {len(payload)}")
    prefix = struct.pack(">2sBBIIH", MAGIC, VERSION, frame_type, object_id, seq, len(payload))
    header_crc = struct.pack(">H", crc16_xmodem(prefix))
    # This matches STM32: frame CRC covers prefix + payload, not header_crc bytes.
    frame_crc = struct.pack(">I", crc32(prefix + payload))
    return prefix + header_crc + payload + frame_crc


class FrameParser:
    """TCP stream parser that keeps incomplete bytes across recv() and timeouts."""
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.bad_headers = 0
        self.bad_frames = 0

    def feed(self, data: bytes) -> None:
        self.buffer.extend(data)

    def pop_frame(self) -> Optional[Frame]:
        while True:
            if len(self.buffer) < 2:
                return None

            pos = self.buffer.find(MAGIC)
            if pos < 0:
                self.buffer[:] = self.buffer[-1:] if self.buffer[-1:] == MAGIC[:1] else b""
                return None
            if pos:
                log.warning("Discarding %d bytes before frame MAGIC", pos)
                del self.buffer[:pos]

            if len(self.buffer) < HEADER_SIZE:
                return None

            prefix = bytes(self.buffer[:PREFIX_SIZE])
            magic, version, frame_type, object_id, seq, payload_len = struct.unpack(">2sBBIIH", prefix)
            got_header_crc = struct.unpack(">H", bytes(self.buffer[PREFIX_SIZE:HEADER_SIZE]))[0]

            if magic != MAGIC or version != VERSION or payload_len > PAYLOAD_MAX:
                log.warning("Invalid frame header; resynchronizing")
                del self.buffer[0]
                self.bad_headers += 1
                continue
            if crc16_xmodem(prefix) != got_header_crc:
                log.warning("Header CRC mismatch; resynchronizing")
                del self.buffer[0]
                self.bad_headers += 1
                continue

            frame_size = HEADER_SIZE + payload_len + TRAILER_SIZE
            if len(self.buffer) < frame_size:
                return None

            payload = bytes(self.buffer[HEADER_SIZE:HEADER_SIZE + payload_len])
            got_crc = struct.unpack(">I", bytes(self.buffer[HEADER_SIZE + payload_len:frame_size]))[0]
            exp_crc = crc32(prefix + payload)

            if got_crc != exp_crc:
                self.bad_frames += 1
                log.error(
                    "Frame CRC mismatch type=%s object=%d seq=%d expected=%08X got=%08X",
                    TYPE_NAMES.get(frame_type, hex(frame_type)), object_id, seq, exp_crc, got_crc,
                )
                # A lost UART byte changes the apparent end of this frame.
                # Removing frame_size bytes here may also remove the beginning of
                # the next valid frame. Discard only the current MAGIC start byte
                # and scan again for a header whose CRC16 is valid.
                del self.buffer[0]
                continue

            del self.buffer[:frame_size]
            return Frame(frame_type, object_id, seq, payload)

# ============================================================================
# Object receiver state
# ============================================================================
@dataclass
class ReceiveObject:
    object_id: int
    total_len: int
    total_frames: int
    expected_crc32: int
    data_type: int
    mode: int
    window: int
    temp_path: Path
    final_path: Path
    file_handle: object
    next_seq: int = 0
    received_bytes: int = 0
    running_crc32: int = 0

    # Boundary of the most recent cumulative ACK or NACK.
    # After recovery begins at seq=5 with a window of 4, the next ACK
    # must be sent at seq=9, not only at fixed multiples of 4.
    last_ack_seq: int = 0

    def append_data(self, payload: bytes) -> None:
        self.file_handle.write(payload)
        self.running_crc32 = crc32(payload, self.running_crc32)
        self.received_bytes += len(payload)
        self.next_seq += 1

    def close(self) -> None:
        if not self.file_handle.closed:
            self.file_handle.flush()
            self.file_handle.close()

    def discard(self) -> None:
        self.close()
        self.temp_path.unlink(missing_ok=True)

# ============================================================================
# One TCP connection / protocol session
# ============================================================================
class WSRPSession:
    def __init__(self, conn: socket.socket, addr: tuple, save_dir: Path) -> None:
        self.conn = conn
        self.addr = addr
        self.save_dir = save_dir
        self.parser = FrameParser()
        self.active: Optional[ReceiveObject] = None
        self.completed: dict[int, Path] = {}
        self.window = WINDOW_DEFAULT
        self.connected_at = time.perf_counter()
        self.total_payload_bytes = 0

    def send(self, frame_type: int, object_id: int = 0, seq: int = 0, payload: bytes = b"") -> None:
        raw = build_frame(frame_type, object_id, seq, payload)
        self.conn.sendall(raw)
        log.info(
            "TX %s object=%d seq=%d bytes=%d head=%s",
            TYPE_NAMES.get(frame_type, hex(frame_type)),
            object_id,
            seq,
            len(raw),
            raw[:8].hex(" "),
        )

    def send_ack(self, obj: ReceiveObject, nack: bool = False, status: int = STATUS_OK) -> None:
        # next_seq(4), received_bytes(4), window(2), status(1), reserved(1)
        payload = struct.pack(">IIHBB", obj.next_seq, obj.received_bytes, obj.window, status, 0)
        self.send(TYPE_NACK if nack else TYPE_ACK, obj.object_id, obj.next_seq, payload)

    def handle_hello(self, frame: Frame) -> None:
        if len(frame.payload) < 8:
            self.send(TYPE_ABORT, payload=bytes([STATUS_PROTOCOL_ERROR]))
            return
        requested_payload, requested_window, client_max_window, _features = struct.unpack(">HHHH", frame.payload[:8])
        accepted_payload = min(requested_payload, PAYLOAD_MAX)
        requested_window = requested_window or WINDOW_DEFAULT
        self.window = max(1, min(requested_window, client_max_window or WINDOW_MAX, WINDOW_MAX))
        self.send(TYPE_HELLO_ACK, payload=struct.pack(">HH", accepted_payload, self.window))
        log.info("HELLO %s: payload=%d, window=%d", self.addr, accepted_payload, self.window)

    def handle_start(self, frame: Frame) -> None:
        if len(frame.payload) != 16:
            self.send(TYPE_START_ACK, frame.object_id, payload=bytes([STATUS_PROTOCOL_ERROR, 0, 0]))
            return
        total_len, total_frames, expected_crc, data_type, mode, requested_window = struct.unpack(">IIIBBH", frame.payload)
        expected_frames = (total_len + PAYLOAD_MAX - 1) // PAYLOAD_MAX if total_len else 0
        if total_len == 0 or total_frames != expected_frames:
            self.send(TYPE_START_ACK, frame.object_id, payload=bytes([STATUS_LENGTH_ERROR, 0, 0]))
            return

        # A resent START commonly means START_ACK was lost; do not erase already received data.
        if self.active and self.active.object_id == frame.object_id:
            same = (self.active.total_len == total_len and self.active.expected_crc32 == expected_crc)
            status = STATUS_OK if same else STATUS_BUSY
            self.send(TYPE_START_ACK, frame.object_id, payload=bytes([status]) + struct.pack(">H", self.active.window))
            return
        if self.active is not None:
            log.warning("Aborting incomplete object %d due to new START", self.active.object_id)
            self.active.discard()

        window = max(1, min(requested_window or self.window, WINDOW_MAX))
        stamp = time.strftime("%Y%m%d_%H%M%S")
        base = f"object_{frame.object_id:08d}_{stamp}"
        temp_path = self.save_dir / f"{base}.part"
        final_path = self.save_dir / f"{base}.bin"
        fh = temp_path.open("wb")
        self.active = ReceiveObject(
            frame.object_id, total_len, total_frames, expected_crc,
            data_type, mode, window, temp_path, final_path, fh,
        )
        self.send(TYPE_START_ACK, frame.object_id, payload=bytes([STATUS_OK]) + struct.pack(">H", window))
        log.info("START object=%d size=%d frames=%d window=%d", frame.object_id, total_len, total_frames, window)

    def handle_data(self, frame: Frame) -> None:
        obj = self.active

        if obj is None or frame.object_id != obj.object_id:
            self.send(
                TYPE_ABORT,
                frame.object_id,
                frame.seq,
                bytes([STATUS_PROTOCOL_ERROR]),
            )
            return

        # Normal in-order DATA frame.
        if frame.seq == obj.next_seq:
            remaining = obj.total_len - obj.received_bytes
            expected_len = min(PAYLOAD_MAX, remaining)

            if len(frame.payload) != expected_len:
                self.send_ack(
                    obj,
                    nack=True,
                    status=STATUS_LENGTH_ERROR,
                )
                obj.last_ack_seq = obj.next_seq
                log.warning(
                    "NACK object=%d bad length seq=%d expected_len=%d got_len=%d",
                    obj.object_id,
                    frame.seq,
                    expected_len,
                    len(frame.payload),
                )
                return

            obj.append_data(frame.payload)
            self.total_payload_bytes += len(frame.payload)

            # ACK relative to the current recovery/window base.
            # This works both for normal 0->4->8 windows and for a
            # recovered stream such as 5->9->13 after a NACK at seq=5.
            frames_since_ack = obj.next_seq - obj.last_ack_seq

            if (
                frames_since_ack >= obj.window
                or obj.next_seq == obj.total_frames
            ):
                self.send_ack(obj)
                obj.last_ack_seq = obj.next_seq

                log.info(
                    "ACK object=%d next_seq=%d/%d received=%d/%d",
                    obj.object_id,
                    obj.next_seq,
                    obj.total_frames,
                    obj.received_bytes,
                    obj.total_len,
                )

            return

        # Duplicate already accepted DATA: the previous ACK was probably lost
        # or delayed. Re-send cumulative ACK after the last accepted duplicate.
        if frame.seq < obj.next_seq:
            if frame.seq == obj.next_seq - 1:
                self.send_ack(obj)
                log.info(
                    "RE-ACK object=%d next_seq=%d/%d after duplicate data",
                    obj.object_id,
                    obj.next_seq,
                    obj.total_frames,
                )
            return

        # Future sequence received: an earlier frame is missing or corrupted.
        # NACK creates a new window base at obj.next_seq.
        self.send_ack(
            obj,
            nack=True,
            status=STATUS_BAD_SEQUENCE,
        )
        obj.last_ack_seq = obj.next_seq

        log.warning(
            "NACK object=%d expected_seq=%d got_seq=%d new_ack_base=%d",
            obj.object_id,
            obj.next_seq,
            frame.seq,
            obj.last_ack_seq,
        )

    def handle_end(self, frame: Frame) -> None:
        # END_ACK can be lost, so answer a repeated END for a completed object again.
        if frame.object_id in self.completed:
            self.send(TYPE_END_ACK, frame.object_id, frame.seq, bytes([STATUS_OK]))
            return
        obj = self.active
        if obj is None or obj.object_id != frame.object_id or len(frame.payload) != 8:
            self.send(TYPE_END_ACK, frame.object_id, frame.seq, bytes([STATUS_PROTOCOL_ERROR]))
            return
        end_len, end_crc = struct.unpack(">II", frame.payload)
        valid = (
            end_len == obj.total_len
            and end_crc == obj.expected_crc32
            and obj.received_bytes == obj.total_len
            and obj.next_seq == obj.total_frames
            and obj.running_crc32 == obj.expected_crc32
        )
        if not valid:
            log.error(
                "END verify failed object=%d bytes=%d/%d frames=%d/%d crc=%08X expected=%08X",
                obj.object_id, obj.received_bytes, obj.total_len, obj.next_seq,
                obj.total_frames, obj.running_crc32, obj.expected_crc32,
            )
            self.send_ack(obj, nack=True, status=STATUS_OBJECT_CRC_ERROR)
            return

        obj.close()
        obj.temp_path.replace(obj.final_path)
        self.completed[obj.object_id] = obj.final_path
        self.active = None
        self.send(TYPE_END_ACK, frame.object_id, frame.seq, bytes([STATUS_OK]))
        log.info("COMPLETE object=%d saved=%s bytes=%d", frame.object_id, obj.final_path.name, obj.total_len)

    def handle_frame(self, frame: Frame) -> None:
        if frame.frame_type == TYPE_HELLO:
            self.handle_hello(frame)
        elif frame.frame_type == TYPE_START:
            self.handle_start(frame)
        elif frame.frame_type == TYPE_DATA:
            self.handle_data(frame)
        elif frame.frame_type == TYPE_END:
            self.handle_end(frame)
        elif frame.frame_type == TYPE_PING:
            self.send(TYPE_PONG, frame.object_id, frame.seq, frame.payload[:CONTROL_MAX])
        else:
            log.warning("Unexpected client frame type=%s", TYPE_NAMES.get(frame.frame_type, hex(frame.frame_type)))

    def run(self) -> None:
        self.conn.settimeout(2.0)
        log.info("MCU connected: %s:%s", self.addr[0], self.addr[1])
        try:
            while True:
                try:
                    chunk = self.conn.recv(16 * 1024)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                self.parser.feed(chunk)
                while True:
                    frame = self.parser.pop_frame()
                    if frame is None:
                        break
                    self.handle_frame(frame)
        finally:
            if self.active:
                log.warning("Connection ended with unfinished object=%d; keeping no partial file", self.active.object_id)
                self.active.discard()
            elapsed = time.perf_counter() - self.connected_at
            speed = self.total_payload_bytes / 1024 / elapsed if elapsed else 0.0
            log.info("Disconnected %s:%s total=%d bytes %.1f KiB/s parser_bad=%d/%d",
                     self.addr[0], self.addr[1], self.total_payload_bytes, speed,
                     self.parser.bad_headers, self.parser.bad_frames)

# ============================================================================
# TCP server
# ============================================================================
def handle_client(conn: socket.socket, addr: tuple, save_dir: Path) -> None:
    try:
        # ACK / NACK 都是小型且需要低延遲的控制 frame。
        # 關閉 Nagle，避免小 ACK 被延遲合併。
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        WSRPSession(conn, addr, save_dir).run()

    except Exception:
        log.exception("Session failed for %s", addr)

    finally:
        try:
            conn.close()
        except OSError:
            pass


def main() -> None:
    parser = argparse.ArgumentParser(description="WSRP v2 reliable STM32 Wi-Fi TCP server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--save-dir", default="./received")
    args = parser.parse_args()

    save_dir = Path(args.save_dir).resolve()
    save_dir.mkdir(parents=True, exist_ok=True)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(5)
    log.info("WSRP server listening on %s:%d", args.host, args.port)
    log.info("Saving complete objects to %s", save_dir)

    try:
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=handle_client, args=(conn, addr, save_dir), daemon=True).start()
    except KeyboardInterrupt:
        log.info("Server stopped")
    finally:
        srv.close()


if __name__ == "__main__":
    main()
