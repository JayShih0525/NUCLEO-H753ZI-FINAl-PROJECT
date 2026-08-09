import time
import threading
import hashlib
import cv2
import numpy as np

from uart_transport import UARTTransport
from ml_kem_app import STM32MLKEM
from aes_gcm_app import STM32AESGCM
from ml_dsa_app import STM32MLDSA
from host_identity import HostIdentity
import command_opcodes as op
from url_screen_capture import URLScreenCapture

# ============================================================
# Config
# ============================================================

# APP: Simple IP Cam
# URL = "http://172.20.10.2:8080/stream.mjpg"
URL = "http://127.0.0.1:8080/stream.mjpg"

PORT = "/dev/cu.usbmodem1103"
BAUD = 4500000
AES_GCM_APP_MAX_SIZE = 131072

# 這是「等 STM32 回應」的預設 timeout，不是輪詢間隔。
# 拿掉了舊版那個 0.01s —— 那是舊協定非阻塞輪詢用的極短值，
# 直接套在新版阻塞式 read_exact() 上會導致 STM32 還沒算完
# （例如產生 ML-DSA/ML-KEM keypair）就先 timeout。
TIMEOUT = 5.0

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

# 第二個 status dashboard 畫面大小（再放大一輪，文字也放大，
# 並且所有動態長度的文字都會自動縮放避免超出 box）
STATUS_WIDTH = 1800
STATUS_HEIGHT = 1560

# 每幾秒送一張 frame 給 STM32 加密
ENCRYPT_GAP = 0.0

# 黑白畫面
GRAYSCALE = False

# ------------------------------------------------------------
# PQC 週期性換金鑰 / 簽章設定
# ------------------------------------------------------------

REKEY_EVERY_N_FRAMES = 0
KEM_REKEY_RETRY = 3
MLDSA_SIGN_ENABLED = False
MLDSA_VERIFY_ENABLED = True
KEM_AUTH_MODE = op.KEM_AUTH_BOTH


# ============================================================
# Helper functions
# ============================================================

def resize_for_display(frame):
    return cv2.resize(frame, (DISPLAY_WIDTH, DISPLAY_HEIGHT))


def jpg_bytes_to_frame(jpg_bytes: bytes):
    arr = np.frombuffer(jpg_bytes, dtype=np.uint8)
    frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)

    if frame is None:
        raise RuntimeError("Failed to decode JPG bytes")

    return frame


def frame_to_jpg_bytes_with_quality(frame, quality: int) -> bytes:
    quality = int(max(JPEG_QUALITY_MIN, min(JPEG_QUALITY_MAX, quality)))

    ok, encoded = cv2.imencode(
        ".jpg",
        frame,
        [int(cv2.IMWRITE_JPEG_QUALITY), quality],
    )

    if not ok:
        raise RuntimeError("Failed to encode frame to JPG")

    return encoded.tobytes()


def hex_preview(data, head: int = 16, tail: int = 16) -> str:
    if data is None:
        return "None"

    h = data.hex()

    if len(data) <= head + tail:
        return h

    return f"{h[:head * 2]}...{h[-tail * 2:]} ({len(data)}B)"


def put_text(img, text, x, y, scale=0.7, thickness=2):
    cv2.putText(
        img, str(text), (x, y), cv2.FONT_HERSHEY_SIMPLEX, scale,
        (235, 235, 235), thickness, cv2.LINE_AA,
    )


def put_small_text(img, text, x, y):
    cv2.putText(
        img, str(text), (x, y), cv2.FONT_HERSHEY_SIMPLEX, 0.55,
        (200, 200, 200), 1, cv2.LINE_AA,
    )


def put_text_fit(img, text, x, y, max_width, scale=0.7, thickness=2, color=(235, 235, 235)):
    font = cv2.FONT_HERSHEY_SIMPLEX
    text = str(text)
    min_scale = 0.34

    (w, _), _ = cv2.getTextSize(text, font, scale, thickness)

    while w > max_width and scale > min_scale:
        scale -= 0.02
        (w, _), _ = cv2.getTextSize(text, font, scale, thickness)

    if w > max_width:
        while len(text) > 1 and w > max_width:
            text = text[:-1]
            (w, _), _ = cv2.getTextSize(text + "...", font, scale, thickness)
        text = text + "..."

    cv2.putText(img, text, (x, y), font, scale, color, thickness, cv2.LINE_AA)


