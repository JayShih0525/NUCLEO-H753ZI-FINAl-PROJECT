from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np

from camera_serial import open_camera_port, receive_frame


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture one ESP32-S3-CAM JPEG")
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--output", type=Path, default=Path("captured_photo.jpg"))
    parser.add_argument("--preview", action="store_true")
    args = parser.parse_args()

    with open_camera_port(args.port, args.baud) as port:
        port.write(b"CAPTURE\n")
        port.flush()
        frame = receive_frame(port)

    args.output.write_bytes(frame.jpeg)
    image = cv2.imdecode(np.frombuffer(frame.jpeg, dtype=np.uint8), cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError("OpenCV could not decode the received JPEG")

    height, width = image.shape[:2]
    print(
        f"Saved frame {frame.frame_id}: {width}x{height}, "
        f"{len(frame.jpeg)} bytes -> {args.output.resolve()}"
    )
    if args.preview:
        cv2.imshow("ESP32-S3-CAM photo", image)
        print("Press any key in the image window to close.")
        cv2.waitKey(0)
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
