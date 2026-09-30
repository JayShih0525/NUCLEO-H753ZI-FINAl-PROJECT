"""Feed real AES-GCM packets through the production camera receiver; no COM."""
import os
import struct
import unittest
from unittest.mock import Mock

from cryptography.exceptions import InvalidTag
from host.crypto_ops import aes_encrypt
from host.camera_replay import CameraReplayGuard
from host.pqc_camera_demo import request_encrypted_frame
from host.serial_protocol import ProtocolError


class CameraReplayTests(unittest.TestCase):
    def setUp(self):
        self.key = os.urandom(32)
        self.guard = CameraReplayGuard(2)
        self.guard.begin_session(7)

    def packet(self, frame=100, epoch=7, count=1, key=None):
        metadata = struct.pack('>4sIIHHI', b'CAM2', epoch, frame, 320, 240, 4)
        nonce = os.urandom(12)
        ciphertext, tag = aes_encrypt(key or self.key, nonce, b'\xff\xd8\xff\xd9', metadata)
        return [metadata, nonce, ciphertext, tag], f'OK epoch={epoch} count={count} rekey={int(count == 2)}'

    def receive(self, packet, key=None):
        protocol = Mock()
        protocol.expect_prefix.return_value = packet[1]
        protocol.receive_frame.side_effect = packet[0]
        return request_encrypted_frame(protocol, key or self.key, self.guard.epoch, self.guard)

    def test_normal_frames_and_rekey_keep_frame_sequence(self):
        self.receive(self.packet())
        self.receive(self.packet(frame=101, count=2))
        self.guard.begin_session(8)
        self.key = os.urandom(32)
        self.receive(self.packet(frame=102, epoch=8))
        self.assertEqual((self.guard.last_frame, self.guard.count), (102, 1))

    def test_identical_valid_packet_replay_rejected(self):
        packet = self.packet()
        self.receive(packet)
        with self.assertRaisesRegex(ProtocolError, 'replay/order'):
            self.receive(packet)

    def test_out_of_order_and_skipped_frames_rejected_without_advancing(self):
        self.receive(self.packet())
        for frame in [99, 102]:
            with self.subTest(frame=frame), self.assertRaises(ProtocolError):
                self.receive(self.packet(frame=frame, count=2))
        self.receive(self.packet(frame=101, count=2))

    def test_old_epoch_after_rekey_rejected(self):
        packet = self.packet()
        self.guard.begin_session(8)
        with self.assertRaisesRegex(ProtocolError, 'epoch mismatch'):
            self.receive(packet)

    def test_old_connection_same_epoch_rejected_by_new_key(self):
        packet = self.packet()
        self.key = os.urandom(32)
        with self.assertRaises(InvalidTag):
            self.receive(packet)
        self.assertIsNone(self.guard.last_frame)

    def test_forged_metadata_and_bad_tag_do_not_poison_state(self):
        packet, status = self.packet()
        for index in [0, 3]:
            bad = list(packet)
            changed = bytearray(bad[index])
            changed[-1] ^= 1 if index == 3 else 0
            if index == 0:
                changed[11] ^= 1  # Authenticated frame_id, unchanged length.
            bad[index] = bytes(changed)
            with self.subTest(index=index), self.assertRaises(InvalidTag):
                self.receive((bad, status))
            self.assertIsNone(self.guard.last_frame)
        self.receive((packet, status))

    def test_plaintext_status_count_and_rekey_tampering_rejected(self):
        packet, _ = self.packet()
        for status in ['OK epoch=7 count=2 rekey=1', 'OK epoch=7 count=1 rekey=1']:
            with self.subTest(status=status), self.assertRaises(ProtocolError):
                self.receive((packet, status))
        self.assertEqual(self.guard.count, 0)

    def test_frame_reset_across_rekey_rejected(self):
        self.receive(self.packet())
        self.guard.begin_session(8)
        with self.assertRaises(ProtocolError):
            self.receive(self.packet(frame=0, epoch=8))

    def test_epoch_rollback_and_skip_rejected(self):
        for epoch in [7, 6, 9, 0]:
            with self.subTest(epoch=epoch), self.assertRaises(ProtocolError):
                self.guard.begin_session(epoch)
        self.assertEqual(self.guard.epoch, 7)

    def test_frame_counter_wrap_rejected(self):
        self.receive(self.packet(frame=0xffffffff))
        with self.assertRaises(ProtocolError):
            self.receive(self.packet(frame=0, count=2))

    def test_injected_partial_header_does_not_accept_frame(self):
        packet, status = self.packet()
        protocol = Mock()
        protocol.expect_prefix.return_value = status
        protocol.receive_frame.side_effect = packet
        with self.assertRaisesRegex(TimeoutError, 'Injected timeout'):
            request_encrypted_frame(protocol, self.key, 7, self.guard, inject_timeout=True)
        protocol._read_exact.assert_called_once_with(3, label='tag', phase='header')
        self.assertIsNone(self.guard.last_frame)
