"""
wifi_bulk_protocol.py
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
WiFi TCP Bulk Transfer Protocol Library

設計風格與 uart3_protocol.py 完全對齊，方法名稱一致，
可以直接替換使用。

Frame 格式（與 MCU wifi_bulk_transfer.c 完全對應）：
    [0]    0xAB   MAGIC_0
    [1]    0xCD   MAGIC_1
    [2]    TYPE   0x01=DATA 0x02=ACK 0x03=PING 0x04=PONG 0xFF=ABORT
    [3]    SEQ    序號 0~255 循環
    [4]    LEN_HI payload 長度高位元組
    [5]    LEN_LO payload 長度低位元組
    [6..]  PAYLOAD
    [-2:]  CRC16  (covers byte 0 ~ end of payload)

對應關係：
    UART3Protocol               WiFiBulkProtocol
    ─────────────────────────   ──────────────────────────
    open()                  →   open()
    close()                 →   close()
    send_packet(data)       →   send_packet(data)
    receive_packet()        →   receive_packet()
    echo(data)              →   echo(data)
    write_line(text)        →   write_line(text)
    read_line()             →   read_line()
    ─                       →   ping()   (新增)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
"""

import socket
import time

# ── 協議常數（必須與 MCU 端 wifi_bulk_transfer.h 完全一致）──
MAGIC_0, MAGIC_1 = 0xAB, 0xCD
HDR_SIZE         = 6
FTR_SIZE         = 2
PAYLOAD_MAX      = 4096
MAX_RETRY        = 3

TYPE_DATA  = 0x01
TYPE_ACK   = 0x02
TYPE_PING  = 0x03
TYPE_PONG  = 0x04
TYPE_ABORT = 0xFF

ACK_OK    = 0x00
ACK_RETRY = 0x01
ACK_ABORT = 0xFF


def _crc16(data: bytes) -> int:
    """CRC16-CCITT xmodem，與 MCU 端 Bulk_CRC16() 完全一致"""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc


