import socket
import struct
import time

import cv2
import numpy as np

ESP32_IP = "192.168.4.1"
TCP_PORT = 5000

MAX_JPEG_SIZE = 2 * 1024 * 1024
SOCKET_TIMEOUT_SECONDS = 5.0


def receive_exact(sock: socket.socket, size: int) -> bytes:
    """Receive exactly size bytes or raise ConnectionError."""
    data = bytearray()

    while len(data) < size:
        chunk = sock.recv(size - len(data))

        if not chunk:
            raise ConnectionError("ESP32 closed the TCP connection")

        data.extend(chunk)

    return bytes(data)


def main() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(SOCKET_TIMEOUT_SECONDS)

    print(f"Connecting to ESP32 TCP server {ESP32_IP}:{TCP_PORT}...")
    sock.connect((ESP32_IP, TCP_PORT))
    print("Connected. Press Q or ESC to close.")

    frame_count = 0
    start_time = time.time()

    try:
        while True:
            # ESP32 sends:
            # [4-byte JPEG length, big endian][JPEG bytes]
            length_data = receive_exact(sock, 4)
            jpeg_length = struct.unpack(">I", length_data)[0]

            if jpeg_length == 0 or jpeg_length > MAX_JPEG_SIZE:
                raise ValueError(f"Invalid JPEG length: {jpeg_length}")

            jpeg_data = receive_exact(sock, jpeg_length)

            image_array = np.frombuffer(jpeg_data, dtype=np.uint8)
            image = cv2.imdecode(image_array, cv2.IMREAD_COLOR)

            if image is None:
                print(f"JPEG decode failed, length={jpeg_length}")
                continue

            frame_count += 1
            elapsed = time.time() - start_time
            fps = frame_count / elapsed if elapsed > 0 else 0.0

            cv2.putText(
                image,
                f"TCP FPS: {fps:.1f}  JPEG: {jpeg_length} bytes",
                (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 255, 0),
                2,
            )

            cv2.imshow("ESP32 AP TCP Camera", image)

            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break

    except (ConnectionError, OSError, ValueError) as error:
        print(f"Connection stopped: {error}")

    finally:
        sock.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
