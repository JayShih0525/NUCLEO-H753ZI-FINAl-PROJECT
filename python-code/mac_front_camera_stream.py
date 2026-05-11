import time
import cv2
from flask import Flask, Response

# ============================================================
# Config
# ============================================================

HOST = "127.0.0.1"
PORT = 8080

CAMERA_INDEX = 0

OUTPUT_WIDTH = 640
OUTPUT_HEIGHT = 480

JPEG_QUALITY = 80
FPS = 15

app = Flask(__name__)


def open_camera():
    """
    開啟 Mac 內建前置鏡頭。
    CAMERA_INDEX = 0 通常是 FaceTime / built-in camera。
    """

    cap = cv2.VideoCapture(CAMERA_INDEX)

    if not cap.isOpened():
        raise RuntimeError("Cannot open camera. Try CAMERA_INDEX = 1 or 2.")

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, OUTPUT_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, OUTPUT_HEIGHT)
    cap.set(cv2.CAP_PROP_FPS, FPS)

    return cap


def generate_mjpeg():
    """
    產生 MJPEG stream。
    """

    cap = open_camera()
    frame_interval = 1.0 / FPS

    try:
        while True:
            start = time.time()

            ok, frame = cap.read()

            if not ok or frame is None:
                time.sleep(0.05)
                continue

            frame = cv2.resize(frame, (OUTPUT_WIDTH, OUTPUT_HEIGHT))

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

    finally:
        cap.release()


@app.route("/")
def index():
    return """
    <html>
        <body style="background:black;color:white;font-family:Arial;">
            <h2>Mac Front Camera MJPEG Stream</h2>
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
    print("Mac front camera stream is running:")
    print(f"http://{HOST}:{PORT}/stream.mjpg")
    print()
    print("Open browser:")
    print(f"http://{HOST}:{PORT}/")

    app.run(host=HOST, port=PORT, threaded=True)