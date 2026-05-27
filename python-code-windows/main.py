import time
import threading
import cv2
import numpy as np

from library.uart3_protocol import UART3Protocol
from library.ml_kem_app import STM32MLKEM
from library.aes_gcm_app import STM32AESGCM
from library.url_screen_capture import URLScreenCapture

# ============================================================
# Config
# ============================================================

# APP: Simple IP Cam
URL = "http://172.20.10.2:8080/stream.mjpg"
URL = "http://127.0.0.1:8080/stream.mjpg"

PORT = "COM6"
BAUD = 4000000
AES_GCM_APP_MAX_SIZE = 131072
TIMEOUT = 0.01

# 這是實際送去 STM32 加密的圖片大小
DATA_WIDTH = 640
DATA_HEIGHT = 480

# 初始 JPEG quality，可以在執行時用 + / - 改
JPEG_QUALITY = 50
JPEG_QUALITY_MIN = 0
JPEG_QUALITY_MAX = 100
JPEG_QUALITY_STEP = 5

# 原始畫面與解密畫面的顯示大小
DISPLAY_WIDTH = 640
DISPLAY_HEIGHT = 480

# 第二個 status dashboard 畫面大小
STATUS_WIDTH = 1200
STATUS_HEIGHT = 800

# 每幾秒送一張 frame 給 STM32 加密
ENCRYPT_GAP = 0.0

# 黑白畫面
GRAYSCALE = False

# ============================================================
# Helper functions
# ============================================================

def resize_for_display(frame):
    """
    只改變顯示大小，不改變實際加密資料大小。
    """
    return cv2.resize(frame, (DISPLAY_WIDTH, DISPLAY_HEIGHT))


def jpg_bytes_to_frame(jpg_bytes: bytes):
    """
    把 JPG bytes 解碼回 OpenCV frame。
    """
    arr = np.frombuffer(jpg_bytes, dtype=np.uint8)
    frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)

    if frame is None:
        raise RuntimeError("Failed to decode JPG bytes")

    return frame


def frame_to_jpg_bytes_with_quality(frame, quality: int) -> bytes:
    """
    用目前 JPEG quality 把 frame 壓成 JPG bytes。
    quality 越高，畫質越好，但 bytes 越大。
    """
    quality = int(max(JPEG_QUALITY_MIN, min(JPEG_QUALITY_MAX, quality)))

    ok, encoded = cv2.imencode(
        ".jpg",
        frame,
        [int(cv2.IMWRITE_JPEG_QUALITY), quality],
    )

    if not ok:
        raise RuntimeError("Failed to encode frame to JPG")

    return encoded.tobytes()


def put_text(img, text, x, y, scale=0.55, thickness=1):
    """
    白色文字。
    """
    cv2.putText(
        img,
        str(text),
        (x, y),
        cv2.FONT_HERSHEY_SIMPLEX,
        scale,
        (235, 235, 235),
        thickness,
        cv2.LINE_AA,
    )


def put_small_text(img, text, x, y):
    """
    小字。
    """
    cv2.putText(
        img,
        str(text),
        (x, y),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.45,
        (200, 200, 200),
        1,
        cv2.LINE_AA,
    )


def put_value(img, label, value, x, y, value_x):
    """
    label + value 顯示。
    """
    put_small_text(img, label, x, y)
    put_text(img, value, value_x, y, scale=0.58, thickness=1)


def draw_panel(img, x1, y1, x2, y2, title):
    """
    畫 dashboard 區塊。
    """
    cv2.rectangle(img, (x1, y1), (x2, y2), (80, 80, 80), 1)

    # title 背景
    cv2.rectangle(img, (x1, y1), (x2, y1 + 38), (25, 25, 25), -1)

    cv2.putText(
        img,
        title,
        (x1 + 18, y1 + 26),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.65,
        (255, 255, 255),
        1,
        cv2.LINE_AA,
    )