def put_value(img, label, value, x, y, value_x, max_width, scale=0.75, thickness=2):
    put_small_text(img, label, x, y)
    put_text_fit(img, value, value_x, y, max_width, scale=scale, thickness=thickness)


def draw_panel(img, x1, y1, x2, y2, title):
    cv2.rectangle(img, (x1, y1), (x2, y2), (80, 80, 80), 1)
    cv2.rectangle(img, (x1, y1), (x2, y1 + 44), (25, 25, 25), -1)

    put_text_fit(
        img, title, x1 + 18, y1 + 30, (x2 - x1) - 36,
        scale=0.8, thickness=2, color=(255, 255, 255),
    )


def make_encrypted_status_image(state):
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

        sig_len = state.last_sig_len
        sign_elapsed = state.last_sign_elapsed
        verify_ok = state.last_verify_ok

        mldsa_public_key_len = state.mldsa_public_key_len
        mldsa_sign_total = state.mldsa_sign_total
        mldsa_verify_ok_total = state.mldsa_verify_ok_total
        mldsa_verify_fail_total = state.mldsa_verify_fail_total
        mldsa_sign_enabled = state.mldsa_sign_enabled
        mldsa_verify_enabled = state.mldsa_verify_enabled

        mlkem_public_key_len = state.mlkem_public_key_len
        mlkem_ciphertext_len = state.mlkem_ciphertext_len
        mlkem_shared_secret_len = state.mlkem_shared_secret_len

        aes_key_hex = state.aes_key_hex
        mlkem_public_key_hex = state.mlkem_public_key_hex
        mlkem_shared_secret_hex = state.mlkem_shared_secret_hex
        mldsa_public_key_hex = state.mldsa_public_key_hex
        last_signature_hex = state.last_signature_hex

        kem_rekey_count = state.kem_rekey_count
        last_kem_rekey_frame = state.last_kem_rekey_frame

        last_error_type = state.last_error_type
        last_error_plain_len = state.last_error_plain_len

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

    if REKEY_EVERY_N_FRAMES > 0:
        frames_since_rekey = frame_count - last_kem_rekey_frame
        next_rekey_in = max(0, REKEY_EVERY_N_FRAMES - frames_since_rekey)
    else:
        next_rekey_in = None

    if verify_ok is None:
        verify_str = "N/A"
    elif verify_ok:
        verify_str = "OK"
    else:
        verify_str = "FAIL"

    # ========================================================
    # Header
    # ========================================================

    put_text(
        img, "STM32 PQC Live Pipeline: ML-KEM-768 + AES-256-GCM + ML-DSA-44",
        40, 70, scale=1.05, thickness=2,
    )

    put_small_text(
        img, "Status view only - this screen does not display ciphertext as an image",
        42, 105,
    )

    # ========================================================
    # Panel 1: Runtime Status
    # ========================================================

    p1 = (40, 140, 660, 330)
    draw_panel(img, *p1, "Runtime Status")

    put_value(img, "Frame", f"{frame_count:,}", 70, 225, 230, 150)
    put_value(img, "OK", f"{ok_count:,}", 420, 225, 480, 150)
    put_value(img, "ERR", f"{err_count:,}", 70, 265, 230, 500)
    put_value(img, "Status", last_status, 70, 305, 230, p1[2] - 230 - 20)

    # ========================================================
    # Panel 2: Image Input Config
    # ========================================================

    p2 = (700, 140, 1760, 330)
    draw_panel(img, *p2, "Image Input Config")

    put_value(img, "Data size", f"{DATA_WIDTH} x {DATA_HEIGHT}", 730, 225, 900, 280)
    put_value(img, "JPEG quality", jpeg_quality, 1300, 225, 1470, 260)
    put_value(img, "Display size", f"{DISPLAY_WIDTH} x {DISPLAY_HEIGHT}", 730, 275, 900, 280)
    put_value(img, "Encrypt gap", f"{ENCRYPT_GAP:.3f}s", 1300, 275, 1470, 260)

    # ========================================================
    # Panel 3: AES-GCM Payload Size
    # ========================================================

    p3 = (40, 360, 1760, 570)
    draw_panel(img, *p3, "AES-GCM Payload Size")

    put_value(img, "Plaintext bytes", f"{plain_len:,}", 70, 445, 300, 260)
    put_value(img, "Ciphertext bytes", f"{cipher_len:,}", 70, 490, 300, 260)
    put_value(img, "Max payload bytes", f"{AES_GCM_APP_MAX_SIZE:,}", 650, 445, 940, 260)
    put_value(img, "Used", f"{used_percent:.2f}%", 650, 490, 940, 260)
    put_value(img, "Elapsed", f"{elapsed:.3f}s", 1230, 445, 1420, 300)
    put_value(img, "Rate", f"{fps_like:.2f} frame/s", 1230, 490, 1420, 300)

    bar_x = 70
    bar_y = 525
    bar_w = 1650
    bar_h = 18

    cv2.rectangle(img, (bar_x, bar_y), (bar_x + bar_w, bar_y + bar_h), (90, 90, 90), 1)
    fill_w = int(min(max(used_percent, 0.0), 100.0) / 100.0 * bar_w)
    cv2.rectangle(img, (bar_x, bar_y), (bar_x + fill_w, bar_y + bar_h), (230, 230, 230), -1)

    # ========================================================
    # Panel 4: Nonce / Tag
    # ========================================================

    p4 = (40, 600, 1760, 800)
    draw_panel(img, *p4, "AES-GCM Nonce / Tag")

    put_small_text(img, "Nonce", 70, 665)
    put_text_fit(img, nonce_hex, 70, 700, p4[2] - 70 - 30, scale=0.75, thickness=2)
    put_small_text(img, "Tag", 70, 740)
    put_text_fit(img, tag_hex, 70, 775, p4[2] - 70 - 30, scale=0.75, thickness=2)

    # ========================================================
    # Panel 5: ML-KEM (key exchange)
    # ========================================================

    p5 = (40, 830, 880, 1200)
    draw_panel(img, *p5, "ML-KEM-768 (Key Exchange)")

    kem_label_x = p5[0] + 30
    kem_value_x = p5[0] + 280
    kem_max_w = p5[2] - kem_value_x - 20

    put_value(img, "Algorithm", "ML-KEM-768", kem_label_x, 905, kem_value_x, kem_max_w)
    put_value(img, "Public key size", f"{mlkem_public_key_len:,} bytes", kem_label_x, 945, kem_value_x, kem_max_w)
    put_value(img, "Ciphertext size", f"{mlkem_ciphertext_len:,} bytes", kem_label_x, 985, kem_value_x, kem_max_w)
    put_value(img, "Shared secret size", f"{mlkem_shared_secret_len:,} bytes", kem_label_x, 1025, kem_value_x, kem_max_w)
    put_value(img, "Rekeys done", f"{kem_rekey_count:,}", kem_label_x, 1065, kem_value_x, kem_max_w)

    next_rekey_str = f"{next_rekey_in} frame(s)" if next_rekey_in is not None else "disabled"
    put_value(img, "Next rekey in", next_rekey_str, kem_label_x, 1105, kem_value_x, kem_max_w)

    _kem_auth_short = {
        op.KEM_AUTH_NONE: "no auth",
        op.KEM_AUTH_DEVICE_SIGNS: "device signs pubkey",
        op.KEM_AUTH_HOST_SIGNS: "host signs ciphertext",
        op.KEM_AUTH_BOTH: "mutual auth (both directions)",
    }.get(KEM_AUTH_MODE, str(KEM_AUTH_MODE))

    kem_desc = (
        f"Every {REKEY_EVERY_N_FRAMES} frame(s): new KEM keypair + fresh AES-256 key. "
        f"Auth: {_kem_auth_short}."
        if REKEY_EVERY_N_FRAMES > 0
        else f"Periodic rekey disabled. Auth: {_kem_auth_short}."
    )
    put_text_fit(img, kem_desc, kem_label_x, 1160, p5[2] - kem_label_x - 20, scale=0.5, thickness=1, color=(190, 190, 190))

    # ========================================================
    # Panel 6: ML-DSA-44 (digital signature)
    # ========================================================

    p6 = (920, 830, 1760, 1200)
    draw_panel(img, *p6, "ML-DSA-44 (Digital Signature)")

    mldsa_label_x = p6[0] + 30
    mldsa_value_x = p6[0] + 280
    mldsa_max_w = p6[2] - mldsa_value_x - 20

    sign_status = "ON" if mldsa_sign_enabled else "OFF"
    verify_status = "ON" if mldsa_verify_enabled else "OFF"

    put_value(img, "Algorithm", "ML-DSA-44", mldsa_label_x, 905, mldsa_value_x, mldsa_max_w)
    put_value(img, "Sign / Verify enabled", f"{sign_status} / {verify_status}", mldsa_label_x, 945, mldsa_value_x, mldsa_max_w)
    put_value(img, "Public key size", f"{mldsa_public_key_len:,} bytes", mldsa_label_x, 985, mldsa_value_x, mldsa_max_w)
    put_value(img, "Last signature size", f"{sig_len:,} bytes", mldsa_label_x, 1025, mldsa_value_x, mldsa_max_w)
    put_value(img, "Total signed", f"{mldsa_sign_total:,}", mldsa_label_x, 1065, mldsa_value_x, mldsa_max_w)
    put_value(
        img, "Verify OK : FAIL", f"{mldsa_verify_ok_total:,} : {mldsa_verify_fail_total:,}",
        mldsa_label_x, 1105, mldsa_value_x, mldsa_max_w,
    )

    dil_desc = (
        f"Last sign+verify: {sign_elapsed:.3f}s, result: {verify_str}. "
        "Signs a SHA-256 digest of nonce+ciphertext+tag, not the raw photo."
    )
    put_text_fit(img, dil_desc, mldsa_label_x, 1160, p6[2] - mldsa_label_x - 20, scale=0.5, thickness=1, color=(190, 190, 190))

    # ========================================================
    # Panel 7: Key Material (demo/education only)
    # ========================================================

    p7 = (40, 1230, 1760, 1500)
    draw_panel(img, *p7, "Key Material - DEMO/EDUCATION ONLY, never do this in production")

    col1_x = p7[0] + 30
    col2_x = p7[0] + 900
    col_max_w = 830

    put_small_text(img, "AES-256 Key (32B, from ML-KEM shared secret)", col1_x, 1305)
    put_text_fit(img, aes_key_hex, col1_x, 1335, col_max_w, scale=0.6, thickness=1)
    put_small_text(img, "ML-KEM Shared Secret (32B)", col2_x, 1305)
    put_text_fit(img, mlkem_shared_secret_hex, col2_x, 1335, col_max_w, scale=0.6, thickness=1)
    put_small_text(img, "ML-KEM Public Key (preview)", col1_x, 1375)
    put_text_fit(img, mlkem_public_key_hex, col1_x, 1405, col_max_w, scale=0.6, thickness=1)
    put_small_text(img, "ML-KEM Secret Key", col2_x, 1375)
    put_text_fit(img, "never leaves STM32 - not available on host by design", col2_x, 1405, col_max_w, scale=0.6, thickness=1)
    put_small_text(img, "ML-DSA-44 Public Key (preview)", col1_x, 1445)
    put_text_fit(img, mldsa_public_key_hex, col1_x, 1475, col_max_w, scale=0.6, thickness=1)
    put_small_text(img, "ML-DSA-44 Last Signature (preview)", col2_x, 1445)
    put_text_fit(img, last_signature_hex, col2_x, 1475, col_max_w, scale=0.6, thickness=1)

    # ========================================================
    # Panel 8: 除錯用（新增）——最近一次失敗的例外型別跟長度
    # ========================================================

    put_small_text(
        img,
        f"[debug] last error type: {last_error_type}  |  plaintext len at failure: {last_error_plain_len}",
        40,
        STATUS_HEIGHT - 45,
    )

    # ========================================================
    # Footer
    # ========================================================

    put_small_text(img, "Press + / - to change JPEG quality, q to quit", 40, STATUS_HEIGHT - 20)

    return img


