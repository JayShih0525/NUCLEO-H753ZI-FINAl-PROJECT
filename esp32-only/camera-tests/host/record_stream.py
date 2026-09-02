from __future__ import annotations

import argparse
import time
from pathlib import Path

import cv2
import numpy as np

from camera_serial import open_camera_port, receive_frame


def main() -> int:
    parser = argparse.ArgumentParser(description="Display and record ESP32 JPEG stream")
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--output", type=Path, default=Path("camera_recording.avi"))
    parser.add_argument("--output-fps", type=float, default=14.0)
    parser.add_argument("--no-display", action="store_true")
    args = parser.parse_args()

    writer: cv2.VideoWriter | None = None
    received = 0
    started: float | None = None

    try:
        with open_camera_port(args.port, args.baud) as port:
            port.write(b"STREAM_START\n")
            port.flush()
            started = time.monotonic()

            while time.monotonic() - started < args.seconds:
                frame = receive_frame(port)
                image = cv2.imdecode(
                    np.frombuffer(frame.jpeg, dtype=np.uint8), cv2.IMREAD_COLOR
                )
                if image is None:
                    print(f"Frame {frame.frame_id}: JPEG decode failed")
                    continue

                if writer is None:
                    height, width = image.shape[:2]
                    writer = cv2.VideoWriter(
                        str(args.output),
                        cv2.VideoWriter_fourcc(*"MJPG"),
                        args.output_fps,
                        (width, height),
                    )
                    if not writer.isOpened():
                        raise RuntimeError("Could not open AVI video writer")

                writer.write(image)
                received += 1
                elapsed = time.monotonic() - started
                print(
                    f"Frame={frame.frame_id} JPEG={len(frame.jpeg)} bytes "
                    f"received={received} average={received / elapsed:.2f} FPS"
                )

                if not args.no_display:
                    cv2.imshow("ESP32-S3-CAM stream", image)
                    if cv2.waitKey(1) & 0xFF in (ord("q"), 27):
                        break

            port.write(b"STREAM_STOP\n")
            port.flush()
    finally:
        if writer is not None:
            writer.release()
        cv2.destroyAllWindows()

    if started is None:
        raise RuntimeError("Camera stream did not start")
    elapsed = time.monotonic() - started
    if received == 0:
        raise RuntimeError("No camera frames were received")
    print(
        f"Saved {received} frames in {elapsed:.2f}s "
        f"({received / elapsed:.2f} FPS) -> {args.output.resolve()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