def make_encrypted_status_image(state):
    """
    第二個視窗：黑色 dashboard。
    不顯示 ciphertext noise image。
    """

    img = np.zeros((STATUS_HEIGHT, STATUS_WIDTH, 3), dtype=np.uint8)

    with state.lock:
        frame_count = state.frame_count
        ok_count = state.ok_count
        err_count = state.err_count

        plain_len = state.last_plain_len
        cipher_len = state.last_cipher_len
        elapsed = state.last_elapsed

        nonce = state.last_nonce
        tag = state.last_tag

        last_status = state.last_status
        jpeg_quality = state.jpeg_quality

    nonce_hex = nonce.hex() if nonce is not None else "None"
    tag_hex = tag.hex() if tag is not None else "None"

    if AES_GCM_APP_MAX_SIZE > 0:
        used_percent = plain_len / AES_GCM_APP_MAX_SIZE * 100
    else:
        used_percent = 0.0

    if elapsed > 0:
        fps_like = 1.0 / elapsed
    else:
        fps_like = 0.0

    # ========================================================
    # Header
    # ========================================================

    put_text(
        img,
        "STM32 AES-GCM Live Encryption Dashboard",
        40,
        55,
        scale=0.9,
        thickness=2,
    )

    put_small_text(
        img,
        "Status view only - this screen does not display ciphertext as an image",
        42,
        86,
    )

    # ========================================================
    # Panel 1: Runtime Status
    # ========================================================

    draw_panel(img, 40, 115, 560, 255, "Runtime Status")

    put_value(img, "Frame", f"{frame_count:,}", 70, 175, 210)
    put_value(img, "OK", f"{ok_count:,}", 70, 215, 210)

    put_value(img, "Status", last_status, 330, 175, 440)
    put_value(img, "ERR", f"{err_count:,}", 330, 215, 440)

    # ========================================================
    # Panel 2: Image Input Config
    # ========================================================

    draw_panel(img, 600, 115, 1160, 255, "Image Input Config")

    put_value(img, "Data size", f"{DATA_WIDTH} x {DATA_HEIGHT}", 630, 175, 790)
    put_value(img, "JPEG quality", jpeg_quality, 630, 215, 790)

    put_value(img, "Display size", f"{DISPLAY_WIDTH} x {DISPLAY_HEIGHT}", 900, 175, 1040)
    put_value(img, "Encrypt gap", f"{ENCRYPT_GAP:.3f}s", 900, 215, 1040)

    # ========================================================
    # Panel 3: AES-GCM Payload Size
    # ========================================================

    draw_panel(img, 40, 285, 1160, 460, "AES-GCM Payload Size")

    put_value(img, "Plaintext bytes", f"{plain_len:,}", 70, 350, 280)
    put_value(img, "Ciphertext bytes", f"{cipher_len:,}", 70, 395, 280)

    put_value(img, "Max payload bytes", f"{AES_GCM_APP_MAX_SIZE:,}", 500, 350, 720)
    put_value(img, "Used", f"{used_percent:.2f}%", 500, 395, 720)

    put_value(img, "Elapsed", f"{elapsed:.3f}s", 850, 350, 980)
    put_value(img, "Rate", f"{fps_like:.2f} frame/s", 850, 395, 980)

    # usage bar
    bar_x = 70
    bar_y = 425
    bar_w = 1030
    bar_h = 16

    cv2.rectangle(
        img,
        (bar_x, bar_y),
        (bar_x + bar_w, bar_y + bar_h),
        (90, 90, 90),
        1,
    )

    fill_w = int(min(max(used_percent, 0.0), 100.0) / 100.0 * bar_w)

    cv2.rectangle(
        img,
        (bar_x, bar_y),
        (bar_x + fill_w, bar_y + bar_h),
        (230, 230, 230),
        -1,
    )

    # ========================================================
    # Panel 4: Nonce / Tag
    # ========================================================

    draw_panel(img, 40, 490, 1160, 720, "AES-GCM Nonce / Tag")

    put_small_text(img, "Nonce", 70, 555)
    put_text(img, nonce_hex, 70, 590, scale=0.62, thickness=1)

    put_small_text(img, "Tag", 70, 645)
    put_text(img, tag_hex, 70, 680, scale=0.62, thickness=1)

    # ========================================================
    # Footer
    # ========================================================

    put_small_text(
        img,
        "Press + / - to change JPEG quality, q to quit",
        40,
        STATUS_HEIGHT - 28,
    )

    return img