def kem_handshake_with_retry(kem: STM32MLKEM, retry: int = 3, force_rekey: bool = False):
    last_error = None

    for attempt in range(retry):
        try:
            kem.clear()

            if attempt > 0 or force_rekey:
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

        self.last_sig_len = 0
        self.last_sign_elapsed = 0.0
        self.last_verify_ok = None

        self.mldsa_public_key_len = 0
        self.mldsa_sign_total = 0
        self.mldsa_verify_ok_total = 0
        self.mldsa_verify_fail_total = 0
        self.mldsa_sign_enabled = False
        self.mldsa_verify_enabled = False

        self.mlkem_public_key_len = 0
        self.mlkem_ciphertext_len = 0
        self.mlkem_shared_secret_len = 0
        self.kem_rekey_count = 0
        self.last_kem_rekey_frame = 0

        self.aes_key_hex = "None"
        self.mlkem_public_key_hex = "None"
        self.mlkem_shared_secret_hex = "None"
        self.mldsa_public_key_hex = "None"
        self.last_signature_hex = "None"

        # 除錯用（新增）：最近一次失敗的例外型別、發生失敗時的 plaintext 長度
        self.last_error_type = "None"
        self.last_error_plain_len = 0

        self.jpeg_quality = JPEG_QUALITY
        self.stop = False

    def change_jpeg_quality(self, delta: int):
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
# Thread 2: STM32 encrypt + sign + Python local decrypt
# ============================================================

