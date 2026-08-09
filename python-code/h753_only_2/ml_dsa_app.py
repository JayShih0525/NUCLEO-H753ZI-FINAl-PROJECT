import command_opcodes as op
import packet_protocol as proto
from uart_transport import UARTTransport

# 底層是 ML-DSA-44（FIPS 204 最終標準），不是原版 CRYSTALS-Dilithium
# round 3 —— 兩者 key/簽章大小剛好一樣，但不是同一份規格、不能互相驗證，
# 命名對齊實際演算法版本。
MLDSA44_PUBLICKEYBYTES = 1312
MLDSA44_SIGBYTES = 2420


class STM32MLDSA:
    def __init__(self, transport: UARTTransport):
        self.t = transport
        self.public_key = None

    def clear(self):
        proto.send_opcode(self.t, op.CMD_CLEAR)
        proto.recv_response_ok(self.t)
        return True

    def rekey(self):
        proto.send_opcode(self.t, op.CMD_MLDSA_REKEY)
        proto.recv_response_ok(self.t)

        self.public_key = None
        return True

    def get_public_key(self):
        proto.send_opcode(self.t, op.CMD_MLDSA_GET_PUBKEY)

        public_key = proto.recv_response_ok(self.t)

        if len(public_key) != MLDSA44_PUBLICKEYBYTES:
            raise RuntimeError(f"Unexpected public key length: {len(public_key)}")

        self.public_key = public_key
        return public_key

    def sign(self, message: bytes) -> bytes:
        if not isinstance(message, (bytes, bytearray)):
            raise TypeError("message must be bytes")

        proto.send_opcode(self.t, op.CMD_MLDSA_SIGN)
        proto.send_packet(self.t, bytes(message))

        signature = proto.recv_response_ok(self.t)

        if len(signature) == 0 or len(signature) > MLDSA44_SIGBYTES:
            raise RuntimeError(f"Unexpected signature length: {len(signature)}")

        return signature

    def verify(self, signature: bytes, message: bytes) -> bool:
        """
        STM32 端用它目前存的 public key 驗證。
        回傳 True/False；如果是傳輸/系統層級的錯誤（不是「驗證結果為否」）
        會丟 ProtocolError，呼叫端要分開處理。
        """
        if not isinstance(signature, (bytes, bytearray)):
            raise TypeError("signature must be bytes")

        if not isinstance(message, (bytes, bytearray)):
            raise TypeError("message must be bytes")

        proto.send_opcode(self.t, op.CMD_MLDSA_VERIFY)
        proto.send_packet(self.t, bytes(signature))
        proto.send_packet(self.t, bytes(message))

        status, payload = proto.recv_response(self.t)

        if status == proto.RESP_OK:
            return True

        if status == proto.RESP_VERIFY_FAIL:
            return False

        raise proto.ProtocolError(status, payload.decode(errors="replace"))
