"""Real KEM/DSA/HMAC and AES-GCM over fragmented fake TCP, no device required."""
import hashlib
import hmac
import io
import struct
import unittest
from unittest.mock import patch

from pqcrypto.kem import ml_kem_768
from pqcrypto.sign import ml_dsa_44

from host.crypto_ops import aes_encrypt
from host.pqc_camera_demo import request_encrypted_frame
from host.camera_replay import CameraReplayGuard
from host.rekey_pipeline import RekeyPipeline, AUTH, CONFIRM, COMMIT, REQUEST, REPLY
from host.serial_protocol import SerialProtocol, ProtocolError
from host.tcp_connection import TcpTransportError


def framed(data):
    return struct.pack('>I', len(data)) + data


class Board:
    def __init__(self, limit=3, slow=False):
        self.identity, self.identity_sk = ml_dsa_44.generate_keypair()
        pk, sk = ml_kem_768.generate_keypair()
        ct, key = ml_kem_768.encrypt(pk)
        self.session = pk, ct, key, 1
        self.limit, self.count, self.frame = limit, 0, 0
        self.input = io.BytesIO()
        self.phase = 'empty'
        self.slow = slow
        self.requests = []
        self.mutate = lambda body: body
        self.raw_mutate = lambda extension: extension
        self.abort_commit = False

    def write(self, packet):
        self.requests.append(packet)
        prefix = b'CAMERA_PIPELINED\n'
        assert packet.startswith(prefix)
        n = struct.unpack('>I', packet[len(prefix):len(prefix)+4])[0]
        request = packet[len(prefix)+4:]
        assert n == len(request)
        header, data, mac = request[:10], request[10:-32], request[-32:]
        action, capture, epoch, count = struct.unpack('>BBII', header)
        pk, ct, key, current = self.session
        assert epoch == current and count == self.count
        assert hmac.compare_digest(mac, hmac.digest(key, REQUEST + request[:-32], 'sha256'))
        if action == 1:
            assert self.phase == 'empty'
            self.context = hashlib.sha256(pk + ct + struct.pack('>I', epoch)).digest() + struct.pack('>II', epoch, epoch+1) + data
            self.pk, self.sk = ml_kem_768.generate_keypair()
            self.signature = ml_dsa_44.sign(self.identity_sk, AUTH + self.context + self.pk)
            self.phase = 'offer'
        elif action == 3:
            assert self.phase == 'offer'
            self.ct = data
            self.key = ml_kem_768.decrypt(self.sk, data)
            self.proof = hmac.digest(self.key, CONFIRM + self.context + self.pk + self.ct, 'sha256')
            self.phase = 'ready'
        elif action == 4:
            assert self.phase == 'ready' and self.count == self.limit
            assert hmac.compare_digest(data, hmac.digest(self.key, COMMIT + self.context, 'sha256'))
            self.session = self.pk, self.ct, self.key, epoch + 1
            self.count = 0
            self.phase = 'empty'
            key = self.key
            if self.abort_commit:
                self.input = io.BytesIO()
                return len(packet)
        if self.phase == 'offer':
            body = b'\2' + self.context + self.pk + self.signature + struct.pack('>II', 8, 30)
        elif self.phase == 'ready':
            body = b'\3' + self.proof + struct.pack('>II', 10, 8192)
        else:
            body = b'\0'
        if self.slow and capture and self.phase == 'offer':
            body = b'\1'  # Offer becomes available only in boundary polls.
        body = self.mutate(body)
        extension = body + hmac.digest(key, REPLY + header + body, 'sha256')
        extension = self.raw_mutate(extension)
        if capture:
            assert self.count < self.limit, 'old key exceeded its limit'
            self.count += 1
            self.frame += 1
            jpeg = b'\xff\xd8test\xff\xd9'
            metadata = struct.pack('>4sIIHHI', b'CAM2', self.session[3], self.frame, 320, 240, len(jpeg))
            nonce = self.frame.to_bytes(12, 'big')
            encrypted, tag = aes_encrypt(key, nonce, jpeg, metadata)
            response = f'OK epoch={self.session[3]} count={self.count} rekey={int(self.count == self.limit)}\n'.encode()
            response += b''.join(map(framed, (metadata, nonce, encrypted, tag, extension)))
        else:
            assert self.count == self.limit
            response = b'PENDING\n' + framed(extension)
        self.input = io.BytesIO(response)
        return len(packet)

    def read(self, count):
        return self.input.read(min(count, 7))

    def readline(self):
        return self.input.readline()

    def flush(self):
        pass