def encrypt_decrypt_thread(
    aes: STM32AESGCM,
    kem: STM32MLKEM,
    mldsa: STM32MLDSA,
    state: SharedFrameState,
    gap: float,
    rekey_every_n_frames: int,
):
    last_encrypt_time = 0.0

    with state.lock:
        state.mlkem_public_key_len = len(kem.public_key) if kem.public_key else 0
        state.mlkem_ciphertext_len = len(kem.kem_ciphertext) if kem.kem_ciphertext else 0
        state.mlkem_shared_secret_len = len(kem.shared_secret_python) if kem.shared_secret_python else 0
        state.mldsa_public_key_len = len(mldsa.public_key) if mldsa.public_key else 0
        state.mldsa_sign_enabled = MLDSA_SIGN_ENABLED
        state.mldsa_verify_enabled = MLDSA_VERIFY_ENABLED

        state.aes_key_hex = hex_preview(aes.key)
        state.mlkem_public_key_hex = hex_preview(kem.public_key)
        state.mlkem_shared_secret_hex = hex_preview(kem.shared_secret_python)
        state.mldsa_public_key_hex = hex_preview(mldsa.public_key)

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

            with state.lock:
                state.frame_count += 1
                current_frame_count = state.frame_count

            # --------------------------------------------------
            # 週期性 ML-KEM rekey
            # --------------------------------------------------
            if rekey_every_n_frames > 0 and current_frame_count % rekey_every_n_frames == 0:
                try:
                    print()
                    print(f"[Frame {current_frame_count}] Rotating AES key via ML-KEM rekey...")

                    kem_result = kem_handshake_with_retry(
                        kem, retry=KEM_REKEY_RETRY, force_rekey=True
                    )

                    aes.update_key(kem_result["aes_key"])

                    with state.lock:
                        state.kem_rekey_count += 1
                        state.last_kem_rekey_frame = current_frame_count
                        state.last_status = f"ML-KEM rekeyed at frame {current_frame_count}"
                        state.mlkem_public_key_len = len(kem.public_key) if kem.public_key else 0
                        state.mlkem_ciphertext_len = len(kem.kem_ciphertext) if kem.kem_ciphertext else 0
                        state.mlkem_shared_secret_len = (
                            len(kem.shared_secret_python) if kem.shared_secret_python else 0
                        )
                        state.aes_key_hex = hex_preview(aes.key)
                        state.mlkem_public_key_hex = hex_preview(kem.public_key)
                        state.mlkem_shared_secret_hex = hex_preview(kem.shared_secret_python)

                    print(f"Rekey OK, new aes_key = {kem_result['aes_key'].hex()}")
                    print()

                except Exception as e:
                    with state.lock:
                        state.err_count += 1
                        state.last_status = f"KEM rekey error: {e}"

                    print("KEM rekey error:", e)
                    print("Continuing with previous AES key.")
                    print()

            # --------------------------------------------------
            # AES-GCM encrypt (STM32) + local decrypt 驗證
            # --------------------------------------------------
            try:
                result = aes.stm32_encrypt_local_decrypt_test(jpg_bytes)

                if not result["ok"]:
                    with state.lock:
                        state.err_count += 1
                        state.last_status = "AES-GCM mismatch"

                    last_encrypt_time = now
                    continue

                decrypted_frame = jpg_bytes_to_frame(result["decrypted"])

                sig_len = 0
                sign_elapsed = 0.0
                verify_ok = None

                if MLDSA_SIGN_ENABLED:
                    try:
                        sign_start = time.perf_counter()

                        digest = hashlib.sha256(
                            result["nonce"] + result["ciphertext"] + result["tag"]
                        ).digest()

                        signature = mldsa.sign(digest)
                        sig_len = len(signature)
                        if MLDSA_VERIFY_ENABLED:
                            verify_ok = mldsa.verify(signature, digest)

                        sign_elapsed = time.perf_counter() - sign_start

                        with state.lock:
                            state.mldsa_sign_total += 1
                            state.last_signature_hex = hex_preview(signature)

                            if MLDSA_VERIFY_ENABLED:
                                if verify_ok:
                                    state.mldsa_verify_ok_total += 1
                                else:
                                    state.mldsa_verify_fail_total += 1
                                    state.err_count += 1
                                    state.last_status = "ML-DSA verify FAILED"

                    except Exception as e:
                        with state.lock:
                            state.err_count += 1
                            state.last_status = f"ML-DSA error: {e}"

                        print("ML-DSA sign/verify error:", e)

                with state.lock:
                    state.decrypted_frame = decrypted_frame
                    state.ok_count += 1
                    state.last_nonce = result["nonce"]
                    state.last_tag = result["tag"]
                    state.last_plain_len = result["plaintext_len"]
                    state.last_cipher_len = result["ciphertext_len"]
                    state.last_elapsed = result["elapsed"]
                    state.last_sig_len = sig_len
                    state.last_sign_elapsed = sign_elapsed
                    state.last_verify_ok = verify_ok

                    if state.last_status not in ("ML-DSA verify FAILED",):
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
                    f"plain={plain_len} | "
                    f"cipher={cipher_len} | "
                    f"elapsed={elapsed:.3f}s | "
                    f"sig={sig_len}B | "
                    f"verify={verify_ok} | "
                    f"nonce={nonce_hex} | "
                    f"tag={tag_hex}",
                    end="\r",
                    flush=True,
                )

            except Exception as e:
                # ------------------------------------------------------
                # 除錯用改動（新增）：印出例外的「型別」跟「發生失敗時的
                # plaintext 長度 / 長度對 4 取餘數」。之前的版本只印
                # str(e)，遇到 cryptography 的 InvalidTag 例外時，
                # str(e) 會是空字串，看起來像「Last status: ...error: 」
                # 後面完全空白，等於什麼線索都沒有。
                #
                # 加上 type(e).__name__，才能一眼看出是不是 InvalidTag
                # （代表 STM32 硬體算出來的密文/tag 本身就錯了，屬於
                # 加密這一步出錯，不是 UART 傳輸的問題）。
                #
                # 加上 jpg_len % 4，是為了檢查失敗的 frame 長度是不是
                # 集中在某個餘數（例如全部集中在餘 1、餘 2、或餘 3），
                # 這樣才能判斷是不是 CRYP 硬體處理「非 4 對齊長度的
                # 最後一段資料」時，在大量資料/特定長度餘數下才會出的
                # 邊界問題（我們之前的已知答案測試只測過 33 bytes，
                # 覆蓋率不夠，這個統計能幫忙補上這塊）。
                # ------------------------------------------------------
                error_type_name = type(e).__name__
                jpg_len = len(jpg_bytes) if jpg_bytes is not None else -1

                with state.lock:
                    state.err_count += 1
                    state.last_status = f"encrypt/decrypt error: {error_type_name}: {e}"
                    state.last_error_type = error_type_name
                    state.last_error_plain_len = jpg_len

                print()
                print(
                    f"Encrypt/decrypt error: type={error_type_name} "
                    f"msg={e!r} plaintext_len={jpg_len} "
                    f"plaintext_len_mod4={jpg_len % 4}"
                )

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
    cv2.namedWindow("1. Original Frame", cv2.WINDOW_NORMAL)
    cv2.namedWindow("2. Encrypted Data from STM32 (Status View)", cv2.WINDOW_NORMAL)
    cv2.namedWindow("3. Decrypted Frame Local", cv2.WINDOW_NORMAL)

    cv2.resizeWindow("1. Original Frame", DISPLAY_WIDTH, DISPLAY_HEIGHT)
    cv2.resizeWindow("2. Encrypted Data from STM32 (Status View)", STATUS_WIDTH, STATUS_HEIGHT)
    cv2.resizeWindow("3. Decrypted Frame Local", DISPLAY_WIDTH, DISPLAY_HEIGHT)

    try:
        while not state.stop:
            with state.lock:
                original_frame = (
                    None if state.latest_frame is None else state.latest_frame.copy()
                )
                decrypted_frame = (
                    None if state.decrypted_frame is None else state.decrypted_frame.copy()
                )

            if original_frame is not None:
                cv2.imshow("1. Original Frame", resize_for_display(original_frame))

            encrypted_status_img = make_encrypted_status_image(state)
            cv2.imshow("2. Encrypted Data from STM32 (Status View)", encrypted_status_img)

            if decrypted_frame is not None:
                cv2.imshow("3. Decrypted Frame Local", resize_for_display(decrypted_frame))

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

