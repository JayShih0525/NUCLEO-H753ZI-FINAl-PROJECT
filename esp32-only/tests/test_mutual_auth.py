"""Real crypto on a fragmented in-memory wire; hardware validation remains separate."""
import hashlib
import hmac
import io
import os
import struct
import unittest
from unittest.mock import patch
from pqcrypto.kem import ml_kem_768
from pqcrypto.sign import ml_dsa_44
from host import mutual_auth as auth
from host.serial_protocol import SerialProtocol, ProtocolError


def frame(data):
    return struct.pack('>I', len(data)) + data


class Board:
    def __init__(self, host_pk):
        self.pk, self.sk = ml_dsa_44.generate_keypair()
        self.host_pk = host_pk
        self.data = io.BytesIO()
        self.authorized = False
        self.pending = False
        self.bad_device = self.bad_confirmation = False
        self.mutate_finish = lambda value: value
        self.epoch = 0
        self.v7 = False

    def domain(self, value):
        return value.replace(b'/v1\0', b'/v2\0') if self.v7 else value

    def write(self, packet):
        command, body = packet.split(b'\n', 1)
        size, = struct.unpack('>I', body[:4])
        payload = body[4:]
        assert size == len(payload)
        if command == b'MUTUAL_BEGIN':
            self.authorized = False
            if payload[:32] != hashlib.sha256(self.host_pk).digest():
                self.data = io.BytesIO(b'ERR HOST_NOT_TRUSTED\n')
                return len(packet)
            self.kem_pk, self.kem_sk = ml_kem_768.generate_keypair()
            nonce = os.urandom(32)
            self.limit = int.from_bytes(payload[64:], 'big')
            self.transcript = (payload[:32] + hashlib.sha256(self.pk).digest() +
                               payload[32:64] + nonce + self.kem_pk + payload[64:])
            signature = ml_dsa_44.sign(self.sk, self.domain(auth.DEVICE) + self.transcript)
            if self.bad_device:
                signature = bytes([signature[0] ^ 1]) + signature[1:]
            self.pending = True
            self.data = io.BytesIO(b'OK\n' + frame(nonce) + frame(self.kem_pk) + frame(signature))
        elif command == b'MUTUAL_FINISH':
            payload = self.mutate_finish(payload)
            self.last_finish = payload
            if not self.pending:
                self.data = io.BytesIO(b'ERR MUTUAL_EXPIRED\n')
                return len(packet)
            self.pending = False
            ct, signature, proof = payload[:1088], payload[1088:3508], payload[3508:]
            binding = self.transcript + ct
            if not ml_dsa_44.verify(self.host_pk, self.domain(auth.HOST) + binding, signature):
                self.data = io.BytesIO(b'ERR HOST_SIGNATURE_REJECTED\n')
                return len(packet)
            key = ml_kem_768.decrypt(self.kem_sk, ct)
            if not hmac.compare_digest(proof, hmac.digest(key, self.domain(auth.HOST_KEY) + binding, 'sha256')):
                self.data = io.BytesIO(b'ERR HOST_KEY_PROOF_REJECTED\n')
                return len(packet)
            self.authorized = True
            self.epoch += 1
            device_proof = hmac.digest(key, self.domain(auth.DEVICE_KEY) + binding + struct.pack('>I', self.epoch), 'sha256')
            if self.bad_confirmation:
                device_proof = b'\0' * 32
            self.key = key
            self.data = io.BytesIO(f'KEM_OK epoch={self.epoch} limit={self.limit} elapsed_ms=10\n'.encode() + frame(device_proof))
        return len(packet)

    def read(self, size):
        return self.data.read(min(size, 37))

    def readline(self):
        return self.data.readline()

    def flush(self):
        pass


class ProtectedBoard(Board):
    """Exercise initial/plain and later/protected handshake boundaries."""
    def __init__(self, host_pk):
        super().__init__(host_pk)
        self.v7 = True
        self.records = None
        self.wire = None

    def write(self, packet):
        from host.secure_records import RecordConnection
        original_length = len(packet)
        previous = self.records
        if previous:
            self.wire.data = io.BytesIO(packet)
            command = previous.readline().strip()
            if command in (b'MUTUAL_BEGIN', b'MUTUAL_FINISH'):
                payload = SerialProtocol(previous).receive_frame(3540)
                packet = command + b'\n' + frame(payload)
            else:
                packet = command + b'\n'
        command = packet.split(b'\n', 1)[0]
        if command == b'INFO':
            self.data = io.BytesIO(b'INFO proto=7 inline_rekey=1 mutual_auth=1\n')
        else:
            super().write(packet)
        response = self.data.read()
        if previous:
            self.wire.sent.clear()
            previous.write(response)
            response = bytes(self.wire.sent)
        self.data = io.BytesIO(response)
        if command == b'MUTUAL_FINISH' and self.authorized:
            class Wire:
                def __init__(self): self.data = io.BytesIO(); self.sent = bytearray()
                def read(self, size): return self.data.read(min(size, 37))
                def write(self, data): self.sent.extend(data); return len(data)
                def flush(self): pass
            self.wire = Wire()
            self.records = RecordConnection(self.wire, self.key,
                self.transcript + self.last_finish[:1088], self.epoch, device=True)
        return original_length


