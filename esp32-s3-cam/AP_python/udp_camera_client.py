import socket
import struct
import time
from dataclasses import dataclass, field

import cv2
import numpy as np

ESP32_IP = "192.168.4.1"
CONTROL_PORT = 5004
LISTEN_PORT = 5005

UDP_MAGIC = 0x43414D31
HEADER_FORMAT = ">IIHHHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

FRAME_TIMEOUT_SECONDS = 0.5
MAX_PACKET_COUNT = 2048


@dataclass
class FrameBuffer:
    packet_count: int
    created_at: float
    packets: dict[int, bytes] = field(default_factory=dict)

    def complete(self) -> bool:
        return len(self.packets) == self.packet_count

    def build(self) -> bytes:
        return b"".join(self.packets[index] for index in range(self.packet_count))


def register_with_esp32(sock: socket.socket) -> None:
    """Tell ESP32 which AP client should receive the camera packets."""
    sock.sendto(b"HELLO", (ESP32_IP, CONTROL_PORT))
    print(
        f"Registration sent to ESP32 {ESP32_IP}:{CONTROL_PORT}. "
        f"Listening on UDP port {LISTEN_PORT}."
    )


def main() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
    sock.bind(("0.0.0.0", LISTEN_PORT))
    sock.settimeout(0.2)

    register_with_esp32(sock)

    frames: dict[int, FrameBuffer] = {}
    displayed_frames = 0
    dropped_frames = 0
    start_time = time.time()
    last_registration = 0.0

    print("Press Q or ESC to close.")

    try:
        while True:
            now = time.time()

            # 每 2 秒重新註冊一次，避免 ESP32 重啟後不知道電腦 IP。
            if now - last_registration >= 2.0:
                register_with_esp32(sock)
                last_registration = now

            try:
                packet, _ = sock.recvfrom(65535)
            except socket.timeout:
                packet = b""

            if packet and len(packet) >= HEADER_SIZE:
                (
                    magic,
                    frame_id,
                    packet_id,
                    packet_count,
                    payload_size,
                    flags,
                ) = struct.unpack(HEADER_FORMAT, packet[:HEADER_SIZE])

                payload = packet[HEADER_SIZE:]

                if magic != UDP_MAGIC:
                    continue

                if (
                    packet_count == 0
                    or packet_count > MAX_PACKET_COUNT
                    or packet_id >= packet_count
                    or payload_size != len(payload)
                ):
                    continue

                frame = frames.get(frame_id)

                if frame is None or frame.packet_count != packet_count:
                    frame = FrameBuffer(
                        packet_count=packet_count,
                        created_at=now,
                    )
                    frames[frame_id] = frame

                frame.packets[packet_id] = payload

                if frame.complete():
                    jpeg_data = frame.build()
                    del frames[frame_id]

                    image_array = np.frombuffer(jpeg_data, dtype=np.uint8)
                    image = cv2.imdecode(image_array, cv2.IMREAD_COLOR)

                    if image is not None:
                        displayed_frames += 1
                        elapsed = time.time() - start_time
                        fps = displayed_frames / elapsed if elapsed > 0 else 0.0

                        cv2.putText(
                            image,
                            (
                                f"UDP FPS: {fps:.1f}  "
                                f"JPEG: {len(jpeg_data)} bytes  "
                                f"Drop: {dropped_frames}"
                            ),
                            (10, 30),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.65,
                            (0, 255, 0),
                            2,
                        )

                        cv2.imshow("ESP32 AP UDP Camera", image)

            # 清除超時且沒有收完整的 frame。
            expired_ids = [
                frame_id
                for frame_id, frame in frames.items()
                if now - frame.created_at > FRAME_TIMEOUT_SECONDS
            ]

            for frame_id in expired_ids:
                del frames[frame_id]
                dropped_frames += 1

            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break

    finally:
        sock.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