class PipelineTests(unittest.TestCase):
    def setup_flow(self, slow=False):
        board = Board(slow=slow)
        events = []
        protocol = SerialProtocol(board, diagnostic=events.append, trusted_key=board.identity, pipeline_rekey=True)
        flow = RekeyPipeline(protocol, board.session, board.limit)
        guard = CameraReplayGuard(board.limit)
        guard.begin_session(1)
        return board, protocol, flow, guard, events

    def frame(self, protocol, flow, guard):
        return request_encrypted_frame(protocol, flow.active[2], flow.active[3], guard, pipeline=flow)

    def test_multiple_epochs_real_crypto_and_fragmented_reads(self):
        board, protocol, flow, guard, events = self.setup_flow()
        results = [self.frame(protocol, flow, guard) for _ in range(10)]
        self.assertEqual([r.status.epoch for r in results], [1,1,1,2,2,2,3,3,3,4])
        self.assertEqual([r.status.count for r in results], [1,2,3,1,2,3,1,2,3,1])
        self.assertEqual(len(board.requests), 10)  # No standalone handshake requests.
        self.assertEqual(sum(e['event'] == 'pipeline_switch' for e in events), 3)
        self.assertEqual(board.input.read(), b'')

    def test_slow_worker_waits_without_extra_old_key_frames(self):
        board, protocol, flow, guard, events = self.setup_flow(slow=True)
        for _ in range(4):
            self.frame(protocol, flow, guard)
        self.assertEqual(board.frame, 4)
        self.assertEqual(flow.active[3], 2)
        boundary = next(e for e in events if e['event'] == 'pipeline_boundary')
        self.assertFalse(boundary['ready_ahead'])
        self.assertEqual(boundary['wait_polls'], 2)
        self.assertEqual(len(board.requests), 6)

    def test_wrong_context_signature_or_extension_mac_rejected(self):
        for target in ('context', 'signature', 'mac'):
            with self.subTest(target=target):
                board, protocol, flow, guard, _ = self.setup_flow()
                def flip(body):
                    data = bytearray(body)
                    data[1 if target == 'context' else 1257] ^= 1
                    return bytes(data)
                if target == 'mac':
                    board.raw_mutate = lambda b: b[:-1] + bytes([b[-1] ^ 1])
                else:
                    board.mutate = flip
                with self.assertRaises(ProtocolError):
                    self.frame(protocol, flow, guard)
                self.assertIsNone(flow.pending)
                self.assertEqual(len(board.requests), 1)

    def test_bad_confirmation_does_not_activate(self):
        board, protocol, flow, guard, _ = self.setup_flow()
        self.frame(protocol, flow, guard)
        board.mutate = lambda b: b[:1] + bytes([b[1] ^ 1]) + b[2:]
        with self.assertRaisesRegex(ProtocolError, 'confirmation failed'):
            self.frame(protocol, flow, guard)
        self.assertEqual(flow.active[3], 1)

    def test_disconnect_during_commit_does_not_fallback_on_same_socket(self):
        board, protocol, flow, guard, _ = self.setup_flow()
        for _ in range(3):
            self.frame(protocol, flow, guard)
        board.abort_commit = True
        with self.assertRaises(TimeoutError):
            self.frame(protocol, flow, guard)
        self.assertEqual(len(board.requests), 4)
        # Existing TCP supervisor must discard this entire flow on reconnect.

    def test_pending_deadline_no_extra_capture(self):
        board, protocol, flow, guard, _ = self.setup_flow(slow=True)
        for _ in range(3):
            self.frame(protocol, flow, guard)
        with patch('host.rekey_pipeline.time.monotonic', side_effect=[0, 6]):
            with self.assertRaises(TcpTransportError):
                flow.prepare_capture(guard)
        self.assertEqual(board.frame, 3)

    def test_missing_capability_or_short_limit_rejected(self):
        for capability, limit in ((False, 10), (True, 2)):
            protocol = SerialProtocol(None, pipeline_rekey=capability)
            with self.assertRaises(ProtocolError):
                RekeyPipeline(protocol, (), limit)
