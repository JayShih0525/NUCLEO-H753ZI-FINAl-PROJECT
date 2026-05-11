import time
import cv2
import numpy as np
from flask import Flask, Response
import mss

# ============================================================
# Config
# ============================================================

HOST = "127.0.0.1"
HOST = "0.0.0.0"
PORT = 8080

# 你要輸出的螢幕影像大小
OUTPUT_WIDTH = 640
OUTPUT_HEIGHT = 480

# JPEG 品質
JPEG_QUALITY = 80

# FPS
FPS = 15

app = Flask(__name__)


def capture_screen_frame():
    """
    擷取 Mac 螢幕畫面，回傳 OpenCV BGR frame。
    """
    with mss.mss() as sct:
        # monitor 1 通常是主螢幕
        monitor = sct.monitors[1]

        screenshot = sct.grab(monitor)

        # mss 是 BGRA
        frame = np.array(screenshot)

        # BGRA -> BGR
        frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)

        # resize 成你要送去加密的大小
        frame = cv2.resize(frame, (OUTPUT_WIDTH, OUTPUT_HEIGHT))

        return frame


def generate_mjpeg():
    """
    產生 MJPEG stream。
    """
    frame_interval = 1.0 / FPS

    while True:
        start = time.time()

        frame = capture_screen_frame()

        ok, jpg = cv2.imencode(
            ".jpg",
            frame,
            [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY],
        )

        if not ok:
            continue

        jpg_bytes = jpg.tobytes()

        yield (
            b"--frame\r\n"
            b"Content-Type: image/jpeg\r\n\r\n" +
            jpg_bytes +
            b"\r\n"
        )

        elapsed = time.time() - start
        sleep_time = frame_interval - elapsed

        if sleep_time > 0:
            time.sleep(sleep_time)


@app.route("/")
def index():
    return """
    <html>
        <body style="background:black;color:white;font-family:Arial;">
            <h2>Mac Screen MJPEG Stream</h2>
            <p>Stream URL:</p>
            <code>http://127.0.0.1:8080/stream.mjpg</code>
            <br><br>
            <img src="/stream.mjpg" width="640">
        </body>
    </html>
    """


@app.route("/stream.mjpg")
def stream():
    return Response(
        generate_mjpeg(),
        mimetype="multipart/x-mixed-replace; boundary=frame",
    )


if __name__ == "__main__":
    print("Mac screen stream is running:")
    print(f"http://{HOST}:{PORT}/stream.mjpg")
    print()
    print("Open browser:")
    print(f"http://{HOST}:{PORT}/")

    app.run(host=HOST, port=PORT, threaded=True)