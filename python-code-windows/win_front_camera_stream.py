import time
import cv2
from flask import Flask, Response

# ============================================================
# Config - Windows
# ============================================================

HOST = "0.0.0.0"
PORT = 8080

# Usually 0 = built-in webcam / default USB webcam
CAMERA_INDEX = 0

OUTPUT_WIDTH = 640
OUTPUT_HEIGHT = 480

JPEG_QUALITY = 80
FPS = 15

app = Flask(__name__)


def open_camera():
    """
    Open Windows webcam.
    CAMERA_INDEX = 0 usually means default camera.
    If it fails, try CAMERA_INDEX = 1 or 2.
    """

    # Windows recommended backend: DirectShow
    cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)

    if not cap.isOpened():
        raise RuntimeError(
            "Cannot open camera. Try CAMERA_INDEX = 1 or 2, "
            "or check Windows Camera Privacy Settings."
        )

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, OUTPUT_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, OUTPUT_HEIGHT)
    cap.set(cv2.CAP_PROP_FPS, FPS)

    return cap


def generate_mjpeg():
    """
    Generate MJPEG stream.
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
                b"Content-Type: image/jpeg\r\n\r\n"
                + jpg_bytes
                + b"\r\n"
            )

            elapsed = time.time() - start
            sleep_time = frame_interval - elapsed

            if sleep_time > 0:
                time.sleep(sleep_time)

    finally:
        cap.release()


@app.route("/")
def index():
    return f"""
    <html>
        <body style="background:black;color:white;font-family:Arial;">
            <h2>Windows Camera MJPEG Stream</h2>
            <p>Stream URL:</p>
            <code>http://127.0.0.1:{PORT}/stream.mjpg</code>
            <br><br>
            <img src="/stream.mjpg" width="{OUTPUT_WIDTH}">
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
    print("Windows camera stream is running:")
    print(f"http://127.0.0.1:{PORT}/stream.mjpg")
    print()
    print("Open browser:")
    print(f"http://127.0.0.1:{PORT}/")

    app.run(host=HOST, port=PORT, threaded=True)