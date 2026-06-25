import argparse
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PYTHON_CODE = ROOT / "python-code-windows"
sys.path.insert(0, str(PYTHON_CODE))

from library.aes_gcm_app import STM32AESGCM
from library.ml_kem_app import STM32MLKEM
from library.uart3_protocol import UART3Protocol


DILITHIUM2_PUBLICKEYBYTES = 1312
DILITHIUM2_BYTES = 2420


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

    def verify_on_stm32(self, signature: bytes, message: bytes):
        self.uart.write_line("DILITHIUM_VERIFY")
        self.uart.send_packet(signature)
        self.uart.send_packet(message)
        result = self.uart.read_line(timeout=30.0)
        if result != "VERIFY_OK":
            raise RuntimeError(f"DILITHIUM_VERIFY failed: {result!r}")
        return True


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

        print("dilithium: sign + verify on stm32")
        message = b"hello merged pqc flow"
        signature = dil.sign(message)
        dil.verify_on_stm32(signature, message)
        print(f"dilithium: sig={len(signature)} verify=OK")

        print("merged flow: PASS")

    finally:
        uart.close()
        time.sleep(0.1)


if __name__ == "__main__":
    main()