def kem_handshake_with_retry(kem: STM32MLKEM, retry: int = 3):
    """
    ML-KEM handshake:
    Python 取得 STM32 public key
    Python 產生 kem_ciphertext + shared_secret
    Python 把 kem_ciphertext 傳給 STM32
    STM32 decapsulate 後設定 AES-GCM key
    """
    last_error = None

    for attempt in range(retry):
        try:
            kem.clear()

            if attempt > 0:
                print("Trying KEM rekey...")
                kem.rekey()

            result = kem.handshake()

            print("KEM handshake OK")
            print("public_key len:", len(result["public_key"]))
            print("kem_ciphertext len:", len(result["kem_ciphertext"]))
            print("shared_secret len:", len(result["shared_secret_python"]))
            print("aes_key:", result["aes_key"].hex())

            return result

        except RuntimeError as e:
            last_error = e
            print(f"KEM handshake failed at attempt {attempt + 1}: {e}")

            try:
                kem.clear()
            except Exception as clear_error:
                print("KEM clear failed:", clear_error)

            time.sleep(0.2)

    raise RuntimeError(f"KEM handshake failed after {retry} attempts: {last_error}")


# ============================================================
# Shared state
# ============================================================

class SharedFrameState:
    def __init__(self):
        self.lock = threading.Lock()

        self.latest_frame = None
        self.latest_jpg_bytes = None

        self.decrypted_frame = None

        self.frame_count = 0
        self.ok_count = 0
        self.err_count = 0

        self.last_nonce = None
        self.last_tag = None

        self.last_plain_len = 0
        self.last_cipher_len = 0
        self.last_elapsed = 0.0

        self.last_status = "INIT"

        # 可以即時調整的 JPEG quality
        self.jpeg_quality = JPEG_QUALITY

        self.stop = False

    def change_jpeg_quality(self, delta: int):
        """
        即時調整 JPEG quality。
        """
        with self.lock:
            self.jpeg_quality += delta

            if self.jpeg_quality > JPEG_QUALITY_MAX:
                self.jpeg_quality = JPEG_QUALITY_MAX

            if self.jpeg_quality < JPEG_QUALITY_MIN:
                self.jpeg_quality = JPEG_QUALITY_MIN

            self.last_status = f"JPEG quality changed to {self.jpeg_quality}"


# ============================================================
# Thread 1: capture camera
# ============================================================

def capture_display_thread(screen: URLScreenCapture, state: SharedFrameState):
    """
    這個 thread 只負責：
    1. 一直讀 URL camera frame
    2. 把最新 frame / JPG bytes 存進 shared state

    注意：不要在這裡 cv2.imshow()
    macOS 上 OpenCV GUI 要放 main thread。
    """

    try:
        while not state.stop:
            frame = screen.read_frame()

            if frame is None:
                with state.lock:
                    state.last_status = "Cannot get frame from URL"

                time.sleep(0.02)
                continue

            try:
                with state.lock:
                    current_quality = state.jpeg_quality

                jpg_bytes = frame_to_jpg_bytes_with_quality(frame, current_quality)

            except Exception as e:
                with state.lock:
                    state.last_status = f"JPG encode error: {e}"

                time.sleep(0.02)
                continue

            with state.lock:
                state.latest_frame = frame.copy()
                state.latest_jpg_bytes = jpg_bytes

            time.sleep(0.001)

    except Exception as e:
        with state.lock:
            state.last_status = f"capture thread error: {e}"

        state.stop = True


# ============================================================
# Thread 2: STM32 encrypt + Python local decrypt
# ============================================================

