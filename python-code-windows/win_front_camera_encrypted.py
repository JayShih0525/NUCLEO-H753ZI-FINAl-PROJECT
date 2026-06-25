import argparse
import importlib
import time

import cv2
import numpy as np
import serial
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from pqcrypto.kem.ml_kem_768 import encrypt


UART3_OK = 0x00

MLKEM768_PUBLICKEYBYTES = 1184
MLKEM768_CIPHERTEXTBYTES = 1088
MLKEM768_SHAREDKEYBYTES = 32

DILITHIUM2_PUBLICKEYBYTES = 1312
DILITHIUM2_BYTES = 2420
DILITHIUM_VERIFY_MODULES = (
    "pqcrypto.sign.dilithium2",
    "pqcrypto.sign.ml_dsa_44",
)


def verify_dilithium_signature(public_key: bytes, message: bytes, signature: bytes):
    errors = []

    for module_name in DILITHIUM_VERIFY_MODULES:
        try:
            module = importlib.import_module(module_name)
        except Exception as exc:
            errors.append(f"{module_name}: import failed ({exc})")
            continue

        verify = getattr(module, "verify", None)
        if verify is None:
            errors.append(f"{module_name}: no verify()")
            continue

        for args in (
            (public_key, message, signature),
            (signature, message, public_key),
        ):
            try:
                result = verify(*args)
                if result is None or result is True or result == message:
                    return module_name
                errors.append(f"{module_name}: verify returned {result!r}")
            except Exception as exc:
                errors.append(f"{module_name}: verify failed ({exc})")

    raise RuntimeError("No compatible local Dilithium verifier. " + " | ".join(errors))


class UART3Protocol:
    def __init__(self, port, baud=4000000, max_buffer_size=65536, timeout=0.01):
        self.port = port
        self.baud = baud
        self.max_buffer_size = max_buffer_size
        self.timeout = timeout
        self.ser = None

    def open(self):
        self.ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        time.sleep(2)
        self.clear_buffers()

    def close(self):
        if self.ser is not None and self.ser.is_open:
            self.ser.close()

    def clear_buffers(self):
        if self.ser is not None and self.ser.is_open:
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()

    def read_exact(self, n, timeout=5.0):
        data = b""
        start = time.perf_counter()

        while len(data) < n:
            chunk = self.ser.read(n - len(data))
            if chunk:
                data += chunk
            if time.perf_counter() - start > timeout:
                break

        return data

    def write_line(self, text: str):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")
        self.ser.write((text + "\n").encode("utf-8"))
        self.ser.flush()

    def read_line(self, timeout=5.0):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")

        data = b""
        start = time.perf_counter()

        while True:
            ch = self.ser.read(1)
            if ch:
                if ch in (b"\0", b"\n", b"\r"):
                    break
                data += ch
            if time.perf_counter() - start > timeout:
                break

        return data.decode("utf-8", errors="replace")

    def send_packet(self, data: bytes):
        if len(data) > self.max_buffer_size:
            raise ValueError(f"Data too large: {len(data)} > {self.max_buffer_size}")

        self.ser.write(len(data).to_bytes(4, byteorder="big"))
        self.ser.write(data)
        self.ser.flush()

        status = self.read_exact(1, timeout=3.0)
        if len(status) != 1:
            raise TimeoutError("No status received from STM32")
        if status[0] != UART3_OK:
            raise RuntimeError(f"STM32 packet receive error: 0x{status[0]:02X}")

    def receive_packet(self):
        len_bytes = self.read_exact(4, timeout=3.0)
        if len(len_bytes) != 4:
            raise TimeoutError(f"Expected 4 length bytes, got {len(len_bytes)}")

        length = int.from_bytes(len_bytes, byteorder="big")
        if length > self.max_buffer_size:
            raise ValueError(f"Received length too large: {length}")

        data = self.read_exact(length, timeout=10.0)
        if len(data) != length:
            raise TimeoutError(f"Expected {length} bytes, got {len(data)}")

        return data


