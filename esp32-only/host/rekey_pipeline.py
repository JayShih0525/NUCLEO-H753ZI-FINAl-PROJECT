"""Opt-in camera rekey state machine. One owner reads/writes the TCP stream."""
import hashlib
import hmac
import os
import struct
import time

from pqcrypto.kem import ml_kem_768
from pqcrypto.sign import ml_dsa_44

from host.device_auth import protocol_trusted_key
from host.serial_protocol import ProtocolError
from host.tcp_connection import TcpTransportError

AUTH = b'esp32-only/pipeline-auth/v1\0'
CONFIRM = b'esp32-only/pipeline-confirm/v1\0'
COMMIT = b'esp32-only/pipeline-commit/v1\0'
REQUEST = b'esp32-only/pipeline-request/v1\0'
REPLY = b'esp32-only/pipeline-reply/v1\0'


class RekeyPipeline:
    def __init__(self, protocol, session, limit):
        if protocol.pipeline_rekey is not True or limit < 3:
            raise ProtocolError('Pipeline requires advertised capability and rekey-every >= 3')
        self.protocol, self.active, self.limit = protocol, session, limit
        self.count = 0
        self.phase = 'empty'
        self.pending = None
        self.context = None
        self.header = None
        self.switch_started = None
        self.prepare_started = None
        self.boundary_ready = False

    def _request(self, capture):
        pk, ct, key, epoch = self.active
        if self.phase == 'empty':
            if epoch >= 0xffffffff:
                raise ProtocolError('Pipeline epoch exhausted; reconnect required')
            action, data = 1, os.urandom(32)
            binding = hashlib.sha256(pk + ct + struct.pack('>I', epoch)).digest()
            self.context = binding + struct.pack('>II', epoch, epoch + 1) + data
            self.prepare_started = time.perf_counter()
            self.phase = 'preparing'
        elif self.phase == 'offer':
            action, data = 3, self.pending[1]
            self.phase = 'confirming'
        else:
            action, data = (0 if self.phase == 'ready' else 2), b''
        self._send(action, capture, data, epoch, self.count, key)

    def _send(self, action, capture, data, epoch, count, key):
        self.header = struct.pack('>BBII', action, int(capture), epoch, count)
        request = self.header + data
        self.protocol.send_pipeline_request(request + hmac.digest(key, REQUEST + request, 'sha256'))

    def prepare_capture(self, replay_guard):
        """At the boundary, wait without producing any additional old-key frames."""
        if self.count == self.limit:
            self.switch_started = time.perf_counter()
            self.boundary_ready = self.phase == 'ready'
            deadline = time.monotonic() + 5
            polls = 0
            while self.phase != 'ready':
                if time.monotonic() >= deadline:
                    # A progress timeout requires a fresh TCP session, just like a
                    # socket timeout. Authentication failures remain ProtocolError.
                    raise TcpTransportError('Pending rekey not ready at boundary; reconnect required')
                self._request(False)
                self.protocol.expect('PENDING')
                self.accept_extension(self.protocol.receive_frame(3717, label='pipeline'))
                polls += 1
                if self.phase != 'ready':
                    time.sleep(.01)
            self.protocol.trace('pipeline_boundary', ready_ahead=self.boundary_ready, wait_polls=polls,
                                wait_ms=(time.perf_counter() - self.switch_started) * 1000)
            old = self.active
            token = hmac.digest(self.pending[2], COMMIT + self.context, 'sha256')
            self._send(4, True, token, old[3], self.count, old[2])
            # The next frame and extension must authenticate with the new secret.
            self.active = self.pending
            self.pending = None
            self.count = 0
            self.phase = 'committing'
            replay_guard.begin_session(self.active[3])
        else:
            self._request(True)
        return self.active[2], self.active[3]

    def accept_extension(self, extension):
        if len(extension) < 33:
            raise ProtocolError('Truncated pipeline extension')
        body, actual = extension[:-32], extension[-32:]
        expected = hmac.digest(self.active[2], REPLY + self.header + body, 'sha256')
        if not hmac.compare_digest(expected, actual):
            raise ProtocolError('Pipeline extension authentication failed')
        kind = body[0]
        if kind == 0 and len(body) == 1 and self.phase == 'committing':
            self.phase = 'empty'
            return
        if kind == 1 and len(body) == 1 and self.phase in ('preparing', 'confirming'):
            return
        if kind == 2 and len(body) == 3685 and self.phase == 'preparing':
            context, pk, signature = body[1:73], body[73:1257], body[1257:3677]
            if context != self.context or pk == self.active[0]:
                raise ProtocolError('Pending key belongs to wrong session/epoch/challenge or was reused')
            try:
                valid = ml_dsa_44.verify(protocol_trusted_key(self.protocol), AUTH + context + pk, signature)
            except ValueError as error:
                raise ProtocolError('Malformed pending signature') from error
            if not valid:
                raise ProtocolError('Pending device identity proof rejected')
            ct, secret = ml_kem_768.encrypt(pk)
            if len(ct) != 1088 or len(secret) != 32 or secret == self.active[2]:
                raise ProtocolError('Invalid or reused pending KEM secret')
            self.pending = pk, ct, secret, self.active[3] + 1
            self.phase = 'offer'
            keygen_ms, sign_ms = struct.unpack('>II', body[3677:])
            self.protocol.trace('pipeline_offer', epoch=self.pending[3],
                                keygen_ms=keygen_ms, sign_ms=sign_ms)
            return
        if kind == 3 and len(body) == 41 and self.phase in ('confirming', 'ready'):
            pk, ct, secret, epoch = self.pending
            expected = hmac.digest(secret, CONFIRM + self.context + pk + ct, 'sha256')
            if not hmac.compare_digest(expected, body[1:33]):
                raise ProtocolError('Pending session key confirmation failed')
            if self.phase != 'ready':
                decap_ms, stack_min = struct.unpack('>II', body[33:])
                self.protocol.trace('pipeline_prepared', epoch=epoch, decap_ms=decap_ms,
                                    worker_stack_min=stack_min,
                                    prepare_wall_ms=(time.perf_counter() - self.prepare_started) * 1000)
            self.phase = 'ready'
            return
        raise ProtocolError(f'Unexpected pipeline reply type={kind}, phase={self.phase}')

    def frame_accepted(self):
        self.count += 1
        if self.switch_started is not None:
            self.protocol.trace('pipeline_switch', epoch=self.active[3], ready_ahead=self.boundary_ready,
                                boundary_to_verified_ms=(time.perf_counter() - self.switch_started) * 1000)
            self.switch_started = None
