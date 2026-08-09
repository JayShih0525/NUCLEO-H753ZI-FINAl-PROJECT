import time
import serial


# pyserial port 底層固定的單次 read timeout。開機設定一次之後不再變動 ——
# 之前的版本在 read_exact() 的迴圈裡動態改 self.ser.timeout，pyserial
# 每次設定 .timeout 屬性都會重新呼叫 tcsetattr() 重設整個 port，在
# 4,000,000 baud 這種高速下，這個重設中間的空窗期會漏掉剛好進來的 byte，
# 造成資料在傳輸中被吃掉、後面全部錯位（就是長度欄位讀出離譜大數字的
# 原因）。現在固定只在 open() 設一次，之後所有讀取都用 Python 端自己的
# wall-clock deadline 迴圈去累積，不再碰 self.ser.timeout。
_PORT_POLL_TIMEOUT = 0.05


class UARTTransport:
    """
    純粹的 byte-level transport，對應韌體 transport_uart.c 那一層。
    不知道 opcode、不知道 status byte 是什麼意思 —— 這些邏輯在
    library/packet_protocol.py，這裡只管開關 port 跟收送 bytes。
    """

    def __init__(self, port: str, baud: int, max_buffer_size: int, timeout: float = 5.0):
        self.port = port
        self.baud = baud
        self.max_buffer_size = max_buffer_size
        self.timeout = timeout  # 每次協定操作（read_exact / read_line_raw）預設的「總」等待時間
        self.ser: serial.Serial | None = None

    def open(self):
        # port 的底層 timeout 只在這裡設一次，之後永遠不再修改，
        # 避免高 baud rate 下反覆 tcsetattr() 造成漏 byte。
        self.ser = serial.Serial(self.port, self.baud, timeout=_PORT_POLL_TIMEOUT)

    def close(self):
        if self.ser is not None:
            self.ser.close()
            self.ser = None

    def clear_buffers(self):
        if self.ser is not None:
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()

    def read_available(self, duration: float = 0.5) -> bytes:
        """
        在 duration 秒內，把目前 OS buffer 裡已經到的東西全部撈出來。
        只拿來在程式一開始清殘留資料用，不要拿來讀正式的協定回應
        （正式回應要用 read_exact，確保長度對得上）。
        """
        if self.ser is None:
            return b""

        end = time.time() + duration
        data = bytearray()

        while time.time() < end:
            n = self.ser.in_waiting
            if n:
                data += self.ser.read(n)
            else:
                time.sleep(0.01)

        return bytes(data)

    def read_exact(self, n: int, timeout: float = None) -> bytes:
        """
        阻塞讀滿 n bytes，直到 timeout 秒為止。

        實作上完全不碰 self.ser.timeout（那個固定值只在 open() 設一次），
        每次底層 self.ser.read() 呼叫本身最多等 _PORT_POLL_TIMEOUT 秒，
        這裡的 Python 迴圈只是不斷呼叫它、累積 bytes，直到收滿 n 個或
        超過自己算的 wall-clock deadline。
        """
        if self.ser is None:
            raise RuntimeError("UART not open")

        effective_timeout = timeout if timeout is not None else self.timeout
        deadline = time.time() + effective_timeout

        buf = bytearray()

        while len(buf) < n and time.time() < deadline:
            chunk = self.ser.read(n - len(buf))
            if chunk:
                buf += chunk

        if len(buf) != n:
            raise TimeoutError(
                f"Expected {n} bytes, got {len(buf)} (timeout={effective_timeout}s)"
            )

        return bytes(buf)

    def write(self, data: bytes):
        if self.ser is None:
            raise RuntimeError("UART not open")

        self.ser.write(data)
        self.ser.flush()

    def read_line_raw(self, timeout: float = None) -> str:
        """
        讀到換行字元為止，一樣不碰 self.ser.timeout，靠 Python 端 deadline
        迴圈累積 byte。只用在開機那一次性的純文字 banner
        （例如 "CRYPTO_APP_READY"），這是協定裡唯一允許的自由格式文字，
        因為它保證是重開機後送出的第一件事，不會跟任何指令回應混在一起。
        """
        if self.ser is None:
            raise RuntimeError("UART not open")

        effective_timeout = timeout if timeout is not None else self.timeout
        deadline = time.time() + effective_timeout

        buf = bytearray()

        while time.time() < deadline:
            b = self.ser.read(1)

            if not b:
                continue

            if b in (b"\n", b"\r"):
                if buf:
                    break
                continue  # 跳過行首多餘的 \r\n

            buf += b

        return bytes(buf).decode(errors="replace").strip()

    def read_boot_banner(self, expected: str, timeout: float = 5.0) -> str:
        line = self.read_line_raw(timeout=timeout)

        if line != expected:
            raise RuntimeError(
                f"沒收到預期的開機 banner {expected!r}（收到: {line!r}）。"
                "STM32 可能沒有真的重開機，或是燒的韌體版本不對，"
                "請確認板子已 reset，且韌體是這個新版 opcode 協定。"
            )

        return line