class STM32MLKEM:
    def __init__(self, uart: UART3Protocol):
        self.uart = uart

    def wait_line(self, expected: str, timeout=10.0):
        line = self.uart.read_line(timeout=timeout)
        if line != expected:
            raise RuntimeError(f"Expected {expected!r}, got {line!r}")

    def handshake(self):
        self.uart.write_line("GET_KEM_PUBLIC_KEY")
        self.wait_line("READY", timeout=10.0)
        public_key = self.uart.receive_packet()
        if len(public_key) != MLKEM768_PUBLICKEYBYTES:
            raise RuntimeError(f"ML-KEM public key length wrong: {len(public_key)}")

        kem_ciphertext, shared_secret = encrypt(public_key)
        if len(kem_ciphertext) != MLKEM768_CIPHERTEXTBYTES:
            raise RuntimeError(f"ML-KEM ciphertext length wrong: {len(kem_ciphertext)}")
        if len(shared_secret) != MLKEM768_SHAREDKEYBYTES:
            raise RuntimeError(f"ML-KEM shared secret length wrong: {len(shared_secret)}")

        self.uart.write_line("KEM_DECAPSULATE")
        self.wait_line("READY", timeout=10.0)
        self.uart.send_packet(kem_ciphertext)

        result = self.uart.read_line(timeout=20.0)
        if result != "KEM_OK":
            raise RuntimeError(f"KEM_DECAPSULATE failed: {result!r}")

        return {
            "public_key": public_key,
            "kem_ciphertext": kem_ciphertext,
            "aes_key": shared_secret,
        }


class STM32AESGCM:
    def __init__(self, uart: UART3Protocol, key: bytes):
        if len(key) != 32:
            raise ValueError("AES-256-GCM key must be 32 bytes")
        self.uart = uart
        self.local_aesgcm = AESGCM(bytes(key))

    def wait_ready(self):
        ready = self.uart.read_line(timeout=3.0)
        if ready != "READY":
            raise RuntimeError(f"Expected READY, got {ready!r}")

    def stm32_encrypt(self, plaintext: bytes):
        self.uart.write_line("ENCRYPT")
        self.wait_ready()
        self.uart.send_packet(plaintext)

        nonce = self.uart.receive_packet()
        ciphertext = self.uart.receive_packet()
        tag = self.uart.receive_packet()

        if len(nonce) != 12:
            raise RuntimeError(f"Nonce length wrong: {len(nonce)}")
        if len(tag) != 16:
            raise RuntimeError(f"Tag length wrong: {len(tag)}")

        return nonce, ciphertext, tag

    def local_decrypt(self, nonce: bytes, ciphertext: bytes, tag: bytes):
        return self.local_aesgcm.decrypt(nonce, ciphertext + tag, None)


class STM32Dilithium:
    def __init__(self, uart: UART3Protocol):
        self.uart = uart

    def get_public_key(self):
        self.uart.write_line("GET_DILITHIUM_PUBLIC_KEY")
        public_key = self.uart.receive_packet()
        if len(public_key) != DILITHIUM2_PUBLICKEYBYTES:
            raise RuntimeError(f"Dilithium public key length wrong: {len(public_key)}")
        return public_key

    def sign(self, message: bytes):
        self.uart.write_line("DILITHIUM_SIGN")
        self.uart.send_packet(message)
        signature = self.uart.receive_packet()
        if len(signature) != DILITHIUM2_BYTES:
            raise RuntimeError(f"Dilithium signature length wrong: {len(signature)}")
        return signature

    def verify_locally(self, public_key: bytes, signature: bytes, message: bytes):
        return verify_dilithium_signature(public_key, message, signature)


def open_camera(index: int, width: int, height: int, fps: int):
    cap = cv2.VideoCapture(index, cv2.CAP_DSHOW)
    if not cap.isOpened():
        raise RuntimeError(
            "Cannot open camera. Try --camera 1 or check Windows Camera Privacy Settings."
        )

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    cap.set(cv2.CAP_PROP_FPS, fps)
    return cap


def frame_to_jpg_bytes(frame, quality: int):
    ok, jpg = cv2.imencode(
        ".jpg",
        frame,
        [int(cv2.IMWRITE_JPEG_QUALITY), int(quality)],
    )
    if not ok:
        raise RuntimeError("Failed to encode frame to JPEG")
    return jpg.tobytes()


def jpg_bytes_to_frame(jpg_bytes: bytes):
    arr = np.frombuffer(jpg_bytes, dtype=np.uint8)
    frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
    if frame is None:
        raise RuntimeError("Failed to decode decrypted JPEG")
    return frame


def make_status_image(stats):
    img = np.zeros((360, 680, 3), dtype=np.uint8)
    rows = [
        "Encrypted camera flow",
        f"frames ok      : {stats['ok']}",
        f"errors         : {stats['err']}",
        f"jpeg bytes     : {stats['plain_len']}",
        f"cipher bytes   : {stats['cipher_len']}",
        f"encrypt ms     : {stats['encrypt_ms']:.1f}",
        f"status         : {stats['status']}",
        "",
        "Press q to quit",
    ]

    y = 36
    for i, text in enumerate(rows):
        scale = 0.75 if i == 0 else 0.55
        color = (120, 220, 160) if i == 0 else (230, 230, 230)
        cv2.putText(img, text, (24, y), cv2.FONT_HERSHEY_SIMPLEX, scale, color, 1, cv2.LINE_AA)
        y += 36

    return img


