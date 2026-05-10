from pqcrypto.kem.ml_kem_768 import encrypt
from library.uart3_protocol import UART3Protocol


class STM32MLKEM:
    def __init__(self, uart: UART3Protocol):
        self.uart = uart

        self.public_key = None
        self.kem_ciphertext = None
        self.shared_secret_python = None

    def _wait_line(self, expected: str, timeout=10.0):
        line = self.uart.read_line(timeout=timeout)

        if line != expected:
            raise RuntimeError(f"Expected {expected!r}, got {line!r}")

        return line

    def clear(self):
        self.uart.clear_buffers()
        self.uart.write_line("CLEAR")
        self._wait_line("READY", timeout=3.0)
        return True

    def rekey(self):
        """
        Ask STM32 to generate a new ML-KEM public_key / secret_key pair.
        After this, Python should get a new public key again.
        """
        self.uart.write_line("KEM_REKEY")
        self._wait_line("READY", timeout=20.0)

        self.public_key = None
        self.kem_ciphertext = None
        self.shared_secret_python = None

        return True

    def get_public_key(self):
        """
        Command:
            GET_KEM_PUBLIC_KEY

        STM32 response:
            READY
            public_key packet
        """
        self.uart.write_line("GET_KEM_PUBLIC_KEY")

        self._wait_line("READY", timeout=10.0)

        public_key = self.uart.receive_packet()

        self.public_key = public_key

        return public_key

    def encapsulate_with_public_key(self, public_key: bytes):
        """
        Python uses STM32 public key to generate:

            kem_ciphertext
            shared_secret_python

        This does NOT send anything to STM32 yet.
        """
        if not isinstance(public_key, (bytes, bytearray)):
            raise TypeError("public_key must be bytes")

        kem_ciphertext, shared_secret_python = encrypt(bytes(public_key))

        self.kem_ciphertext = kem_ciphertext
        self.shared_secret_python = shared_secret_python

        return {
            "kem_ciphertext": kem_ciphertext,
            "shared_secret_python": shared_secret_python,
        }

    def send_ciphertext_to_stm32(self, kem_ciphertext: bytes):
        """
        Command:
            KEM_DECAPSULATE

        Python sends:
            kem_ciphertext packet

        STM32:
            decapsulate
            shared_secret_stm32 = decapsulate(secret_key, kem_ciphertext)
            AESGCM_SetKey(shared_secret_stm32, 32)

        STM32 response:
            KEM_OK
        """
        if not isinstance(kem_ciphertext, (bytes, bytearray)):
            raise TypeError("kem_ciphertext must be bytes")

        self.uart.write_line("KEM_DECAPSULATE")

        self._wait_line("READY", timeout=10.0)

        self.uart.send_packet(bytes(kem_ciphertext))

        result = self.uart.read_line(timeout=20.0)

        if result != "KEM_OK":
            raise RuntimeError(f"KEM_DECAPSULATE failed: {result!r}")

        return True

    def handshake(self):
        """
        Real main handshake:

            1. Python gets STM32 public key
            2. Python encapsulates
            3. Python sends kem_ciphertext to STM32
            4. STM32 sets AES-GCM key internally
            5. Python returns shared_secret_python as AES key

        Important:
            STM32 does NOT return shared_secret.
        """
        public_key = self.get_public_key()

        enc = self.encapsulate_with_public_key(public_key)

        self.send_ciphertext_to_stm32(enc["kem_ciphertext"])

        return {
            "public_key": public_key,
            "kem_ciphertext": enc["kem_ciphertext"],
            "shared_secret_python": enc["shared_secret_python"],
            "aes_key": enc["shared_secret_python"],
        }

    def stm32_encapsulate(self):
        """
        Optional command:
            KEM_ENCAPSULATE

        STM32 encapsulates with its own public key and sends ciphertext back.

        Warning:
            This is usually NOT the main real-world flow you want,
            because Python would need the matching STM32 secret key to decapsulate.
            Since STM32 secret key should never leave STM32, this is mostly not useful
            for your current design.
        """
        self.uart.write_line("KEM_ENCAPSULATE")

        self._wait_line("READY", timeout=20.0)

        kem_ciphertext = self.uart.receive_packet()

        return kem_ciphertext