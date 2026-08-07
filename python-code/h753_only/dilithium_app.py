import command_opcodes as op
import packet_protocol as proto
from uart_transport import UARTTransport

DILITHIUM2_PUBLICKEYBYTES = 1312
DILITHIUM2_BYTES = 2420


class STM32Dilithium:
    def __init__(self, transport: UARTTransport):
        self.t = transport
        self.public_key = None

    def clear(self):
        proto.send_opcode(self.t, op.CMD_CLEAR)
        proto.recv_response_ok(self.t)
        return True

    def rekey(self):
        proto.send_opcode(self.t, op.CMD_DILITHIUM_REKEY)
        proto.recv_response_ok(self.t)

        self.public_key = None
        return True

    def get_public_key(self):
        proto.send_opcode(self.t, op.CMD_DILITHIUM_GET_PUBKEY)

        public_key = proto.recv_response_ok(self.t)

        if len(public_key) != DILITHIUM2_PUBLICKEYBYTES:
            raise RuntimeError(f"Unexpected public key length: {len(public_key)}")

        self.public_key = public_key
        return public_key

    def sign(self, message: bytes) -> bytes:
        if not isinstance(message, (bytes, bytearray)):
            raise TypeError("message must be bytes")

        proto.send_opcode(self.t, op.CMD_DILITHIUM_SIGN)
        proto.send_packet(self.t, bytes(message))

        signature = proto.recv_response_ok(self.t)

        if len(signature) == 0 or len(signature) > DILITHIUM2_BYTES:
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

        proto.send_opcode(self.t, op.CMD_DILITHIUM_VERIFY)
        proto.send_packet(self.t, bytes(signature))
        proto.send_packet(self.t, bytes(message))

        status, payload = proto.recv_response(self.t)

        if status == proto.RESP_OK:
            return True

        if status == proto.RESP_VERIFY_FAIL:
            return False

        raise proto.ProtocolError(status, payload.decode(errors="replace"))
