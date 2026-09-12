"""Socket byte stream for the existing length-prefixed protocol."""
import socket
import time


class TcpConnection:
    def __init__(self, host, port=9000, timeout=10.0, diagnostic=None):
        self.diagnostic = diagnostic
        self.socket = socket.create_connection((host, port), timeout=timeout)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.trace('tcp_connected', local=self.socket.getsockname(), peer=self.socket.getpeername(), timeout=timeout)

    def trace(self, event, **values):
        callback = getattr(self, 'diagnostic', None)
        if callback:
            callback(dict(event=event, **values))

    def read(self, size):
        if size == 0:
            return b''
        data = self.socket.recv(size)
        if not data:
            raise ConnectionError('ESP32 closed the TCP connection')
        return data

    def readline(self):
        result = bytearray()
        while len(result) < 4096:
            try:
                result.extend(self.read(1))
            except (TimeoutError, ConnectionError, OSError) as error:
                raise type(error)(f'TCP line read failed after {len(result)} bytes: {error}') from error
            if result[-1] == 10:
                return bytes(result)
        raise ValueError('TCP protocol line exceeds 4096 bytes')

    def write(self, data):
        started = time.monotonic()
        try:
            self.socket.sendall(data)
        except OSError as error:
            self.trace('tcp_write_error', requested=len(data), elapsed_ms=(time.monotonic()-started)*1000,
                       reason=str(error))
            raise
        self.trace('tcp_write_complete', requested=len(data), elapsed_ms=(time.monotonic()-started)*1000)
        return len(data)

    def flush(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.socket.close()
        self.trace('tcp_closed')