class MutualAuthTests(unittest.TestCase):
    def setUp(self):
        self.identity = ml_dsa_44.generate_keypair()
        self.board = Board(self.identity[0])
        self.protocol = SerialProtocol(self.board, trusted_key=self.board.pk,
                                       inline_rekey=True, mutual_auth=True)
        self.protocol.rekey_interval = 10
        self.mock = patch('host.mutual_auth.load_identity', return_value=self.identity)
        self.mock.start()
        self.addCleanup(self.mock.stop)

    def test_roundtrip_and_new_session_change_key(self):
        first = auth.establish(self.protocol)
        self.assertTrue(self.board.authorized)
        self.assertEqual(first[2], self.board.key)
        second = auth.establish(self.protocol)
        self.assertNotEqual(first[2], second[2])
        self.assertEqual(second[3], 2)

    def test_v7_activates_records_after_both_proofs(self):
        from host.secure_records import RecordConnection
        self.board.v7 = self.protocol.secure_records = True
        auth.establish(self.protocol)
        self.assertIsInstance(self.protocol.serial_port, RecordConnection)
        self.assertIs(self.protocol.serial_port.raw, self.board)

    def test_v7_cannot_be_downgraded_by_changing_info(self):
        self.board.v7 = True
        with self.assertRaisesRegex(ProtocolError, 'device proof rejected'):
            auth.establish(self.protocol)

    def test_v6_cannot_impersonate_v7(self):
        self.protocol.secure_records = True
        with self.assertRaisesRegex(ProtocolError, 'device proof rejected'):
            auth.establish(self.protocol)

    def test_v7_blocking_rekey_uses_old_channel_then_switches_both_directions(self):
        from host.pqc_host_demo import request_info
        board = ProtectedBoard(self.identity[0])
        protocol = SerialProtocol(board, trusted_key=board.pk)
        request_info(protocol)
        first = auth.establish(protocol)
        request_info(protocol)  # Both command and response now carry records.
        old_session = protocol.serial_port.session
        second = auth.establish(protocol)  # Protected by the old channel.
        self.assertNotEqual(first[2], second[2])
        self.assertNotEqual(old_session, protocol.serial_port.session)
        request_info(protocol)
        self.assertEqual(protocol.serial_port.tx_sequence, 1)
        self.assertEqual(protocol.serial_port.rx_sequence, 1)

    def test_device_signature_rejected_before_host_proof(self):
        self.board.bad_device = True
        with self.assertRaises(ProtocolError):
            auth.establish(self.protocol)
        self.assertFalse(hasattr(self.board, 'last_finish'))

    def test_unknown_host_rejected(self):
        self.board.host_pk, _ = ml_dsa_44.generate_keypair()
        with self.assertRaisesRegex(ProtocolError, 'HOST_NOT_TRUSTED'):
            auth.establish(self.protocol)
        self.assertFalse(self.board.authorized)

    def test_changed_ciphertext_or_signature_or_host_mac_rejected(self):
        for offset in (0, 1088, 3508):
            with self.subTest(offset=offset):
                self.board.mutate_finish = lambda value: value[:offset] + bytes([value[offset] ^ 1]) + value[offset+1:]
                with self.assertRaises(ProtocolError):
                    auth.establish(self.protocol)
                self.assertFalse(self.board.authorized)

    def test_old_proof_rejected_for_new_challenge(self):
        auth.establish(self.protocol)
        old = self.board.last_finish
        self.board.mutate_finish = lambda _: old
        with self.assertRaisesRegex(ProtocolError, 'HOST_SIGNATURE_REJECTED'):
            auth.establish(self.protocol)
        self.assertFalse(self.board.authorized)

    def test_reuse_consumed_challenge_rejected(self):
        auth.establish(self.protocol)
        self.protocol.send_command_frame('MUTUAL_FINISH', self.board.last_finish)
        with self.assertRaisesRegex(ProtocolError, 'MUTUAL_EXPIRED'):
            self.protocol.expect_prefix('KEM_OK ')

    def test_invalid_device_key_confirmation_rejected(self):
        self.board.bad_confirmation = True
        with self.assertRaisesRegex(ProtocolError, 'session proof rejected'):
            auth.establish(self.protocol)

    def test_transcript_binds_rekey_limit_and_both_identities(self):
        nonce, device_nonce = os.urandom(32), os.urandom(32)
        kem_pk, _ = ml_kem_768.generate_keypair()
        original = auth.context(self.identity[0], self.board.pk, nonce, device_nonce, kem_pk, 10)
        self.assertEqual(len(original), 1316)
        self.assertNotEqual(original, auth.context(self.identity[0], self.board.pk, nonce, device_nonce, kem_pk, 30))
        self.assertNotEqual(original, auth.context(self.board.pk, self.identity[0], nonce, device_nonce, kem_pk, 10))

    def test_legacy_unsigned_kem_not_sent_in_v6(self):
        with self.assertRaises(ProtocolError):
            self.protocol.send_command_frame('KEM_DECAPSULATE_INLINE', b'\0' * 1088)
