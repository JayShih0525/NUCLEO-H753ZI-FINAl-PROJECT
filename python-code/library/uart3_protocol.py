import time
import serial

UART3_OK = 0x00

UART3_ERR_LEN_TOO_BIG = 0xE1
UART3_ERR_RX_LEN = 0xE2
UART3_ERR_RX_DATA = 0xE3
UART3_ERR_TX_DATA = 0xE4
UART3_ERR_NULL_PTR = 0xE5


class UART3Protocol:
    def __init__(self, port, baud=115200, max_buffer_size=32768, timeout=0.01):
        self.port = port
        self.baud = baud
        self.max_buffer_size = max_buffer_size
        self.timeout = timeout
        self.ser = None

    def open(self):
        self.ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        time.sleep(2)
        self.clear_buffers()

    def close(self):
        if self.ser is not None and self.ser.is_open:
            self.ser.close()

    def clear_buffers(self):
        if self.ser is not None and self.ser.is_open:
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()

    def read_exact(self, n, timeout=5.0):
        data = b""
        start = time.perf_counter()

        while len(data) < n:
            chunk = self.ser.read(n - len(data))

            if chunk:
                data += chunk

            if time.perf_counter() - start > timeout:
                break

        return data

    # =========================
    # Packet mode
    # Python -> STM32:
    #   2 bytes length + data
    #
    # STM32 -> Python:
    #   1 byte status
    #   2 bytes length + data
    # =========================

    def send_packet(self, data: bytes):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")

        if len(data) > self.max_buffer_size:
            raise ValueError(f"Data too large: {len(data)} bytes")

        length_header = len(data).to_bytes(2, byteorder="big")

        self.ser.write(length_header)
        self.ser.write(data)
        self.ser.flush()

        status = self.read_exact(1, timeout=3.0)

        if len(status) != 1:
            raise TimeoutError("No status received from STM32")

        if status[0] != UART3_OK:
            raise RuntimeError(f"STM32 error status: 0x{status[0]:02X}")

    def receive_packet(self):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")

        len_bytes = self.read_exact(2, timeout=3.0)

        if len(len_bytes) != 2:
            raise TimeoutError(f"Expected 2 length bytes, got {len(len_bytes)}")

        length = int.from_bytes(len_bytes, byteorder="big")

        if length > self.max_buffer_size:
            raise ValueError(f"Received length too large: {length}")

        data = self.read_exact(length, timeout=10.0)

        if len(data) != length:
            raise TimeoutError(f"Expected {length} bytes, got {len(data)}")

        return data

    def echo(self, data: bytes):
        self.clear_buffers()

        start = time.perf_counter()

        self.send_packet(data)
        echo_data = self.receive_packet()

        end = time.perf_counter()

        elapsed = end - start
        ok = echo_data == data

        return {
            "ok": ok,
            "sent_len": len(data),
            "echo_len": len(echo_data),
            "elapsed": elapsed,
            "echo": echo_data,
        }

    # =========================
    # Raw text mode
    # 對應 C:
    #   UART3_Printf()
    #   UART3_ReadLine()
    # =========================

    def write_bytes(self, data: bytes):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")

        self.ser.write(data)
        self.ser.flush()

    def write_text(self, text: str, encoding="utf-8"):
        self.write_bytes(text.encode(encoding))

    def write_line(self, text: str, encoding="utf-8", newline="\n"):
        self.write_bytes((text + newline).encode(encoding))

    def read_line(self, timeout=5.0, encoding="utf-8"):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")

        data = b""
        start = time.perf_counter()

        while True:
            ch = self.ser.read(1)

            if ch:
                if ch == b"\0":
                    break

                if ch == b"\n" or ch == b"\r":
                    break

                data += ch

            if time.perf_counter() - start > timeout:
                break

        return data.decode(encoding, errors="replace")

    def read_available(self, timeout=0.5):
        if self.ser is None or not self.ser.is_open:
            raise RuntimeError("Serial port is not open")

        data = b""
        start = time.perf_counter()

        while time.perf_counter() - start < timeout:
            chunk = self.ser.read(self.ser.in_waiting or 1)

            if chunk:
                data += chunk                
                start = time.perf_counter()

        return data