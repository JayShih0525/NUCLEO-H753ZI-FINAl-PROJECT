import time
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

import command_opcodes as op
import packet_protocol as proto
from uart_transport import UARTTransport

AES_GCM_NONCE_SIZE = 12
AES_GCM_TAG_SIZE = 16


class STM32AESGCM:
    def __init__(self, transport: UARTTransport, key: bytes):
        self.t = transport

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
    # Key rotation (e.g. after periodic ML-KEM re-handshake)
    # ============================================================

    def update_key(self, key: bytes):
        """
        換成新的 AES-256-GCM key（例如週期性重新做 ML-KEM handshake 之後）。
        只更新 Python 這端 local_aesgcm 用的 key，STM32 端的 key 要透過
        STM32MLKEM.handshake()（也就是 KEM_DECAPSULATE）另外設定。
        """
        if not isinstance(key, (bytes, bytearray)):
            raise TypeError("key must be bytes")

        if len(key) != 32:
            raise ValueError("AES-256-GCM key must be 32 bytes")

        self.key = bytes(key)
        self.local_aesgcm = AESGCM(self.key)
        self.set_local_nonce(prefix=b"PYTH", counter=1)

    # ============================================================
    # Common helper
    # ============================================================

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
        proto.send_opcode(self.t, op.CMD_CLEAR)
        proto.recv_response_ok(self.t)
        return True

    def encrypt(self, plaintext: bytes):
        """
        Command: CMD_AES_ENCRYPT
        Python -> STM32: plaintext packet
        STM32 -> Python: 一個 RESP_OK 回應，payload = nonce(12) + ciphertext(N) + tag(16)
        """
        plaintext = self._check_bytes("plaintext", plaintext)

        if len(plaintext) > self.t.max_buffer_size:
            raise ValueError(f"Data too large: {len(plaintext)} bytes")

        proto.send_opcode(self.t, op.CMD_AES_ENCRYPT)
        proto.send_packet(self.t, plaintext)

        payload = proto.recv_response_ok(self.t)

        min_len = AES_GCM_NONCE_SIZE + AES_GCM_TAG_SIZE
        if len(payload) < min_len:
            raise RuntimeError(f"Encrypt response too short: {len(payload)} bytes")

        nonce = payload[:AES_GCM_NONCE_SIZE]
        tag = payload[-AES_GCM_TAG_SIZE:]
        ciphertext = payload[AES_GCM_NONCE_SIZE:len(payload) - AES_GCM_TAG_SIZE]

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
        Command: CMD_AES_DECRYPT
        Python -> STM32: nonce packet, ciphertext packet, tag packet
        STM32 -> Python: RESP_OK 回應，payload = plaintext
        """
        nonce = self._check_bytes("nonce", nonce)
        ciphertext = self._check_bytes("ciphertext", ciphertext)
        tag = self._check_bytes("tag", tag)

        if len(nonce) != AES_GCM_NONCE_SIZE:
            raise ValueError(f"nonce must be {AES_GCM_NONCE_SIZE} bytes, got {len(nonce)}")

        if len(tag) != AES_GCM_TAG_SIZE:
            raise ValueError(f"tag must be {AES_GCM_TAG_SIZE} bytes, got {len(tag)}")

        if len(ciphertext) > self.t.max_buffer_size:
            raise ValueError(f"ciphertext too large: {len(ciphertext)} bytes")

        proto.send_opcode(self.t, op.CMD_AES_DECRYPT)
        proto.send_packet(self.t, nonce)
        proto.send_packet(self.t, ciphertext)
        proto.send_packet(self.t, tag)

        plaintext = proto.recv_response_ok(self.t)

        return plaintext

    # ============================================================
    # Python local AES-GCM encrypt / decrypt
    # ============================================================

    def local_encrypt(self, plaintext: bytes, nonce: bytes = None):
        plaintext = self._check_bytes("plaintext", plaintext)

        if nonce is None:
            nonce = self.get_local_nonce()
            auto_increment = True
        else:
            nonce = self._check_bytes("nonce", nonce)
            auto_increment = False

        if len(nonce) != AES_GCM_NONCE_SIZE:
            raise ValueError(f"nonce must be {AES_GCM_NONCE_SIZE} bytes, got {len(nonce)}")

        encrypted = self.local_aesgcm.encrypt(nonce, plaintext, None)

        ciphertext = encrypted[:-AES_GCM_TAG_SIZE]
        tag = encrypted[-AES_GCM_TAG_SIZE:]

        if auto_increment:
            self.increment_local_nonce()

        return {
            "nonce": nonce,
            "ciphertext": ciphertext,
            "tag": tag,
        }

    def local_decrypt(self, nonce: bytes, ciphertext: bytes, tag: bytes):
        nonce = self._check_bytes("nonce", nonce)
        ciphertext = self._check_bytes("ciphertext", ciphertext)
        tag = self._check_bytes("tag", tag)

        if len(nonce) != AES_GCM_NONCE_SIZE:
            raise ValueError(f"nonce must be {AES_GCM_NONCE_SIZE} bytes, got {len(nonce)}")

        if len(tag) != AES_GCM_TAG_SIZE:
            raise ValueError(f"tag must be {AES_GCM_TAG_SIZE} bytes, got {len(tag)}")

        encrypted = ciphertext + tag

        plaintext = self.local_aesgcm.decrypt(nonce, encrypted, None)

        return plaintext

    # ============================================================
    # Test methods
    # ============================================================

    def stm32_encrypt_stm32_decrypt_test(self, plaintext: bytes):
        plaintext = self._check_bytes("plaintext", plaintext)

        start = time.perf_counter()
        enc = self.encrypt(plaintext)
        decrypted = self.decrypt(enc["nonce"], enc["ciphertext"], enc["tag"])
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
        plaintext = self._check_bytes("plaintext", plaintext)

        start = time.perf_counter()
        enc = self.encrypt(plaintext)
        decrypted = self.local_decrypt(enc["nonce"], enc["ciphertext"], enc["tag"])
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
        plaintext = self._check_bytes("plaintext", plaintext)

        start = time.perf_counter()
        enc = self.local_encrypt(plaintext, nonce)
        decrypted = self.decrypt(enc["nonce"], enc["ciphertext"], enc["tag"])
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