def encrypt_decrypt_thread(aes: STM32AESGCM, state: SharedFrameState, gap: float):
    """
    這個 thread 負責：
    1. 每 gap 秒拿最新 JPG bytes
    2. 傳給 STM32 AES-GCM encrypt
    3. Python local decrypt
    4. 更新狀態畫面和 decrypted frame

    只有這個 thread 使用 UART，所以不會跟其他 thread 搶 UART。
    """

    last_encrypt_time = 0.0

    try:
        while not state.stop:
            now = time.time()

            if now - last_encrypt_time < gap:
                time.sleep(0.01)
                continue

            with state.lock:
                jpg_bytes = state.latest_jpg_bytes

            if jpg_bytes is None:
                time.sleep(0.05)
                continue

            if len(jpg_bytes) > AES_GCM_APP_MAX_SIZE:
                with state.lock:
                    state.err_count += 1
                    state.last_status = (
                        f"JPG too large: {len(jpg_bytes)} > {AES_GCM_APP_MAX_SIZE}"
                    )

                    err_count = state.err_count
                    frame_count = state.frame_count

                    state.last_plain_len = len(jpg_bytes)
                    state.last_cipher_len = 0
                    state.last_elapsed = 0.0

                print()
                print("JPG TOO LARGE")
                print(f"Frame        : {frame_count}")
                print(f"JPG size     : {len(jpg_bytes)} bytes")
                print(f"Max size     : {AES_GCM_APP_MAX_SIZE} bytes")
                print(f"DATA_WIDTH   : {DATA_WIDTH}")
                print(f"DATA_HEIGHT  : {DATA_HEIGHT}")

                with state.lock:
                    current_quality = state.jpeg_quality

                print(f"JPEG_QUALITY : {current_quality}")
                print(f"ERR count    : {err_count}")
                print("Suggestion   : press '-' to reduce JPEG quality")
                print()

                last_encrypt_time = now
                continue

            try:
                with state.lock:
                    state.frame_count += 1
                    current_frame_count = state.frame_count

                result = aes.stm32_encrypt_local_decrypt_test(jpg_bytes)

                if not result["ok"]:
                    with state.lock:
                        state.err_count += 1
                        state.last_status = "AES-GCM mismatch"

                    last_encrypt_time = now
                    continue

                decrypted_frame = jpg_bytes_to_frame(result["decrypted"])

                with state.lock:
                    state.decrypted_frame = decrypted_frame

                    state.ok_count += 1

                    state.last_nonce = result["nonce"]
                    state.last_tag = result["tag"]

                    state.last_plain_len = result["plaintext_len"]
                    state.last_cipher_len = result["ciphertext_len"]
                    state.last_elapsed = result["elapsed"]

                    state.last_status = "OK"

                    ok_count = state.ok_count
                    err_count = state.err_count

                    plain_len = result["plaintext_len"]
                    cipher_len = result["ciphertext_len"]
                    elapsed = result["elapsed"]

                    nonce_hex = result["nonce"].hex()
                    tag_hex = result["tag"].hex()

                print(
                    f"Frame={current_frame_count} | "
                    f"OK={ok_count} | "
                    f"ERR={err_count} | "
                    f"OK | "
                    f"plain={plain_len} | "
                    f"cipher={cipher_len} | "
                    f"elapsed={elapsed:.3f}s | "
                    f"nonce={nonce_hex} | "
                    f"tag={tag_hex}",
                    end="\r",
                    flush=True,
                )

            except Exception as e:
                with state.lock:
                    state.err_count += 1
                    state.last_status = f"encrypt/decrypt error: {e}"

                print()
                print("Encrypt/decrypt error:", e)

                try:
                    aes.clear()
                    print("UART clear OK")

                except Exception as clear_error:
                    print("UART clear failed:", clear_error)

            last_encrypt_time = now

    except Exception as e:
        with state.lock:
            state.last_status = f"encrypt thread error: {e}"

        state.stop = True


# ============================================================
# Main thread: show windows
# ============================================================

