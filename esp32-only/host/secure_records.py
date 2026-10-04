"""v7 ordered, authenticated byte records (integrity; payload is not encrypted here).

Image confidentiality remains AES-GCM. Connection keys are independent of image
epochs and are replaced on a full mutual handshake, not on pipeline image rekey.
"""
import hashlib
import hmac
import struct

from host.serial_protocol import ProtocolError
from host.tcp_connection import TcpTransportError

HEADER = struct.Struct('>4s16sQI')
MAX_PAYLOAD = 4096
MAX_SEQUENCE = (1 << 64) - 1
DOMAIN = b'esp32-only/records/v1\0'


def material(secret, binding, epoch):
    seed = hashlib.sha256(binding).digest() + struct.pack('>I', epoch)
    return tuple(hmac.digest(secret, DOMAIN + label + seed, 'sha256')
                 for label in (b'session', b'host', b'device'))


class RecordConnection:
    def __init__(self, raw, secret, binding, epoch, *, device=False):
        self.raw = raw
        session, host, board = material(secret, binding, epoch)
        self.session = session[:16]
        self.tx_key, self.rx_key = (board, host) if device else (host, board)
        self.tx_sequence = self.rx_sequence = 0
        self.buffer = bytearray()
        self.failed = False

    def _reject(self, reason):
        self.failed = True
        self.buffer.clear()
        self.tx_key = self.rx_key = b''
        raise ProtocolError('Authenticated record rejected: ' + reason)

    def _check(self):
        if self.failed:
            raise ProtocolError('Authenticated record connection is unusable')

    def begin_command(self):
        self._check()
        begin = getattr(self.raw, 'begin_command', None)
        if begin:
            begin()

    def _exact(self, count):
        data = bytearray()
        while len(data) < count:
            chunk = self.raw.read(count - len(data))
            if not chunk:
                raise TcpTransportError('Truncated authenticated record; discard connection')
            data.extend(chunk)
        return bytes(data)

    def _receive(self):
        self._check()
        try:
            header = self._exact(HEADER.size)
            magic, session, sequence, length = HEADER.unpack(header)
            if (magic != b'PQR7' or session != self.session or
                    sequence != self.rx_sequence or sequence == MAX_SEQUENCE or
                    not 1 <= length <= MAX_PAYLOAD):
                self._reject('header / session / sequence / length')
            payload = self._exact(length)
            tag = self._exact(32)
            if not hmac.compare_digest(tag, hmac.digest(self.rx_key, header + payload, 'sha256')):
                self._reject('MAC')
            self.rx_sequence += 1
            self.buffer.extend(payload)
        except BaseException:
            # No reuse after a partial record, cancellation, or integrity failure.
            self.failed = True
            self.buffer.clear()
            raise

    def read(self, size):
        self._check()
        if size <= 0:
            return b''
        if not self.buffer:
            self._receive()
        data = bytes(self.buffer[:size])
        del self.buffer[:size]
        return data

    def readline(self):
        self._check()
        while True:
            newline = self.buffer.find(b'\n', 0, 4096)
            if newline >= 0:
                data = bytes(self.buffer[:newline + 1])
                del self.buffer[:newline + 1]
                return data
            if len(self.buffer) >= 4096:
                self._reject('line exceeds 4096 bytes')
            self._receive()

    def write(self, data):
        self._check()
        try:
            for offset in range(0, len(data), MAX_PAYLOAD):
                if self.tx_sequence == MAX_SEQUENCE:
                    self._reject('sequence exhausted')
                payload = data[offset:offset + MAX_PAYLOAD]
                header = HEADER.pack(b'PQR7', self.session, self.tx_sequence, len(payload))
                packet = header + payload + hmac.digest(self.tx_key, header + payload, 'sha256')
                if self.raw.write(packet) != len(packet):
                    raise TcpTransportError('Incomplete authenticated record write')
                self.tx_sequence += 1
        except BaseException:
            self.failed = True
            raise
        return len(data)

    def flush(self):
        self.raw.flush()


def activate(protocol, secret, binding, epoch):
    raw = protocol.serial_port
    if isinstance(raw, RecordConnection):
        if raw.buffer or raw.failed:
            raw._reject('unexpected bytes at new handshake boundary')
        raw = raw.raw
    protocol.serial_port = RecordConnection(raw, secret, binding, epoch)
    protocol.trace('secure_records_active', epoch=epoch, max_payload=MAX_PAYLOAD)
