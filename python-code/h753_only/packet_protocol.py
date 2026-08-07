import struct

from uart_transport import UARTTransport

# 跟韌體 inc/command_opcodes.h 的 RESP_* 對齊
RESP_OK = 0x00
RESP_VERIFY_FAIL = 0x02
RESP_ERR_RX = 0x03
RESP_ERR_CRYPTO = 0x04
RESP_ERR_NOT_READY = 0x05
RESP_ERR_LEN = 0x06
RESP_UNKNOWN_CMD = 0xFF

_STATUS_NAMES = {
    RESP_OK: "OK",
    RESP_VERIFY_FAIL: "VERIFY_FAIL",
    RESP_ERR_RX: "ERR_RX",
    RESP_ERR_CRYPTO: "ERR_CRYPTO",
    RESP_ERR_NOT_READY: "ERR_NOT_READY",
    RESP_ERR_LEN: "ERR_LEN",
    RESP_UNKNOWN_CMD: "UNKNOWN_CMD",
}


class ProtocolError(RuntimeError):
    def __init__(self, status: int, message: str):
        self.status = status
        self.message = message
        name = _STATUS_NAMES.get(status, f"0x{status:02X}")
        super().__init__(f"[{name}] {message}")


def send_opcode(t: UARTTransport, opcode: int):
    t.write(bytes([opcode]))


def send_packet(t: UARTTransport, payload: bytes):
    """
    Host -> STM32 上傳一段資料。格式沒有變過：
        [4-byte BE length][payload]
    STM32 收完固定回 1 byte 狀態（0x00 = OK，非 0 表示 STM32 收資料失敗，
    這個 byte 跟指令回應的 status byte 是不同的東西，純粹是
    Protocol_ReceivePacket 內部的收據確認）。
    """
    header = struct.pack(">I", len(payload))
    t.write(header + payload)

    status = t.read_exact(1)[0]
    if status != 0x00:
        raise ProtocolError(status, "STM32 rejected uploaded packet (transport-level RX error)")


def recv_response(t: UARTTransport):
    """
    讀 STM32 -> host 的統一指令回應：
        [1 byte status][4 byte BE length][payload]
    回傳 (status, payload_bytes)，不管成功或失敗都用同一個 shape 讀，
    不需要事先知道這個指令「這次」是要回文字還是回二進位資料。
    """
    status = t.read_exact(1)[0]
    length = struct.unpack(">I", t.read_exact(4))[0]
    payload = t.read_exact(length) if length > 0 else b""

    return status, payload


def recv_response_ok(t: UARTTransport) -> bytes:
    """
    方便函式：預期是 RESP_OK，否則直接丟 ProtocolError（payload 當成
    ASCII 錯誤訊息解出來）。回傳 payload bytes。
    """
    status, payload = recv_response(t)

    if status != RESP_OK:
        raise ProtocolError(status, payload.decode(errors="replace"))

    return payload