def clear_stm32(uart: UART3Protocol):
    uart.write_line("CLEAR")
    result = uart.read_line(timeout=3.0)
    if result != "READY":
        raise RuntimeError(f"CLEAR failed: {result!r}")


def run_camera_encryption(args):
    uart = UART3Protocol(
        port=args.port,
        baud=args.baud,
        max_buffer_size=args.max_size,
        timeout=0.01,
    )
    cap = None

    stats = {
        "ok": 0,
        "err": 0,
        "plain_len": 0,
        "cipher_len": 0,
        "encrypt_ms": 0.0,
        "status": "starting",
    }

    try:
        print(f"opening UART {args.port} @ {args.baud}")
        uart.open()
        clear_stm32(uart)

        print("ml-kem: handshake")
        kem_result = STM32MLKEM(uart).handshake()
        aes = STM32AESGCM(uart, kem_result["aes_key"])
        print(f"ml-kem: pk={len(kem_result['public_key'])} ct={len(kem_result['kem_ciphertext'])}")

        print("dilithium: session sign on stm32 + verify on laptop")
        dil = STM32Dilithium(uart)
        dil_pk = dil.get_public_key()
        session_msg = b"windows camera encrypted session"
        session_sig = dil.sign(session_msg)
        verifier = dil.verify_locally(dil_pk, session_sig, session_msg)
        print(f"dilithium: pk={len(dil_pk)} sig={len(session_sig)} laptop_verify=OK verifier={verifier}")

        print(f"opening camera index {args.camera}")
        cap = open_camera(args.camera, args.width, args.height, args.fps)
        frame_interval = 1.0 / max(args.fps, 1)
        stats["status"] = "running"

        while True:
            loop_start = time.perf_counter()
            ok, frame = cap.read()
            if not ok or frame is None:
                stats["err"] += 1
                stats["status"] = "camera read failed"
                time.sleep(0.05)
                continue

            frame = cv2.resize(frame, (args.width, args.height))
            jpg_bytes = frame_to_jpg_bytes(frame, args.quality)

            if len(jpg_bytes) > args.max_size:
                stats["err"] += 1
                stats["status"] = f"jpeg too large: {len(jpg_bytes)}"
                cv2.imshow("1. Original Camera", frame)
                cv2.imshow("2. Encrypted Status", make_status_image(stats))
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break
                continue

            try:
                enc_start = time.perf_counter()
                nonce, ciphertext, tag = aes.stm32_encrypt(jpg_bytes)
                decrypted_jpg = aes.local_decrypt(nonce, ciphertext, tag)
                decrypted_frame = jpg_bytes_to_frame(decrypted_jpg)
                stats["encrypt_ms"] = (time.perf_counter() - enc_start) * 1000.0

                if decrypted_jpg != jpg_bytes:
                    raise RuntimeError("decrypted JPEG bytes do not match plaintext")

                stats["ok"] += 1
                stats["plain_len"] = len(jpg_bytes)
                stats["cipher_len"] = len(ciphertext)
                stats["status"] = "encrypted -> decrypted OK"

                cv2.imshow("1. Original Camera", frame)
                cv2.imshow("2. Encrypted Status", make_status_image(stats))
                cv2.imshow("3. Decrypted Camera", decrypted_frame)

            except Exception as exc:
                stats["err"] += 1
                stats["status"] = str(exc)
                cv2.imshow("1. Original Camera", frame)
                cv2.imshow("2. Encrypted Status", make_status_image(stats))

            if cv2.waitKey(1) & 0xFF == ord("q"):
                break

            elapsed = time.perf_counter() - loop_start
            sleep_time = frame_interval - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    finally:
        if cap is not None:
            cap.release()
        uart.close()
        cv2.destroyAllWindows()
        print("stopped")
        print(f"frames ok={stats['ok']} errors={stats['err']} last={stats['status']}")


def main():
    parser = argparse.ArgumentParser(description="Encrypt Windows camera frames with STM32 AES-GCM.")
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--baud", type=int, default=4000000)
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--fps", type=int, default=5)
    parser.add_argument("--quality", type=int, default=50)
    parser.add_argument("--max-size", type=int, default=65536)
    args = parser.parse_args()

    run_camera_encryption(args)


if __name__ == "__main__":
    main()
