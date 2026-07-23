from __future__ import annotations

import socket
import struct
import threading
import time
from dataclasses import dataclass, field
from typing import Optional

import cv2
import numpy as np


# =====================================================
# UDP 設定
# =====================================================

LISTEN_IP = "0.0.0.0"
LISTEN_PORT = 5005

# 必須和 ESP32 相同
PACKET_MAGIC = 0x43414D31

# ESP32 Header：!IIHHHH
HEADER_FORMAT = "!IIHHHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

MAX_UDP_PACKET_SIZE = 2048

# 一張影格超過此時間仍未收完，直接丟棄
FRAME_TIMEOUT_SECONDS = 0.080

# 防止異常封包要求過多記憶體
MAX_PACKET_COUNT = 256
MAX_JPEG_SIZE = 200_000

FLAG_FIRST_PACKET = 0x0001
FLAG_LAST_PACKET = 0x0002


# =====================================================
# Frame 組裝資料
# =====================================================

@dataclass
class FrameAssembly:
    frame_id: int
    packet_count: int
    start_time: float = field(default_factory=time.monotonic)

    packets: list[Optional[bytes]] = field(init=False)
    received_count: int = 0
    received_bytes: int = 0

    def __post_init__(self) -> None:
        self.packets = [None] * self.packet_count

    def add_packet(
        self,
        packet_id: int,
        payload: bytes,
    ) -> bool:
        if not 0 <= packet_id < self.packet_count:
            return False

        # 重複封包不要重複計算
        if self.packets[packet_id] is not None:
            return False

        self.packets[packet_id] = payload
        self.received_count += 1
        self.received_bytes += len(payload)

        return True

    def is_complete(self) -> bool:
        return self.received_count == self.packet_count

    def has_expired(self, now: float) -> bool:
        return (
            now - self.start_time
            > FRAME_TIMEOUT_SECONDS
        )

    def build_jpeg(self) -> Optional[bytes]:
        if not self.is_complete():
            return None

        if self.received_bytes > MAX_JPEG_SIZE:
            return None

        if any(packet is None for packet in self.packets):
            return None

        jpeg = b"".join(
            packet
            for packet in self.packets
            if packet is not None
        )

        if len(jpeg) < 4:
            return None

        # JPEG SOI / EOI
        if not jpeg.startswith(b"\xFF\xD8"):
            return None

        if not jpeg.endswith(b"\xFF\xD9"):
            return None

        return jpeg


# =====================================================
# 最新影格共享區
# =====================================================

class LatestFrame:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._frame: Optional[np.ndarray] = None
        self._frame_id: Optional[int] = None
        self._timestamp = 0.0

    def update(
        self,
        frame_id: int,
        frame: np.ndarray,
    ) -> None:
        with self._lock:
            self._frame_id = frame_id
            self._frame = frame
            self._timestamp = time.monotonic()

    def get(
        self,
    ) -> tuple[
        Optional[int],
        Optional[np.ndarray],
        float,
    ]:
        with self._lock:
            frame_id = self._frame_id
            timestamp = self._timestamp

            if self._frame is None:
                return frame_id, None, timestamp

            # 複製 reference 即可；update 會換成新陣列
            return frame_id, self._frame, timestamp


# =====================================================
# UDP Receiver
# =====================================================