def encrypted_decrypted_display_loop(state: SharedFrameState):
    """
    Main thread 顯示三個視窗：
    1. Original Frame
    2. Encrypted Status Dashboard
    3. Decrypted frame
    """

    cv2.namedWindow("1. Original Frame", cv2.WINDOW_NORMAL)
    cv2.namedWindow("2. Encrypted Data from STM32 (Status View)", cv2.WINDOW_NORMAL)
    cv2.namedWindow("3. Decrypted Frame Local", cv2.WINDOW_NORMAL)

    cv2.resizeWindow("1. Original Frame", DISPLAY_WIDTH, DISPLAY_HEIGHT)

    cv2.resizeWindow(
        "2. Encrypted Data from STM32 (Status View)",
        STATUS_WIDTH,
        STATUS_HEIGHT,
    )

    cv2.resizeWindow("3. Decrypted Frame Local", DISPLAY_WIDTH, DISPLAY_HEIGHT)

    try:
        while not state.stop:
            with state.lock:
                original_frame = (
                    None if state.latest_frame is None
                    else state.latest_frame.copy()
                )

                decrypted_frame = (
                    None if state.decrypted_frame is None
                    else state.decrypted_frame.copy()
                )

            if original_frame is not None:
                cv2.imshow(
                    "1. Original Frame",
                    resize_for_display(original_frame),
                )

            encrypted_status_img = make_encrypted_status_image(state)

            cv2.imshow(
                "2. Encrypted Data from STM32 (Status View)",
                encrypted_status_img,
            )

            if decrypted_frame is not None:
                cv2.imshow(
                    "3. Decrypted Frame Local",
                    resize_for_display(decrypted_frame),
                )

            key = cv2.waitKey(1) & 0xFF

            if key == ord("q"):
                state.stop = True
                break

            elif key == ord("+") or key == ord("="):
                state.change_jpeg_quality(JPEG_QUALITY_STEP)

            elif key == ord("-") or key == ord("_"):
                state.change_jpeg_quality(-JPEG_QUALITY_STEP)

            time.sleep(0.005)

    finally:
        state.stop = True
        cv2.destroyAllWindows()


# ============================================================
# Main live function
# ============================================================

def live_three_windows_threaded(aes: STM32AESGCM, url: str):
    """
    啟動三畫面即時顯示：
    1. Original Frame
    2. Encrypted Status Dashboard
    3. Decrypted Frame
    """

    screen = URLScreenCapture(
        url=url,
        width=DATA_WIDTH,
        height=DATA_HEIGHT,
        jpeg_quality=JPEG_QUALITY,
        grayscale=GRAYSCALE,
    )

    state = SharedFrameState()

    capture_thread = threading.Thread(
        target=capture_display_thread,
        args=(screen, state),
        daemon=True,
    )

    encrypt_thread = threading.Thread(
        target=encrypt_decrypt_thread,
        args=(aes, state, ENCRYPT_GAP),
        daemon=True,
    )

    try:
        capture_thread.start()
        encrypt_thread.start()

        encrypted_decrypted_display_loop(state)

    finally:
        state.stop = True

        capture_thread.join(timeout=1.0)
        encrypt_thread.join(timeout=1.0)

        screen.release()
        screen.close_windows()

        print()
        print("Stopped")
        print("Frame count:", state.frame_count)
        print("OK count:", state.ok_count)
        print("ERR count:", state.err_count)
        print("Last status:", state.last_status)


# ============================================================
# Program entry
# ============================================================

uart = UART3Protocol(
    port=PORT,
    baud=BAUD,
    max_buffer_size=AES_GCM_APP_MAX_SIZE,
    timeout=TIMEOUT,
)

try:
    # 1. Open UART
    uart.open()
    time.sleep(0.5)

    # Clear stale serial data on the Mac side before starting a new command
    print("清理 Mac 端殘存序列資料...")
    uart.clear_buffers()
    stale = uart.read_available(0.5)

    if stale:
        print("清掉的殘存資料: ")
        print("\t", stale.decode(errors="replace"))
    else:
        print("沒有殘存資料")

    print("\n")

    # 2. ML-KEM handshake
    kem = STM32MLKEM(uart)
    kem_result = kem_handshake_with_retry(kem, retry=3)

    # 3. Python AES key = ML-KEM shared secret
    aes_key = kem_result["aes_key"]

    # 4. AES-GCM object
    aes = STM32AESGCM(uart, aes_key)

    # 清 UART buffer
    aes.clear()

    # 5. Start live three windows
    live_three_windows_threaded(aes, URL)

except KeyboardInterrupt:
    pass

finally:
    try:
        uart.close()
    except Exception:
        pass

    print("UART closed")