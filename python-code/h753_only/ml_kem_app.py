from pqcrypto.kem.ml_kem_768 import encrypt

import command_opcodes as op
import packet_protocol as proto
import host_identity as hostid
from uart_transport import UARTTransport

MLKEM_PUBLIC_KEY_SIZE = 1184
MLDSA_SIGNATURE_SIZE = 2420


class STM32MLKEM:
    def __init__(self, transport: UARTTransport):
        self.t = transport

        self.public_key = None
        self.kem_ciphertext = None
        self.shared_secret_python = None

        # 雙向認證設定（預設關閉，行為完全跟原本一樣）。
        # 用 set_auth_mode() 設定，不要直接改這幾個屬性。
        self.auth_mode = op.KEM_AUTH_NONE
        self.device_mldsa_pubkey = None  # 驗 STM32 對 KEM public key 的簽章用
        self.host_identity = None            # HostIdentity，HOST_SIGNS 模式才需要

        # 最近一次 get_public_key() 的簽章驗證結果：
        # None = 這個模式下沒有簽章要驗；True/False = 驗證結果
        self.last_device_sig_verified = None

    def clear(self):
        proto.send_opcode(self.t, op.CMD_CLEAR)
        proto.recv_response_ok(self.t)
        return True

    def rekey(self):
        """
        Command: CMD_KEM_REKEY
        只重新產生 STM32 的 ML-KEM keypair，不影響 auth_mode 設定
        （那是韌體另一個獨立的狀態，不會因為 rekey 被重置）。
        """
        proto.send_opcode(self.t, op.CMD_KEM_REKEY)
        proto.recv_response_ok(self.t)

        self.public_key = None
        self.kem_ciphertext = None
        self.shared_secret_python = None

        return True

    # ============================================================
    # 雙向簽章認證設定
    # ============================================================

    def set_auth_mode(self, mode: int, device_mldsa_pubkey: bytes = None,
                       host_identity: "hostid.HostIdentity" = None):
        """
        設定這次 session 的 ML-KEM handshake 要不要做簽章認證、哪個方向。
        會透過 CMD_KEM_SET_AUTH_MODE 同步設定給 STM32。

        mode: op.KEM_AUTH_NONE / _DEVICE_SIGNS / _HOST_SIGNS / _BOTH

        device_mldsa_pubkey:
            DEVICE_SIGNS / BOTH 模式需要。STM32 的 ML-DSA public key，
            從 STM32MLDSA.get_public_key() 拿，用來在本地驗證 STM32
            對 ML-KEM public key 的簽章。

        host_identity:
            HOST_SIGNS / BOTH 模式需要。Python 自己的 HostIdentity，
            用來簽 kem_ciphertext，並把 public key 交給 STM32 信任
            （CMD_SET_HOST_MLDSA_PUBKEY，TOFU）。
        """
        valid_modes = (op.KEM_AUTH_NONE, op.KEM_AUTH_DEVICE_SIGNS,
                       op.KEM_AUTH_HOST_SIGNS, op.KEM_AUTH_BOTH)

        if mode not in valid_modes:
            raise ValueError(f"Invalid auth mode: {mode}")

        needs_device_key = mode in (op.KEM_AUTH_DEVICE_SIGNS, op.KEM_AUTH_BOTH)
        needs_host_identity = mode in (op.KEM_AUTH_HOST_SIGNS, op.KEM_AUTH_BOTH)

        if needs_device_key and device_mldsa_pubkey is None:
            raise ValueError("device_mldsa_pubkey is required for this auth mode")

        if needs_host_identity and host_identity is None:
            raise ValueError("host_identity is required for this auth mode")

        self.auth_mode = mode
        self.device_mldsa_pubkey = device_mldsa_pubkey
        self.host_identity = host_identity

        # 告訴 STM32 這次要用哪個模式
        proto.send_opcode(self.t, op.CMD_KEM_SET_AUTH_MODE)
        proto.send_packet(self.t, bytes([mode]))
        proto.recv_response_ok(self.t)

        # HOST_SIGNS / BOTH：先把 host public key 交給 STM32 信任
        if needs_host_identity:
            proto.send_opcode(self.t, op.CMD_SET_HOST_MLDSA_PUBKEY)
            proto.send_packet(self.t, host_identity.public_key)
            proto.recv_response_ok(self.t)

        return True

    # ============================================================
    # Handshake
    # ============================================================

    def get_public_key(self):
        """
        Command: CMD_KEM_GET_PUBKEY

        NONE / HOST_SIGNS 模式：回應 payload 就是 public_key。

        DEVICE_SIGNS / BOTH 模式：回應 payload = public_key(1184B) +
        STM32 對它的簽章(2420B)。這裡會立刻在本地驗證（純 Python 算，
        不用再問 STM32 一次），驗證失敗直接丟例外 —— 一把沒通過簽章
        驗證的 public key 不該被拿去 encapsulate。
        """
        proto.send_opcode(self.t, op.CMD_KEM_GET_PUBKEY)
        payload = proto.recv_response_ok(self.t)

        expect_device_sig = self.auth_mode in (op.KEM_AUTH_DEVICE_SIGNS, op.KEM_AUTH_BOTH)

        if not expect_device_sig:
            public_key = payload
            self.last_device_sig_verified = None
        else:
            expected_len = MLKEM_PUBLIC_KEY_SIZE + MLDSA_SIGNATURE_SIZE

            if len(payload) != expected_len:
                raise RuntimeError(
                    f"Unexpected GET_PUBKEY payload length: "
                    f"{len(payload)} (expected {expected_len})"
                )

            public_key = payload[:MLKEM_PUBLIC_KEY_SIZE]
            device_sig = payload[MLKEM_PUBLIC_KEY_SIZE:]

            if self.device_mldsa_pubkey is None:
                raise RuntimeError(
                    "auth_mode requires device_mldsa_pubkey but none was set "
                    "(call set_auth_mode() first)"
                )

            verified = hostid.verify_signature(
                self.device_mldsa_pubkey, public_key, device_sig
            )
            self.last_device_sig_verified = verified

            if not verified:
                raise RuntimeError(
                    "ML-KEM public key signature verification FAILED "
                    "(possible tampering, or wrong device public key) - "
                    "refusing to use this key"
                )

        self.public_key = public_key
        return public_key

    def encapsulate_with_public_key(self, public_key: bytes):
        """
        Python 用 STM32 public key 產生 kem_ciphertext + shared_secret_python。
        純本地計算，不碰 UART。
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
        Command: CMD_KEM_DECAPSULATE

        HOST_SIGNS / BOTH 模式：送完 kem_ciphertext packet 之後，
        多送一包 Python 對這個 ciphertext 的簽章。STM32 驗證通過才會
        繼續 decapsulate + 設定 AES key；驗證失敗會回 RESP_VERIFY_FAIL，
        這裡直接丟例外，呼叫端要當成 handshake 失敗處理。
        """
        if not isinstance(kem_ciphertext, (bytes, bytearray)):
            raise TypeError("kem_ciphertext must be bytes")

        need_host_sig = self.auth_mode in (op.KEM_AUTH_HOST_SIGNS, op.KEM_AUTH_BOTH)

        if need_host_sig and self.host_identity is None:
            raise RuntimeError(
                "auth_mode requires host_identity but none was set "
                "(call set_auth_mode() first)"
            )

        proto.send_opcode(self.t, op.CMD_KEM_DECAPSULATE)
        proto.send_packet(self.t, bytes(kem_ciphertext))

        if need_host_sig:
            signature = self.host_identity.sign(bytes(kem_ciphertext))
            proto.send_packet(self.t, signature)

        status, payload = proto.recv_response(self.t)

        if status == proto.RESP_VERIFY_FAIL:
            raise RuntimeError(
                "STM32 rejected kem_ciphertext: host signature verification FAILED"
            )

        if status != proto.RESP_OK:
            raise proto.ProtocolError(status, payload.decode(errors="replace"))

        return True

    def handshake(self):
        """
        1. Python 取得 STM32 public key（可能附簽章，視 auth_mode 而定）
        2. Python encapsulate
        3. Python 把 kem_ciphertext 傳給 STM32（可能附簽章）
        4. STM32 內部設定 AES-GCM key（不會回傳 shared_secret）
        5. Python 回傳 shared_secret_python 當 AES key
        """
        public_key = self.get_public_key()
        enc = self.encapsulate_with_public_key(public_key)
        self.send_ciphertext_to_stm32(enc["kem_ciphertext"])

        return {
            "public_key": public_key,
            "kem_ciphertext": enc["kem_ciphertext"],
            "shared_secret_python": enc["shared_secret_python"],
            "aes_key": enc["shared_secret_python"],
            "device_sig_verified": self.last_device_sig_verified,
        }

    def stm32_encapsulate(self):
        """
        Command: CMD_KEM_ENCAPSULATE（選用，一般主流程用不到）
        """
        proto.send_opcode(self.t, op.CMD_KEM_ENCAPSULATE)
        kem_ciphertext = proto.recv_response_ok(self.t)

        return kem_ciphertext
