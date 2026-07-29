import argparse
import importlib
import time

import serial
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from pqcrypto.kem.ml_kem_768 import encrypt


UART3_OK = 0x00
UART3_ERR_LEN_TOO_BIG = 0xE1

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


def load_dilithium_sign_module():
    errors = []

    for module_name in DILITHIUM_VERIFY_MODULES:
        try:
            module = importlib.import_module(module_name)
        except Exception as exc:
            errors.append(f"{module_name}: import failed ({exc})")
            continue

        if not hasattr(module, "generate_keypair") or not hasattr(module, "sign"):
            errors.append(f"{module_name}: missing generate_keypair() or sign()")
            continue

        return module_name, module

    raise RuntimeError("No compatible local Dilithium signer. " + " | ".join(errors))


def sign_dilithium_message(module, secret_key: bytes, message: bytes):
    sign = getattr(module, "sign")

    for args in (
        (secret_key, message),
        (message, secret_key),
    ):
        try:
            signature = sign(*args)
            if isinstance(signature, bytes) and len(signature) == DILITHIUM2_BYTES:
                return signature
        except Exception:
            pass

    raise RuntimeError("Local Dilithium sign() failed with supported argument orders")


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

    def write_bytes(self, data: bytes):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")
        self.ser.write(data)
        self.ser.flush()

    def write_line(self, text: str):
        self.write_bytes((text + "\n").encode("utf-8"))

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
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")
        if len(data) > self.max_buffer_size:
            raise ValueError(f"Data too large: {len(data)} bytes")

        self.ser.write(len(data).to_bytes(4, byteorder="big"))
        self.ser.write(data)
        self.ser.flush()

        status = self.read_exact(1, timeout=3.0)
        if len(status) != 1:
            raise TimeoutError("No status received from STM32")
        if status[0] != UART3_OK:
            raise RuntimeError(f"STM32 packet receive error: 0x{status[0]:02X}")

    def receive_packet(self):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")

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
        return line

    def get_public_key(self):
        self.uart.write_line("GET_KEM_PUBLIC_KEY")
        self.wait_line("READY", timeout=10.0)
        public_key = self.uart.receive_packet()
        if len(public_key) != MLKEM768_PUBLICKEYBYTES:
            raise RuntimeError(f"ML-KEM public key length wrong: {len(public_key)}")
        return public_key

    def handshake(self):
        public_key = self.get_public_key()
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
        self.local_nonce_prefix = b"PYTH"
        self.local_nonce_counter = 1

    def wait_ready(self, timeout=3.0):
        ready = self.uart.read_line(timeout=timeout)
        if ready != "READY":
            raise RuntimeError(f"Expected READY, got {ready!r}")

    def next_local_nonce(self):
        nonce = self.local_nonce_prefix + self.local_nonce_counter.to_bytes(8, "big")
        self.local_nonce_counter += 1
        return nonce

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

        return {"nonce": nonce, "ciphertext": ciphertext, "tag": tag}

    def stm32_decrypt(self, nonce: bytes, ciphertext: bytes, tag: bytes):
        self.uart.write_line("DECRYPT")
        self.wait_ready()
        self.uart.send_packet(nonce)
        self.uart.send_packet(ciphertext)
        self.uart.send_packet(tag)
        return self.uart.receive_packet()

    def local_encrypt(self, plaintext: bytes):
        nonce = self.next_local_nonce()
        encrypted = self.local_aesgcm.encrypt(nonce, plaintext, None)
        return {
            "nonce": nonce,
            "ciphertext": encrypted[:-16],
            "tag": encrypted[-16:],
        }

    def local_decrypt(self, nonce: bytes, ciphertext: bytes, tag: bytes):
        return self.local_aesgcm.decrypt(nonce, ciphertext + tag, None)

    def stm32_encrypt_local_decrypt_test(self, plaintext: bytes):
        enc = self.stm32_encrypt(plaintext)
        decrypted = self.local_decrypt(enc["nonce"], enc["ciphertext"], enc["tag"])
        return {"ok": decrypted == plaintext, **enc}

    def local_encrypt_stm32_decrypt_test(self, plaintext: bytes):
        enc = self.local_encrypt(plaintext)
        decrypted = self.stm32_decrypt(enc["nonce"], enc["ciphertext"], enc["tag"])
        return {"ok": decrypted == plaintext, **enc, "decrypted": decrypted}


class STM32Dilithium:
    def __init__(self, uart: UART3Protocol):
        self.uart = uart
        self.public_key = None

    def rekey(self):
        self.uart.write_line("DILITHIUM_REKEY")
        result = self.uart.read_line(timeout=30.0)
        if result != "DILITHIUM_REKEY_OK":
            raise RuntimeError(f"DILITHIUM_REKEY failed: {result!r}")
        self.public_key = None
        return True

    def get_public_key(self):
        self.uart.write_line("GET_DILITHIUM_PUBLIC_KEY")
        public_key = self.uart.receive_packet()
        if len(public_key) != DILITHIUM2_PUBLICKEYBYTES:
            raise RuntimeError(
                f"Dilithium public key length wrong: {len(public_key)}"
            )
        self.public_key = public_key
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


