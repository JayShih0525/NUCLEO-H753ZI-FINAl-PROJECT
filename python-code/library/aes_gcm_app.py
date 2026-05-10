import time
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from library.uart3_protocol import UART3Protocol


class STM32AESGCM:
    def __init__(self, uart: UART3Protocol, key: bytes):
        self.uart = uart

        if not isinstance(key, (bytes, bytearray)):
            raise TypeError("key must be bytes")

        if len(key) != 32:
            raise ValueError("AES-256-GCM key must be 32 bytes")

        self.key = bytes(key)
        self.local_aesgcm = AESGCM(self.key)

        # Default local nonce:
        # 4-byte prefix + 8-byte counter
        self.set_local_nonce(prefix=b"PYTH", counter=1)

    # ============================================================
    # Common helper
    # ============================================================

    def _wait_ready(self, timeout=2.0):
        ready = self.uart.read_line(timeout=timeout)

        if ready != "READY":
            raise RuntimeError(f"Expected READY, got {ready!r}")

    def _check_bytes(self, name: str, data: bytes):
        if not isinstance(data, (bytes, bytearray)):
            raise TypeError(f"{name} must be bytes")

        return bytes(data)

    # ============================================================
    # Local nonce management
    # nonce = 4-byte prefix + 8-byte counter
    # ============================================================

    def set_local_nonce(self, prefix: bytes = b"PYTH", counter: int = 1):
        prefix = self._check_bytes("prefix", prefix)

        if len(prefix) != 4:
            raise ValueError("prefix must be 4 bytes")

        if counter < 0 or counter >= (1 << 64):
            raise ValueError("counter must fit in 8 bytes")

        self.local_nonce_prefix = prefix
        self.local_nonce_counter = counter

    def get_local_nonce(self) -> bytes:
        return self.local_nonce_prefix + self.local_nonce_counter.to_bytes(8, "big")

    def increment_local_nonce(self):
        self.local_nonce_counter += 1

        if self.local_nonce_counter >= (1 << 64):
            raise OverflowError("local nonce counter overflow")

    # ============================================================
    # STM32 AES-GCM encrypt / decrypt / clear
    # ============================================================
    def clear(self):
        self.uart.clear_buffers()
        self.uart.write_line("CLEAR")
        self._wait_ready(3.0)
        return True

    def encrypt(self, plaintext: bytes):
        """
        Ask STM32 to encrypt plaintext.

        Python -> STM32:
            ENCRYPT command
            plaintext packet

        STM32 -> Python:
            nonce packet
            ciphertext packet
            tag packet
        """
        plaintext = self._check_bytes("plaintext", plaintext)

        if len(plaintext) > self.uart.max_buffer_size:
            raise ValueError(f"Data too large: {len(plaintext)} bytes")

        self.uart.write_line("ENCRYPT")
        self._wait_ready()

        self.uart.send_packet(plaintext)

        nonce = self.uart.receive_packet()
        ciphertext = self.uart.receive_packet()
        tag = self.uart.receive_packet()

        if len(nonce) != 12:
            raise RuntimeError(f"Nonce length wrong: {len(nonce)}")

        if len(tag) != 16:
            raise RuntimeError(f"Tag length wrong: {len(tag)}")

        if len(ciphertext) != len(plaintext):
            raise RuntimeError(
                f"Ciphertext length wrong: expected {len(plaintext)}, got {len(ciphertext)}"
            )

        return {
            "nonce": nonce,
            "ciphertext": ciphertext,
            "tag": tag,
        }

    def decrypt(self, nonce: bytes, ciphertext: bytes, tag: bytes):
        """
        Ask STM32 to decrypt.

        Python -> STM32:
            DECRYPT command
            nonce packet
            ciphertext packet
            tag packet

        STM32 -> Python:
            plaintext packet
        """
        nonce = self._check_bytes("nonce", nonce)
        ciphertext = self._check_bytes("ciphertext", ciphertext)
        tag = self._check_bytes("tag", tag)

        if len(nonce) != 12:
            raise ValueError(f"nonce must be 12 bytes, got {len(nonce)}")

        if len(tag) != 16:
            raise ValueError(f"tag must be 16 bytes, got {len(tag)}")

        if len(ciphertext) > self.uart.max_buffer_size:
            raise ValueError(f"ciphertext too large: {len(ciphertext)} bytes")

        self.uart.write_line("DECRYPT")
        self._wait_ready()

        self.uart.send_packet(nonce)
        self.uart.send_packet(ciphertext)
        self.uart.send_packet(tag)

        plaintext = self.uart.receive_packet()

        return plaintext

    # ============================================================
    # Python local AES-GCM encrypt / decrypt
    # ============================================================

    def local_encrypt(self, plaintext: bytes, nonce: bytes = None):
        """
        Encrypt locally in Python.

        If nonce is None:
            use current local nonce
            then automatically increment local nonce

        If nonce is provided:
            use that nonce
            do not auto increment
        """
        plaintext = self._check_bytes("plaintext", plaintext)

        if nonce is None:
            nonce = self.get_local_nonce()
            auto_increment = True
        else:
            nonce = self._check_bytes("nonce", nonce)
            auto_increment = False

        if len(nonce) != 12:
            raise ValueError(f"nonce must be 12 bytes, got {len(nonce)}")

        encrypted = self.local_aesgcm.encrypt(nonce, plaintext, None)

        # Python AESGCM returns: ciphertext + tag
        ciphertext = encrypted[:-16]
        tag = encrypted[-16:]

        if auto_increment:
            self.increment_local_nonce()

        return {
            "nonce": nonce,
            "ciphertext": ciphertext,
            "tag": tag,
        }

    def local_decrypt(self, nonce: bytes, ciphertext: bytes, tag: bytes):
        """
        Decrypt locally in Python.

        Important:
            decrypt does not increment nonce.
            It must use the exact nonce used during encryption.
        """
        nonce = self._check_bytes("nonce", nonce)
        ciphertext = self._check_bytes("ciphertext", ciphertext)
        tag = self._check_bytes("tag", tag)

        if len(nonce) != 12:
            raise ValueError(f"nonce must be 12 bytes, got {len(nonce)}")

        if len(tag) != 16:
            raise ValueError(f"tag must be 16 bytes, got {len(tag)}")

        encrypted = ciphertext + tag

        plaintext = self.local_aesgcm.decrypt(nonce, encrypted, None)

        return plaintext

    # ============================================================
    # Test methods
    # ============================================================

    def stm32_encrypt_stm32_decrypt_test(self, plaintext: bytes):
        """
        STM32 encrypt -> STM32 decrypt
        """
        plaintext = self._check_bytes("plaintext", plaintext)

        start = time.perf_counter()

        enc = self.encrypt(plaintext)

        decrypted = self.decrypt(
            enc["nonce"],
            enc["ciphertext"],
            enc["tag"]
        )

        end = time.perf_counter()

        return {
            "ok": decrypted == plaintext,
            "plaintext_len": len(plaintext),
            "ciphertext_len": len(enc["ciphertext"]),
            "nonce": enc["nonce"],
            "ciphertext": enc["ciphertext"],
            "tag": enc["tag"],
            "decrypted": decrypted,
            "elapsed": end - start,
        }

    def stm32_encrypt_local_decrypt_test(self, plaintext: bytes):
        """
        STM32 encrypt -> Python local decrypt

        This checks whether STM32 encryption is standard AES-GCM compatible.
        """
        plaintext = self._check_bytes("plaintext", plaintext)

        start = time.perf_counter()

        enc = self.encrypt(plaintext)

        decrypted = self.local_decrypt(
            enc["nonce"],
            enc["ciphertext"],
            enc["tag"]
        )

        end = time.perf_counter()

        return {
            "ok": decrypted == plaintext,
            "plaintext_len": len(plaintext),
            "ciphertext_len": len(enc["ciphertext"]),
            "nonce": enc["nonce"],
            "ciphertext": enc["ciphertext"],
            "tag": enc["tag"],
            "decrypted": decrypted,
            "elapsed": end - start,
        }

    def local_encrypt_stm32_decrypt_test(self, plaintext: bytes, nonce: bytes = None):
        """
        Python local encrypt -> STM32 decrypt

        If nonce is None, local nonce auto increments.
        """
        plaintext = self._check_bytes("plaintext", plaintext)

        start = time.perf_counter()

        enc = self.local_encrypt(plaintext, nonce)

        decrypted = self.decrypt(
            enc["nonce"],
            enc["ciphertext"],
            enc["tag"]
        )

        end = time.perf_counter()

        return {
            "ok": decrypted == plaintext,
            "plaintext_len": len(plaintext),
            "ciphertext_len": len(enc["ciphertext"]),
            "nonce": enc["nonce"],
            "ciphertext": enc["ciphertext"],
            "tag": enc["tag"],
            "decrypted": decrypted,
            "elapsed": end - start,
        }