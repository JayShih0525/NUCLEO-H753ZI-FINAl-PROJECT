"""Socket byte stream for the existing length-prefixed protocol."""
import socket
import time


class TcpTransportError(ConnectionError):
    """A socket failure, distinct from local file or authentication errors."""


class TcpConnection:
    def __init__(self, host, port=9000, timeout=10.0, diagnostic=None, command_timeout=None, stop_event=None):
        self.command_timeout = command_timeout
        self.stop_event = stop_event
        self.deadline = None
        self.diagnostic = diagnostic
        self._receive_buffer = bytearray()
        try:
            self.socket = socket.create_connection((host, port), timeout=timeout)
        except OSError as error:
            raise TcpTransportError(str(error)) from error
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.trace('tcp_connected', local=self.socket.getsockname(), peer=self.socket.getpeername(), timeout=timeout)

    def trace(self, event, **values):
        callback = getattr(self, 'diagnostic', None)
        if callback:
            callback(dict(event=event, **values))

    def begin_command(self):
        budget = getattr(self, 'command_timeout', None)
        if budget is not None:
            self.deadline = time.monotonic() + budget
        self._check_budget()

    def _check_budget(self):
        stop = getattr(self, 'stop_event', None)
        if stop is not None and stop.is_set():
            from host.live_display import UserStop
            raise UserStop()
        deadline = getattr(self, 'deadline', None)
        if deadline is not None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.trace('tcp_command_deadline', timeout=self.command_timeout)
                raise TcpTransportError(f'Command exceeded {self.command_timeout:g}s total deadline; discard connection')
            self.socket.settimeout(min(.2, remaining))

    def read(self, size):
        if size == 0:
            return b''
        self._check_budget()
        buffer = getattr(self, '_receive_buffer', None)
        if buffer:
            result = bytes(buffer[:size])
            del buffer[:size]
            return result
        return self._recv(size)

    def _recv(self, size):
        while True:
            self._check_budget()
            try:
                data = self.socket.recv(size)
                break
            except socket.timeout as error:
                if getattr(self, 'deadline', None) is None:
                    raise TcpTransportError(str(error)) from error
                continue
            except OSError as error:
                raise TcpTransportError(str(error)) from error
        if not data:
            raise TcpTransportError('ESP32 closed the TCP connection')
        return data

    def readline(self):
        # One TCP receive can contain text, frame headers and encrypted payload.
        # Keep everything after the newline for read(); never discard or reorder it.
        buffer = getattr(self, '_receive_buffer', None)
        if buffer is None:
            buffer = self._receive_buffer = bytearray()
        while True:
            self._check_budget()
            newline = buffer.find(b'\n', 0, 4096)
            if newline >= 0:
                result = bytes(buffer[:newline + 1])
                del buffer[:newline + 1]
                return result
            if len(buffer) >= 4096:
                raise ValueError('TCP protocol line exceeds 4096 bytes')
            try:
                buffer.extend(self._recv(65536 - len(buffer)))
            except (TimeoutError, ConnectionError, OSError) as error:
                raise type(error)(f'TCP line read failed after {len(buffer)} bytes: {error}') from error

    def write(self, data):
        started = time.monotonic()
        self._check_budget()
        if getattr(self, "deadline", None) is not None:
            self.socket.settimeout(max(.001, self.deadline - time.monotonic()))
        try:
            self.socket.sendall(data)
        except OSError as error:
            self.trace('tcp_write_error', requested=len(data), elapsed_ms=(time.monotonic()-started)*1000,
                       reason=str(error))
            raise TcpTransportError(str(error)) from error
        self.trace('tcp_write_complete', requested=len(data), elapsed_ms=(time.monotonic()-started)*1000)
        return len(data)

    def flush(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.socket.close()
        self.trace('tcp_closed')
