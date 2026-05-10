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

PORT = "/dev/cu.usbmodem1103"
BAUD = 4000000
MAX_BUFFER_SIZE = 65535
TIMEOUT = 0.01

# 這是實際送去 STM32 加密的圖片大小
DATA_WIDTH = 640
DATA_HEIGHT = 480
# JPEG_QUALITY = 80, almost will not have error
JPEG_QUALITY = 10

# 這只是顯示視窗大小，不影響加密資料大小
DISPLAY_WIDTH = 640
DISPLAY_HEIGHT = 480

# 每幾秒送一張 frame 給 STM32 加密
ENCRYPT_GAP = 0.0


# ============================================================
# Helper functions
# ============================================================

def resize_for_display(frame):
    """
    只改變顯示大小，不改變實際加密資料大小。
    """
    return cv2.resize(frame, (DISPLAY_WIDTH, DISPLAY_HEIGHT))


def bytes_to_noise_image(data: bytes, width: int, height: int):
    """
    把 ciphertext bytes 轉成灰階雜訊圖。
    注意：ciphertext 本身不是圖片，這只是視覺化加密資料。
    """
    need = width * height

    arr = np.frombuffer(data, dtype=np.uint8)

    if len(arr) < need:
        arr = np.pad(arr, (0, need - len(arr)), mode="constant")
    else:
        arr = arr[:need]

    return arr.reshape((height, width))


def jpg_bytes_to_frame(jpg_bytes: bytes):
    """
    把 JPG bytes 解碼回 OpenCV frame。
    """
    arr = np.frombuffer(jpg_bytes, dtype=np.uint8)
    frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)

    if frame is None:
        raise RuntimeError("Failed to decode JPG bytes")

    return frame


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

        self.encrypted_img = None
        self.decrypted_frame = None

        self.frame_count = 0
        self.ok_count = 0
        self.err_count = 0

        self.last_nonce = None
        self.last_tag = None

        self.last_status = "INIT"
        self.stop = False


# ============================================================
# Thread 1: capture camera + show original frame
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
                jpg_bytes = screen.frame_to_jpg_bytes(frame)
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
    4. 更新 encrypted noise image 和 decrypted frame

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

            if len(jpg_bytes) > MAX_BUFFER_SIZE:
                with state.lock:
                    state.err_count += 1
                    state.last_status = (
                        f"JPG too large: {len(jpg_bytes)} > {MAX_BUFFER_SIZE}"
                    )
                    err_count = state.err_count
                    frame_count = state.frame_count

                print()
                print("JPG TOO LARGE")
                print(f"Frame        : {frame_count}")
                print(f"JPG size     : {len(jpg_bytes)} bytes")
                print(f"Max size     : {MAX_BUFFER_SIZE} bytes")
                print(f"DATA_WIDTH   : {DATA_WIDTH}")
                print(f"DATA_HEIGHT  : {DATA_HEIGHT}")
                print(f"JPEG_QUALITY : {JPEG_QUALITY}")
                print(f"ERR count    : {err_count}")
                print("Suggestion   : reduce DATA_WIDTH / DATA_HEIGHT / JPEG_QUALITY")
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

                encrypted_img = bytes_to_noise_image(
                    result["ciphertext"],
                    width=DATA_WIDTH,
                    height=DATA_HEIGHT,
                )

                decrypted_frame = jpg_bytes_to_frame(result["decrypted"])

                with state.lock:
                    state.encrypted_img = encrypted_img
                    state.decrypted_frame = decrypted_frame
                    state.ok_count += 1

                    state.last_nonce = result["nonce"]
                    state.last_tag = result["tag"]

                    state.last_status = (
                        f"OK | plain={result['plaintext_len']} | "
                        f"cipher={result['ciphertext_len']} | "
                        f"elapsed={result['elapsed']:.3f}s"
                    )

                    ok_count = state.ok_count
                    err_count = state.err_count
                    last_status = state.last_status
                    nonce_hex = result["nonce"].hex()
                    tag_hex = result["tag"].hex()

                    print(
                        f"Frame={current_frame_count} | "
                        f"OK={ok_count} | ERR={err_count} | "
                        f"{last_status} | "
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
# Main thread: show encrypted + decrypted windows
# ============================================================

def encrypted_decrypted_display_loop(state: SharedFrameState):
    """
    Main thread 顯示三個視窗：
    1. Original Frame
    2. Encrypted noise image
    3. Decrypted frame
    """

    cv2.namedWindow("1. Original Frame", cv2.WINDOW_NORMAL)
    cv2.namedWindow("2. Encrypted Data from STM32 (Noise View)", cv2.WINDOW_NORMAL)
    cv2.namedWindow("3. Decrypted Frame Local", cv2.WINDOW_NORMAL)

    cv2.resizeWindow("1. Original Frame", DISPLAY_WIDTH, DISPLAY_HEIGHT)
    cv2.resizeWindow("2. Encrypted Data from STM32 (Noise View)", DISPLAY_WIDTH, DISPLAY_HEIGHT)
    cv2.resizeWindow("3. Decrypted Frame Local", DISPLAY_WIDTH, DISPLAY_HEIGHT)

    try:
        while not state.stop:
            with state.lock:
                original_frame = (
                    None if state.latest_frame is None
                    else state.latest_frame.copy()
                )

                encrypted_img = (
                    None if state.encrypted_img is None
                    else state.encrypted_img.copy()
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

            if encrypted_img is not None:
                cv2.imshow(
                    "2. Encrypted Data from STM32 (Noise View)",
                    resize_for_display(encrypted_img),
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
    2. Encrypted Noise View
    3. Decrypted Frame
    """

    screen = URLScreenCapture(
        url=url,
        width=DATA_WIDTH,
        height=DATA_HEIGHT,
        jpeg_quality=JPEG_QUALITY,
        grayscale=False,
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
    max_buffer_size=MAX_BUFFER_SIZE,
    timeout=TIMEOUT,
)

try:
    # 1. Open UART
    uart.open()
    time.sleep(0.5)

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

finally:
    try:
        uart.close()
    except Exception:
        pass

    print("UART closed")