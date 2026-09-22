import io
import struct
import unittest
from unittest.mock import Mock, patch

from host.serial_protocol import SerialProtocol, ProtocolError
from host.pqc_host_demo import request_info, establish_session


class Wire:
    def __init__(self, data):
        self.input = io.BytesIO(data)
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def flush(self):
        pass

    def read(self, count):
        return self.input.read(min(count, 7))  # Fragmentation must remain supported.

    def readline(self):
        return self.input.readline()


def framed(data):
    return struct.pack('>I', len(data)) + data


class InlineHandshakeTests(unittest.TestCase):
    def test_pipeline_capability_is_explicit_and_reset_on_reconnect(self):
        protocol = SerialProtocol(Wire(b'INFO proto=5 inline_rekey=1 pipeline_rekey=1\n'))
        request_info(protocol)
        self.assertTrue(protocol.pipeline_rekey)
        protocol.serial_port = Wire(b'INFO proto=5 inline_rekey=1\n')
        request_info(protocol)
        self.assertFalse(protocol.pipeline_rekey)

    def test_negotiation_requires_version_and_capability(self):
        for info, expected in [('proto=5 inline_rekey=1', True), ('proto=4', False),
                               ('proto=5 inline_rekey=0', False), ('proto=6 inline_rekey=1', True),
                               ('proto=7 inline_rekey=1', False)]:
            protocol = SerialProtocol(Wire(f'INFO {info}\n'.encode()))
            request_info(protocol)
            self.assertEqual(protocol.inline_rekey, expected)

    def test_full_handshake_without_ready_preserves_crypto_inputs(self):
        public, ciphertext, secret, signature, proof = b'p'*1184, b'c'*1088, b'k'*32, b's'*2420, b'h'*32
        wire = Wire(b'OK\n'+framed(public)+framed(signature)+
                    b'KEM_OK epoch=7 limit=10 elapsed_ms=10\nOK\n'+framed(proof))
        protocol = SerialProtocol(wire, trusted_key=b't'*1312, inline_rekey=True)
        with patch('host.device_auth.os.urandom', return_value=b'n'*32), \
             patch('host.device_auth.verify_proof') as verify, \
             patch('host.pqc_host_demo.ml_kem_768.encrypt', return_value=(ciphertext, secret)) as encaps, \
             patch('host.device_auth.hmac.digest', return_value=proof), patch('builtins.print'):
            self.assertEqual(establish_session(protocol), (public, ciphertext, secret, 7))
        encaps.assert_called_once_with(public)
        verify.assert_called_once_with(b't'*1312, b'n'*32, public, signature)
        self.assertEqual(wire.writes, [b'AUTH_KEM_INLINE\n'+framed(b'n'*32),
                                      b'KEM_DECAPSULATE_INLINE\n'+framed(ciphertext),
                                      b'CONFIRM_SESSION_INLINE\n'+framed(b'n'*32)])
        self.assertEqual(wire.input.read(), b'')

    def test_invalid_requests_never_written(self):
        wire = Wire(b'')
        protocol = SerialProtocol(wire)
        with self.assertRaises(ProtocolError):
            protocol.send_command_frame('AUTH_KEM_INLINE', b'x'*32)
        protocol.inline_rekey = True
        for command, payload in [('AUTH_KEM_INLINE', b'x'*31), ('WRONG', b'x'*32)]:
            with self.assertRaises(ProtocolError):
                protocol.send_command_frame(command, payload)
        self.assertEqual(wire.writes, [])

    def test_bad_signature_stops_before_encapsulation(self):
        wire = Wire(b'OK\n'+framed(b'p'*1184)+framed(b's'*2420))
        protocol = SerialProtocol(wire, trusted_key=b't'*1312, inline_rekey=True)
        with patch('host.device_auth.verify_proof', side_effect=ProtocolError('bad signature')), \
             patch('host.pqc_host_demo.ml_kem_768.encrypt') as encaps:
            with self.assertRaises(ProtocolError):
                establish_session(protocol)
        encaps.assert_not_called()
        self.assertEqual(len(wire.writes), 1)

    def test_error_reply_does_not_retry_legacy_on_same_stream(self):
        wire = Wire(b'ERR BAD_AUTH_CHALLENGE\n')
        protocol = SerialProtocol(wire, trusted_key=b't'*1312, inline_rekey=True)
        with self.assertRaises(ProtocolError):
            establish_session(protocol)
        self.assertEqual(len(wire.writes), 1)
