import cv2
import time
from typing import Optional, Tuple


class URLScreenCapture:
    def __init__(
        self,
        url: str,
        width: Optional[int] = None,
        height: Optional[int] = None,
        jpeg_quality: int = 80,
        grayscale: bool = False,
        reconnect_after_fail: int = 30,
    ):
        self.url = url
        self.width = width
        self.height = height
        self.jpeg_quality = jpeg_quality
        self.grayscale = grayscale
        self.reconnect_after_fail = reconnect_after_fail

        self.cap = None
        self.fail_count = 0

    def connect(self) -> bool:
        self.release()

        self.cap = cv2.VideoCapture(self.url)

        if not self.cap.isOpened():
            self.cap = None
            return False

        self.fail_count = 0
        return True

    def is_connected(self) -> bool:
        return self.cap is not None and self.cap.isOpened()

    def read_frame(self):
        if not self.is_connected():
            if not self.connect():
                return None

        ok, frame = self.cap.read()

        if not ok or frame is None:
            self.fail_count += 1

            if self.fail_count >= self.reconnect_after_fail:
                self.connect()

            return None

        self.fail_count = 0

        if self.width is not None and self.height is not None:
            frame = cv2.resize(frame, (self.width, self.height))

        if self.grayscale:
            frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        return frame

    def frame_to_jpg_bytes(self, frame) -> bytes:
        encode_params = [
            int(cv2.IMWRITE_JPEG_QUALITY),
            int(self.jpeg_quality),
        ]

        ok, encoded = cv2.imencode(".jpg", frame, encode_params)

        if not ok:
            raise RuntimeError("Failed to encode frame to JPG bytes")

        return encoded.tobytes()

    def get_jpg_bytes(self) -> Optional[bytes]:
        frame = self.read_frame()

        if frame is None:
            return None

        return self.frame_to_jpg_bytes(frame)

    def get_frame_and_jpg_bytes(self):
        frame = self.read_frame()

        if frame is None:
            return None, None

        jpg_bytes = self.frame_to_jpg_bytes(frame)

        return frame, jpg_bytes

    def show_live(self, window_name: str = "URL Screen", delay: int = 1):
        frame = self.read_frame()

        if frame is None:
            return False

        cv2.imshow(window_name, frame)

        key = cv2.waitKey(delay) & 0xFF

        if key == ord("q"):
            return False

        return True

    def save_jpg_bytes(self, output_path: str) -> bool:
        jpg_bytes = self.get_jpg_bytes()

        if jpg_bytes is None:
            return False

        with open(output_path, "wb") as f:
            f.write(jpg_bytes)

        return True

    def release(self):
        if self.cap is not None:
            self.cap.release()
            self.cap = None

    def close_windows(self):
        cv2.destroyAllWindows()