def live_three_windows_threaded(aes: STM32AESGCM, kem: STM32MLKEM, mldsa: STM32MLDSA, url: str):
    screen = URLScreenCapture(
        url=url, width=DATA_WIDTH, height=DATA_HEIGHT,
        jpeg_quality=JPEG_QUALITY, grayscale=GRAYSCALE,
    )

    state = SharedFrameState()

    capture_thread = threading.Thread(target=capture_display_thread, args=(screen, state), daemon=True)
    encrypt_thread = threading.Thread(
        target=encrypt_decrypt_thread,
        args=(aes, kem, mldsa, state, ENCRYPT_GAP, REKEY_EVERY_N_FRAMES),
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
        print("KEM rekey count:", state.kem_rekey_count)
        print("Last status:", state.last_status)


# ============================================================
# Program entry
# ============================================================

uart = UARTTransport(
    port=PORT,
    baud=BAUD,
    max_buffer_size=AES_GCM_APP_MAX_SIZE,
    timeout=TIMEOUT,
)

try:
    uart.open()
    time.sleep(0.5)

    print("清理 Mac 端殘存序列資料...")
    uart.clear_buffers()
    stale = uart.read_available(0.5)

    if stale:
        print("清掉的殘存資料 (raw bytes): ")
        print("\t", stale)
    else:
        print("沒有殘存資料")

    print("等待 STM32 開機 banner...")
    boot_line = uart.read_boot_banner("CRYPTO_APP_READY", timeout=5.0)
    print("STM32 boot OK:", boot_line)
    print("\n")

    mldsa = STM32MLDSA(uart)
    mldsa.clear()
    mldsa.rekey()
    mldsa_pk = mldsa.get_public_key()
    print("ML-DSA public_key len:", len(mldsa_pk))
    print("\n")

    kem = STM32MLKEM(uart)

    host_identity = None
    if KEM_AUTH_MODE in (op.KEM_AUTH_HOST_SIGNS, op.KEM_AUTH_BOTH):
        print("產生 host 自己的 ML-DSA-44 keypair（用來對 kem_ciphertext 簽章）...")
        host_identity = HostIdentity()
        print("host public_key len:", len(host_identity.public_key))

    auth_mode_names = {
        op.KEM_AUTH_NONE: "NONE（不驗證）",
        op.KEM_AUTH_DEVICE_SIGNS: "DEVICE_SIGNS（驗證 STM32 簽的 public key）",
        op.KEM_AUTH_HOST_SIGNS: "HOST_SIGNS（STM32 驗證 host 簽的 ciphertext）",
        op.KEM_AUTH_BOTH: "BOTH（雙向都驗證）",
    }
    print(f"ML-KEM handshake 認證模式: {auth_mode_names.get(KEM_AUTH_MODE, KEM_AUTH_MODE)}")

    kem.set_auth_mode(
        KEM_AUTH_MODE,
        device_mldsa_pubkey=mldsa_pk,
        host_identity=host_identity,
    )

    kem_result = kem_handshake_with_retry(kem, retry=3)

    if kem_result["device_sig_verified"] is not None:
        print("STM32 對 ML-KEM public key 的簽章驗證:",
              "通過" if kem_result["device_sig_verified"] else "失敗")
    print("\n")

    aes_key = kem_result["aes_key"]
    aes = STM32AESGCM(uart, aes_key)
    aes.clear()

    print(f"每 {REKEY_EVERY_N_FRAMES if REKEY_EVERY_N_FRAMES > 0 else '(不自動)'} "
          f"張照片後會重新做一次 ML-KEM handshake 換 AES key")
    print(f"ML-DSA 簽章: {'開啟' if MLDSA_SIGN_ENABLED else '關閉'}")
    print(f"ML-DSA 驗章: {'開啟' if MLDSA_VERIFY_ENABLED else '關閉'}")
    print("\n")

    live_three_windows_threaded(aes, kem, mldsa, URL)

except KeyboardInterrupt:
    pass

finally:
    try:
        uart.close()
    except Exception:
        pass

    print("UART closed")