class WiFiBulkProtocol:
    """
    TCP Bulk Transfer Protocol Client

    基本使用：
        wifi = WiFiBulkProtocol(host="172.20.10.7", port=5555)
        wifi.open()

        wifi.send_packet(data)          # MCU → Server 後，Server 轉送給 MCU 的資料
        data = wifi.receive_packet()    # 接收 MCU 送來的資料
        result = wifi.echo(data)        # 送出並等 MCU 原封回傳（測試用）

        wifi.close()
    """

    def __init__(self,
                 host: str,
                 port: int       = 5555,
                 max_buffer_size: int   = 65536,
                 timeout: float  = 8.0):
        self.host            = host
        self.port            = port
        self.max_buffer_size = max_buffer_size
        self.timeout         = timeout
        self._sock           = None
        self._tx_seq         = 0

    # ──────────────────────────────────────────────────────────
    #  連線管理
    # ──────────────────────────────────────────────────────────

    def open(self):
        """建立 TCP 連線"""
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.settimeout(self.timeout)
        self._sock.connect((self.host, self.port))
        self._tx_seq = 0
        print(f"[WiFi] Connected → {self.host}:{self.port}")

    def close(self):
        """關閉 TCP 連線"""
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None
            print("[WiFi] Disconnected")

    @property
    def is_open(self) -> bool:
        return self._sock is not None

    # ──────────────────────────────────────────────────────────
    #  底層 Frame 收送（內部使用）
    # ──────────────────────────────────────────────────────────

    def _recv_exact(self, n: int) -> bytes:
        """確保讀到 exactly n bytes"""
        buf      = b""
        deadline = time.perf_counter() + self.timeout
        while len(buf) < n:
            if time.perf_counter() > deadline:
                raise TimeoutError(f"recv_exact: need {n}, got {len(buf)}")
            try:
                chunk = self._sock.recv(n - len(buf))
                if not chunk:
                    raise ConnectionError("MCU disconnected")
                buf += chunk
            except socket.timeout:
                raise TimeoutError("socket timeout")
        return buf

    def _send_frame(self, ftype: int, seq: int, payload: bytes = b""):
        """組裝並送出一個完整 Frame"""
        plen  = len(payload)
        hdr   = bytes([MAGIC_0, MAGIC_1, ftype, seq,
                        (plen >> 8) & 0xFF, plen & 0xFF])
        body  = hdr + payload
        crc   = _crc16(body)
        frame = body + bytes([(crc >> 8) & 0xFF, crc & 0xFF])
        self._sock.sendall(frame)

    def _recv_frame(self) -> tuple:
        """
        接收並解析一個完整 Frame。
        回傳 (type, seq, payload)，CRC 錯誤拋出 ValueError
        """
        # 掃描 MAGIC（處理串流中的雜訊）
        while True:
            b = self._recv_exact(1)
            if b[0] == MAGIC_0:
                b2 = self._recv_exact(1)
                if b2[0] == MAGIC_1:
                    break

        # 讀 Header 剩餘 4 bytes：[TYPE, SEQ, LEN_HI, LEN_LO]
        rest  = self._recv_exact(HDR_SIZE - 2)
        ftype = rest[0]
        seq   = rest[1]
        plen  = (rest[2] << 8) | rest[3]

        # 讀 Payload + CRC
        tail    = self._recv_exact(plen + FTR_SIZE)
        payload = tail[:plen]
        got_crc = (tail[plen] << 8) | tail[plen + 1]

        # 驗 CRC
        body    = bytes([MAGIC_0, MAGIC_1, ftype, seq,
                         (plen >> 8) & 0xFF, plen & 0xFF]) + payload
        exp_crc = _crc16(body)
        if exp_crc != got_crc:
            raise ValueError(f"CRC fail: exp={exp_crc:#06x} got={got_crc:#06x}")

        return ftype, seq, payload

    def _wait_ack(self, expected_seq: int) -> int:
        """等待 ACK frame，回傳 ACK status byte"""
        ftype, seq, payload = self._recv_frame()
        if ftype != TYPE_ACK:
            raise ValueError(f"Expected ACK, got type={ftype:#04x}")
        if seq != expected_seq:
            raise ValueError(f"ACK seq mismatch: exp={expected_seq} got={seq}")
        return payload[0] if payload else ACK_OK

    # ──────────────────────────────────────────────────────────
    #  公開 API（對應 UART3Protocol 風格）
    # ──────────────────────────────────────────────────────────

    def send_packet(self, data: bytes):
        """
        傳送一包資料到 MCU。
        自動分割成 PAYLOAD_MAX 大小的 chunk，每個 chunk 等 ACK。

        對應 UART3Protocol.send_packet()
        """
        if not self.is_open:
            raise RuntimeError("Not connected. Call open() first.")
        if len(data) > self.max_buffer_size:
            raise ValueError(f"Data too large: {len(data)} > {self.max_buffer_size}")

        offset = 0
        seq    = self._tx_seq

        while offset < len(data):
            chunk    = data[offset : offset + PAYLOAD_MAX]
            this_len = len(chunk)

            for retry in range(MAX_RETRY):
                self._send_frame(TYPE_DATA, seq, chunk)
                try:
                    status = self._wait_ack(seq)
                    if status == ACK_ABORT:
                        raise RuntimeError("MCU sent ABORT")
                    if status == ACK_OK:
                        break
                    # ACK_RETRY → 重送
                except (TimeoutError, ValueError):
                    if retry == MAX_RETRY - 1:
                        raise RuntimeError(f"send_packet: seq={seq} failed after {MAX_RETRY} retries")

            offset += this_len
            seq = (seq + 1) & 0xFF

        # EOF frame：plen=0 告知 MCU 傳輸結束
        for retry in range(MAX_RETRY):
            self._send_frame(TYPE_DATA, seq, b"")
            try:
                self._wait_ack(seq)
                break
            except Exception:
                pass

        self._tx_seq = (seq + 1) & 0xFF

    def receive_packet(self) -> bytes:
        """
        從 MCU 接收一包完整資料（等到 EOF frame 為止）。
        每收到一個 chunk 自動回 ACK。

        對應 UART3Protocol.receive_packet()
        """
        if not self.is_open:
            raise RuntimeError("Not connected. Call open() first.")

        result  = bytearray()
        exp_seq = 0

        while True:
            try:
                ftype, seq, payload = self._recv_frame()
            except Exception as e:
                self._send_frame(TYPE_ACK, exp_seq, bytes([ACK_RETRY]))
                raise TimeoutError(f"receive_packet error: {e}")

            if ftype == TYPE_ABORT:
                raise RuntimeError("MCU sent ABORT")

            if ftype == TYPE_PING:
                self._send_frame(TYPE_PONG, seq)
                continue

            if ftype != TYPE_DATA:
                self._send_frame(TYPE_ACK, seq, bytes([ACK_RETRY]))
                continue

            if len(payload) == 0:          # EOF
                self._send_frame(TYPE_ACK, seq, bytes([ACK_OK]))
                break

            if seq != exp_seq:             # 序號錯誤
                self._send_frame(TYPE_ACK, seq, bytes([ACK_RETRY]))
                continue

            result.extend(payload)
            self._send_frame(TYPE_ACK, seq, bytes([ACK_OK]))
            exp_seq = (exp_seq + 1) & 0xFF

        return bytes(result)

    def echo(self, data: bytes) -> dict:
        """
        傳送資料並等 MCU 原封回傳，計算速度與正確性。

        對應 UART3Protocol.echo()

        Returns
        -------
        {
            "ok"      : bool,   資料是否完整一致
            "sent_len": int,    送出的 bytes 數
            "echo_len": int,    收到的 bytes 數
            "elapsed" : float,  來回時間（秒）
            "echo"    : bytes,  收到的原始資料
        }
        """
        start     = time.perf_counter()
        self.send_packet(data)
        echo_data = self.receive_packet()
        elapsed   = time.perf_counter() - start

        return {
            "ok"       : echo_data == data,
            "sent_len" : len(data),
            "echo_len" : len(echo_data),
            "elapsed"  : elapsed,
            "echo"     : echo_data,
        }

    def ping(self) -> float:
        """
        送 Ping，等 Pong，回傳來回時間（秒）。
        連線失敗拋出 TimeoutError。
        """
        start = time.perf_counter()
        self._send_frame(TYPE_PING, 0)
        ftype, _, _ = self._recv_frame()
        if ftype != TYPE_PONG:
            raise TimeoutError("Ping failed: no PONG")
        return time.perf_counter() - start

    def write_line(self, text: str, encoding: str = "utf-8"):
        """
        用 send_packet 送出文字。
        對應 UART3Protocol.write_line()
        """
        self.send_packet(text.encode(encoding))

    def read_line(self, encoding: str = "utf-8") -> str:
        """
        用 receive_packet 接收並解碼成文字。
        對應 UART3Protocol.read_line()
        """
        return self.receive_packet().decode(encoding, errors="replace")
