import time
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

import command_opcodes as op
import packet_protocol as proto
from uart_transport import UARTTransport

AES_GCM_NONCE_SIZE = 12
AES_GCM_TAG_SIZE = 16


def _pad_to_word(data: bytes) -> bytes:
    """
    補零到 4 的倍數。

    背景：STM32 的 CRYP 硬體加速在處理「長度不是 4 的倍數」的大量資料
    （幾十 KB 等級）時不可靠——實測 200+ 張照片，長度是 4 的倍數的
    100% 成功，有餘數的幾乎必然失敗（InvalidTag）。已知答案測試只測過
    33 bytes 這種小資料，沒有覆蓋到這個規模才會出現的問題。

    修法：與其繼續猜韌體暫存器設定，直接讓長度永遠對齊——這裡送去
    STM32 加密前先補零到 4 的倍數，STM32 端也跟著改成只接受對齊長度
    的 WORD 模式（不再用「允許任意長度」的 BYTE 模式）。

    這個 padding 完全透明，不需要額外的協定欄位去記錄「原始長度是
    多少」：
      - GCM 是 stream cipher 性質，補的 0~3 個零 byte 只是在密文尾巴
        多出等長的幾個 byte，不影響前面已經算好的部分。
      - 我們實際傳輸的是 JPEG 圖片，JPEG 格式本身有結束符號（EOI
        marker），解碼器（cv2.imdecode 等）本來就會忽略結尾多餘的
        padding bytes，不需要額外去掉。
    """
    pad_len = (-len(data)) % 4
    if pad_len:
        data = data + b"\x00" * pad_len
    return data


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
        Python -> STM32: plaintext packet（已補零到 4 的倍數）
        STM32 -> Python: 一個 RESP_OK 回應，
                         payload = nonce(12) + ciphertext(N，已對齊) + tag(16)

        回傳的 ciphertext 長度是「補零後」的長度，不是呼叫端傳進來的
        原始長度（最多多 3 bytes）。這是刻意的——ciphertext 的 padding
        tail 是密碼學上跟 tag 綁定的實際內容，不能事後隨便截斷，
        全程都要保持一致（呼叫端如果要顯示原始檔案大小，用自己手上的
        原始 plaintext 長度即可，不用去猜 ciphertext 該截到哪）。
        """
        plaintext = self._check_bytes("plaintext", plaintext)
        plaintext = _pad_to_word(plaintext)

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
        STM32 -> Python: RESP_OK 回應，payload = plaintext（對齊長度）

        呼叫端要傳進「跟 encrypt() 回傳的 ciphertext 一樣長」的資料
        （也就是已經對齊過的），不要自己截斷或重新 padding——ciphertext
        的每個 byte 都跟 tag 綁定，事後改長度只會讓 tag 驗證失敗。
        """
        nonce = self._check_bytes("nonce", nonce)
        ciphertext = self._check_bytes("ciphertext", ciphertext)
        tag = self._check_bytes("tag", tag)

        if len(nonce) != AES_GCM_NONCE_SIZE:
            raise ValueError(f"nonce must be {AES_GCM_NONCE_SIZE} bytes, got {len(nonce)}")

        if len(tag) != AES_GCM_TAG_SIZE:
            raise ValueError(f"tag must be {AES_GCM_TAG_SIZE} bytes, got {len(tag)}")

        if len(ciphertext) % 4 != 0:
            raise ValueError(
                f"ciphertext length must be a multiple of 4 (STM32 WORD mode), got {len(ciphertext)}"
            )

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
        """
        跟 STM32 版 encrypt() 一樣，也會先補零到 4 的倍數——這是為了讓
        local_encrypt_stm32_decrypt_test（Python 本地加密、STM32 硬體
        解密）這種反方向測試，餵給 STM32 的 ciphertext 一樣是對齊過的
        長度，不會又踩到硬體那個非對齊長度的問題。
        """
        plaintext = self._check_bytes("plaintext", plaintext)
        plaintext = _pad_to_word(plaintext)

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
    #
    # 注意：因為 encrypt()/local_encrypt() 內部會先 padding，回傳的
    # decrypted 長度可能比呼叫端傳進來的原始 plaintext 多 0~3 bytes
    # （補的零）。這裡的 "ok" 判斷改成只比對「原始長度那一段」，忽略
    # 補在最後面的 padding bytes——這樣才不會因為長度多了幾個零 byte，
    # 就把「其實加解密完全正確」的結果誤判成失敗。
    # ============================================================

    def stm32_encrypt_stm32_decrypt_test(self, plaintext: bytes):
        plaintext = self._check_bytes("plaintext", plaintext)

        start = time.perf_counter()
        enc = self.encrypt(plaintext)
        decrypted = self.decrypt(enc["nonce"], enc["ciphertext"], enc["tag"])
        end = time.perf_counter()

        return {
            "ok": decrypted[:len(plaintext)] == plaintext,
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
            "ok": decrypted[:len(plaintext)] == plaintext,
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
            "ok": decrypted[:len(plaintext)] == plaintext,
            "plaintext_len": len(plaintext),
            "ciphertext_len": len(enc["ciphertext"]),
            "nonce": enc["nonce"],
            "ciphertext": enc["ciphertext"],
            "tag": enc["tag"],
            "decrypted": decrypted,
            "elapsed": end - start,
        }