class UdpCameraReceiver:
    def __init__(
        self,
        listen_ip: str,
        listen_port: int,
    ) -> None:
        self.listen_ip = listen_ip
        self.listen_port = listen_port

        self.latest_frame = LatestFrame()

        self.running = False
        self.thread: Optional[threading.Thread] = None

        self.current_assembly: Optional[
            FrameAssembly
        ] = None

        self.completed_frames = 0
        self.dropped_frames = 0
        self.invalid_packets = 0
        self.old_packets = 0

        self.received_packets = 0
        self.received_bytes = 0

        self.last_stats_time = time.monotonic()
        self.last_completed_frames = 0
        self.last_received_bytes = 0

    def start(self) -> None:
        if self.running:
            return

        self.running = True

        self.thread = threading.Thread(
            target=self._receive_loop,
            name="udp-camera-receiver",
            daemon=True,
        )

        self.thread.start()

    def stop(self) -> None:
        self.running = False

        if self.thread is not None:
            self.thread.join(timeout=2.0)

    @staticmethod
    def _is_newer_frame(
        new_frame_id: int,
        old_frame_id: int,
    ) -> bool:
        """
        支援 uint32 frame_id 溢位。

        差值在 1 到 2^31-1 之間，
        視為 new_frame_id 比 old_frame_id 新。
        """
        difference = (
            new_frame_id - old_frame_id
        ) & 0xFFFFFFFF

        return 0 < difference < 0x80000000

    def _start_new_frame(
        self,
        frame_id: int,
        packet_count: int,
    ) -> None:
        if (
            self.current_assembly is not None
            and not self.current_assembly.is_complete()
            and self.current_assembly.received_count > 0
        ):
            self.dropped_frames += 1

        self.current_assembly = FrameAssembly(
            frame_id=frame_id,
            packet_count=packet_count,
        )

    def _handle_packet(
        self,
        data: bytes,
    ) -> None:
        if len(data) < HEADER_SIZE:
            self.invalid_packets += 1
            return

        try:
            (
                magic,
                frame_id,
                packet_id,
                packet_count,
                payload_size,
                flags,
            ) = struct.unpack(
                HEADER_FORMAT,
                data[:HEADER_SIZE],
            )
        except struct.error:
            self.invalid_packets += 1
            return

        if magic != PACKET_MAGIC:
            self.invalid_packets += 1
            return

        if (
            packet_count == 0
            or packet_count > MAX_PACKET_COUNT
        ):
            self.invalid_packets += 1
            return

        if packet_id >= packet_count:
            self.invalid_packets += 1
            return

        payload = data[HEADER_SIZE:]

        if (
            payload_size == 0
            or payload_size != len(payload)
        ):
            self.invalid_packets += 1
            return

        if (
            flags & FLAG_FIRST_PACKET
            and packet_id != 0
        ):
            self.invalid_packets += 1
            return

        if (
            flags & FLAG_LAST_PACKET
            and packet_id != packet_count - 1
        ):
            self.invalid_packets += 1
            return

        now = time.monotonic()

        # 超時的舊影格立即丟掉
        if (
            self.current_assembly is not None
            and self.current_assembly.has_expired(now)
        ):
            if not self.current_assembly.is_complete():
                self.dropped_frames += 1

            self.current_assembly = None

        if self.current_assembly is None:
            self._start_new_frame(
                frame_id,
                packet_count,
            )

        elif (
            frame_id
            == self.current_assembly.frame_id
        ):
            if (
                packet_count
                != self.current_assembly.packet_count
            ):
                self.invalid_packets += 1
                return

        elif self._is_newer_frame(
            frame_id,
            self.current_assembly.frame_id,
        ):
            self._start_new_frame(
                frame_id,
                packet_count,
            )

        else:
            # 舊 frame 的延遲封包
            self.old_packets += 1
            return

        assembly = self.current_assembly

        if assembly is None:
            return

        assembly.add_packet(
            packet_id,
            payload,
        )

        if not assembly.is_complete():
            return

        jpeg = assembly.build_jpeg()

        # 組裝完成後立刻釋放 assembly
        self.current_assembly = None

        if jpeg is None:
            self.dropped_frames += 1
            return

        jpeg_array = np.frombuffer(
            jpeg,
            dtype=np.uint8,
        )

        frame = cv2.imdecode(
            jpeg_array,
            cv2.IMREAD_COLOR,
        )

        if frame is None:
            self.dropped_frames += 1
            return

        self.completed_frames += 1

        self.latest_frame.update(
            frame_id,
            frame,
        )

    def _print_stats(self) -> None:
        now = time.monotonic()
        elapsed = now - self.last_stats_time

        if elapsed < 5.0:
            return

        new_frames = (
            self.completed_frames
            - self.last_completed_frames
        )

        new_bytes = (
            self.received_bytes
            - self.last_received_bytes
        )

        fps = new_frames / elapsed

        rate_kbps = (
            new_bytes /
            elapsed /
            1024.0
        )

        total_finished = (
            self.completed_frames
            + self.dropped_frames
        )

        success_rate = 0.0

        if total_finished > 0:
            success_rate = (
                self.completed_frames /
                total_finished *
                100.0
            )

        print(
            f"FPS={fps:.1f}, "
            f"Rate={rate_kbps:.1f} KB/s, "
            f"Completed={self.completed_frames}, "
            f"Dropped={self.dropped_frames}, "
            f"Success={success_rate:.1f}%, "
            f"InvalidPackets={self.invalid_packets}, "
            f"OldPackets={self.old_packets}"
        )

        self.last_stats_time = now
        self.last_completed_frames = (
            self.completed_frames
        )
        self.last_received_bytes = (
            self.received_bytes
        )

    def _receive_loop(self) -> None:
        sock = socket.socket(
            socket.AF_INET,
            socket.SOCK_DGRAM,
        )

        try:
            # 加大作業系統 UDP receive buffer
            sock.setsockopt(
                socket.SOL_SOCKET,
                socket.SO_RCVBUF,
                4 * 1024 * 1024,
            )

            sock.bind(
                (
                    self.listen_ip,
                    self.listen_port,
                )
            )

            # 小 timeout，定期清理過期影格
            sock.settimeout(0.020)

            actual_buffer = sock.getsockopt(
                socket.SOL_SOCKET,
                socket.SO_RCVBUF,
            )

            print(
                f"Listening on "
                f"{self.listen_ip}:{self.listen_port}"
            )

            print(
                f"Socket receive buffer: "
                f"{actual_buffer} bytes"
            )

            while self.running:
                try:
                    data, _address = sock.recvfrom(
                        MAX_UDP_PACKET_SIZE
                    )

                    self.received_packets += 1
                    self.received_bytes += len(data)

                    self._handle_packet(data)

                except socket.timeout:
                    now = time.monotonic()

                    if (
                        self.current_assembly is not None
                        and self.current_assembly.has_expired(now)
                    ):
                        if not self.current_assembly.is_complete():
                            self.dropped_frames += 1

                        self.current_assembly = None

                except OSError as error:
                    if self.running:
                        print(
                            f"Socket error: {error}"
                        )

                    break

                self._print_stats()

        finally:
            sock.close()