class LaptopDilithium:
    def __init__(self):
        self.module_name, self.module = load_dilithium_sign_module()
        self.public_key, self.secret_key = self.module.generate_keypair()

        if len(self.public_key) != DILITHIUM2_PUBLICKEYBYTES:
            raise RuntimeError(f"Laptop public key length wrong: {len(self.public_key)}")

    def sign(self, message: bytes):
        return sign_dilithium_message(self.module, self.secret_key, message)

    def authenticate_to_mcu(self, uart: UART3Protocol):
        uart.write_line("SET_LAPTOP_DILITHIUM_PUBLIC_KEY")
        ready = uart.read_line(timeout=10.0)
        if ready != "READY":
            raise RuntimeError(f"SET_LAPTOP_DILITHIUM_PUBLIC_KEY expected READY, got {ready!r}")

        uart.send_packet(self.public_key)
        result = uart.read_line(timeout=10.0)
        if result != "LAPTOP_PK_OK":
            raise RuntimeError(f"Set laptop public key failed: {result!r}")

        uart.write_line("GET_AUTH_CHALLENGE")
        challenge = uart.receive_packet()
        if len(challenge) != 32:
            raise RuntimeError(f"Auth challenge length wrong: {len(challenge)}")

        signature = self.sign(challenge)

        uart.write_line("VERIFY_LAPTOP_AUTH")
        ready = uart.read_line(timeout=10.0)
        if ready != "READY":
            raise RuntimeError(f"VERIFY_LAPTOP_AUTH expected READY, got {ready!r}")

        uart.send_packet(signature)
        result = uart.read_line(timeout=30.0)
        if result != "LAPTOP_AUTH_OK":
            raise RuntimeError(f"Laptop auth failed: {result!r}")

        return {
            "module": self.module_name,
            "public_key": self.public_key,
            "challenge": challenge,
            "signature": signature,
        }


def wait_optional_boot_line(uart: UART3Protocol):
    line = uart.read_line(timeout=1.0)
    if line:
        print(f"boot: {line}")


def main():
    parser = argparse.ArgumentParser(description="Run merged ML-KEM/AES/Dilithium UART flow.")
    parser.add_argument("--port", default="COM6", help="Serial port, for example COM6.")
    parser.add_argument("--baud", type=int, default=4000000, help="UART baud rate.")
    parser.add_argument("--size", type=int, default=1024, help="AES test plaintext size.")
    parser.add_argument(
        "--skip-clear",
        action="store_true",
        help="Do not send CLEAR after opening the serial port.",
    )
    args = parser.parse_args()

    if args.size < 1 or args.size > 65536:
        raise ValueError("--size must be between 1 and 65536 bytes")

    uart = UART3Protocol(
        port=args.port,
        baud=args.baud,
        max_buffer_size=65536,
        timeout=0.01,
    )

    try:
        print(f"opening {args.port} @ {args.baud}")
        uart.open()
        wait_optional_boot_line(uart)

        if not args.skip_clear:
            uart.write_line("CLEAR")
            clear_result = uart.read_line(timeout=3.0)
            if clear_result != "READY":
                raise RuntimeError(f"CLEAR failed: {clear_result!r}")
            print("clear: READY")

        print("laptop auth: sign on laptop + verify on stm32")
        laptop_dil = LaptopDilithium()
        laptop_auth = laptop_dil.authenticate_to_mcu(uart)
        print(
            "laptop auth: "
            f"pk={len(laptop_auth['public_key'])} "
            f"sig={len(laptop_auth['signature'])} "
            f"mcu_verify=OK signer={laptop_auth['module']}"
        )

        print("ml-kem: handshake")
        kem = STM32MLKEM(uart)
        kem_result = kem.handshake()
        aes_key = kem_result["aes_key"]
        print(f"ml-kem: pk={len(kem_result['public_key'])} ct={len(kem_result['kem_ciphertext'])}")

        print("aes-gcm: stm32 encrypt -> python decrypt")
        aes = STM32AESGCM(uart, aes_key)
        plaintext = bytes((i % 251 for i in range(args.size)))
        stm32_to_python = aes.stm32_encrypt_local_decrypt_test(plaintext)
        print(f"aes-gcm: stm32->python ok={stm32_to_python['ok']}")

        print("aes-gcm: python encrypt -> stm32 decrypt")
        python_to_stm32 = aes.local_encrypt_stm32_decrypt_test(plaintext)
        print(f"aes-gcm: python->stm32 ok={python_to_stm32['ok']}")

        if not stm32_to_python["ok"] or not python_to_stm32["ok"]:
            raise RuntimeError("AES-GCM round trip failed")

        print("dilithium: get public key")
        dil = STM32Dilithium(uart)
        public_key = dil.get_public_key()
        print(f"dilithium: pk={len(public_key)}")

        print("dilithium: sign on stm32 + verify on laptop")
        message = b"hello merged pqc flow"
        signature = dil.sign(message)
        verifier = dil.verify_locally(public_key, signature, message)
        print(f"dilithium: sig={len(signature)} laptop_verify=OK verifier={verifier}")

        print("merged flow: PASS")

    finally:
        uart.close()
        time.sleep(0.1)


if __name__ == "__main__":
    main()
