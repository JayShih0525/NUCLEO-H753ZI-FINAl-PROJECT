from pqcrypto.kem.ml_kem_768 import encrypt

import command_opcodes as op
import packet_protocol as proto
from uart_transport import UARTTransport


class STM32MLKEM:
    def __init__(self, transport: UARTTransport):
        self.t = transport

        self.public_key = None
        self.kem_ciphertext = None
        self.shared_secret_python = None

    def clear(self):
        proto.send_opcode(self.t, op.CMD_CLEAR)
        proto.recv_response_ok(self.t)
        return True

    def rekey(self):
        """
        Command: CMD_KEM_REKEY
        讓 STM32 產生一組新的 ML-KEM public_key / secret_key。
        """
        proto.send_opcode(self.t, op.CMD_KEM_REKEY)
        proto.recv_response_ok(self.t)

        self.public_key = None
        self.kem_ciphertext = None
        self.shared_secret_python = None

        return True

    def get_public_key(self):
        """
        Command: CMD_KEM_GET_PUBKEY
        STM32 -> Python: RESP_OK 回應，payload = public_key
        """
        proto.send_opcode(self.t, op.CMD_KEM_GET_PUBKEY)

        public_key = proto.recv_response_ok(self.t)
        self.public_key = public_key

        return public_key

    def encapsulate_with_public_key(self, public_key: bytes):
        """
        Python 端用 STM32 public key 產生 kem_ciphertext + shared_secret_python。
        這步完全不碰 UART。
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
        Python -> STM32: kem_ciphertext packet
        STM32: decapsulate + AESGCM_SetKey(shared_secret, 32)
        STM32 -> Python: RESP_OK（空 payload）
        """
        if not isinstance(kem_ciphertext, (bytes, bytearray)):
            raise TypeError("kem_ciphertext must be bytes")

        proto.send_opcode(self.t, op.CMD_KEM_DECAPSULATE)
        proto.send_packet(self.t, bytes(kem_ciphertext))
        proto.recv_response_ok(self.t)

        return True

    def handshake(self):
        """
        1. Python 取得 STM32 public key
        2. Python encapsulate
        3. Python 把 kem_ciphertext 傳給 STM32
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
        }

    def stm32_encapsulate(self):
        """
        Command: CMD_KEM_ENCAPSULATE（選用，一般主流程用不到，見原本註解）
        """
        proto.send_opcode(self.t, op.CMD_KEM_ENCAPSULATE)
        kem_ciphertext = proto.recv_response_ok(self.t)

        return kem_ciphertext