# =====================================================
# 顯示畫面
# =====================================================

def main() -> None:
    receiver = UdpCameraReceiver(
        LISTEN_IP,
        LISTEN_PORT,
    )

    receiver.start()

    print("Waiting for ESP32 UDP frames...")
    print("Press Q or ESC to quit.")

    displayed_frame_id: Optional[int] = None
    display_frames = 0
    display_stats_time = time.monotonic()

    try:
        while True:
            (
                frame_id,
                frame,
                received_time,
            ) = receiver.latest_frame.get()

            if (
                frame is not None
                and frame_id is not None
                and frame_id != displayed_frame_id
            ):
                displayed_frame_id = frame_id
                display_frames += 1

                age_ms = (
                    time.monotonic()
                    - received_time
                ) * 1000.0

                display = frame.copy()

                cv2.putText(
                    display,
                    f"Frame: {frame_id}",
                    (10, 25),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.65,
                    (255, 255, 255),
                    2,
                    cv2.LINE_AA,
                )

                cv2.putText(
                    display,
                    f"Display age: {age_ms:.1f} ms",
                    (10, 50),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.65,
                    (255, 255, 255),
                    2,
                    cv2.LINE_AA,
                )

                cv2.imshow(
                    "ESP32-S3 UDP Camera",
                    display,
                )

            key = cv2.waitKey(1) & 0xFF

            if key in (ord("q"), 27):
                break

            now = time.monotonic()

            if now - display_stats_time >= 5.0:
                display_fps = (
                    display_frames /
                    (now - display_stats_time)
                )

                print(
                    f"Display FPS={display_fps:.1f}"
                )

                display_frames = 0
                display_stats_time = now

            # 不需要高速空轉
            time.sleep(0.001)

    except KeyboardInterrupt:
        pass

    finally:
        receiver.stop()
        cv2.destroyAllWindows()

        print("Receiver stopped.")


if __name__ == "__main__":
    main()