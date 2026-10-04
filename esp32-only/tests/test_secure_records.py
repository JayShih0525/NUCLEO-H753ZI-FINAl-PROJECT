import io
import struct
import unittest

from host.secure_records import RecordConnection, HEADER, MAX_PAYLOAD, MAX_SEQUENCE, activate
from host.serial_protocol import SerialProtocol, ProtocolError
from host.tcp_connection import TcpTransportError


class Wire:
    def __init__(self):
        self.data = bytearray()
        self.sent = bytearray()
        self.fragment = 7

    def read(self, count):
        n = min(count, self.fragment)
        data = bytes(self.data[:n])
        del self.data[:n]
        return data

    def write(self, data):
        self.sent.extend(data)
        return len(data)

    def flush(self):
        pass


class RecordTests(unittest.TestCase):
    def pair(self, epoch=1):
        wire = Wire()
        host = RecordConnection(wire, bytes(range(32)), b'transcript', epoch)
        device = RecordConnection(wire, bytes(range(32)), b'transcript', epoch, device=True)
        return wire, host, device

    def test_fragmented_command_and_coalesced_binary_response(self):
        wire, host, device = self.pair()
        host.write(b'CAMERA_MODE VGA\n')
        wire.data.extend(wire.sent); wire.sent.clear()
        self.assertEqual(device.readline(), b'CAMERA_MODE VGA\n')
        blob = bytes(range(256)) * 50
        device.write(b'OK\n' + struct.pack('>I', len(blob)) + blob)
        wire.data.extend(wire.sent)
        protocol = SerialProtocol(host)
        protocol.expect('OK')
        self.assertEqual(protocol.receive_frame(len(blob)), blob)
        self.assertEqual(host.rx_sequence, 4)

    def test_tampering_every_header_field_payload_and_mac_rejected(self):
        for offset in (0, 4, 20, 28, HEADER.size, HEADER.size + 5):
            wire, host, device = self.pair()
            host.write(b'INFO\n')
            wire.sent[offset] ^= 1
            wire.data.extend(wire.sent)
            with self.subTest(offset=offset), self.assertRaises(ProtocolError):
                device.readline()
            self.assertEqual(device.buffer, b'')
            with self.assertRaises(ProtocolError):
                device.read(1)

    def test_replay_and_out_of_order_rejected(self):
        wire, host, device = self.pair()
        host.write(b'INFO\n')
        first = bytes(wire.sent)
        wire.data.extend(first + first)
        self.assertEqual(device.readline(), b'INFO\n')
        with self.assertRaises(ProtocolError):
            device.readline()
        wire, host, device = self.pair()
        host.tx_sequence = 1
        host.write(b'INFO\n'); wire.data.extend(wire.sent)
        with self.assertRaises(ProtocolError):
            device.readline()

    def test_direction_and_old_session_rejected(self):
        for epoch in (1, 2):
            wire, host, device = self.pair()
            host.write(b'INFO\n'); wire.data.extend(wire.sent)
            receiver = host if epoch == 1 else RecordConnection(wire, bytes(range(32)), b'transcript', 2, device=True)
            with self.assertRaises(ProtocolError):
                receiver.readline()

    def test_truncation_never_exposes_partial_payload(self):
        wire, host, _ = self.pair()
        host.write(b'RESET_SESSION\n')
        original = bytes(wire.sent)
        for length in (0, 1, 31, 32, 40, len(original)-1):
            wire, _, device = self.pair()
            wire.data.extend(original[:length])
            with self.subTest(length=length), self.assertRaises(TcpTransportError):
                device.readline()
            self.assertFalse(device.buffer)
            self.assertTrue(device.failed)

    def test_oversize_zero_and_exhaustion(self):
        for length in (0, MAX_PAYLOAD + 1, 0xffffffff):
            wire, host, device = self.pair()
            wire.data.extend(HEADER.pack(b'PQR7', host.session, 0, length))
            with self.assertRaises(ProtocolError):
                device.read(1)
        wire, host, device = self.pair()
        host.tx_sequence = MAX_SEQUENCE
        with self.assertRaises(ProtocolError):
            host.write(b'x')
        self.assertFalse(wire.sent)

    def test_activation_replaces_channel_without_nested_records(self):
        wire, host, _ = self.pair()
        protocol = SerialProtocol(host)
        activate(protocol, b'x'*32, b'new transcript', 2)
        self.assertIs(protocol.serial_port.raw, wire)
        self.assertEqual(protocol.serial_port.tx_sequence, 0)
        protocol.serial_port.buffer.extend(b'extra')
        with self.assertRaises(ProtocolError):
            activate(protocol, b'x'*32, b'new transcript', 3)

    def test_plaintext_injection_rejected(self):
        wire, _, device = self.pair()
        wire.data.extend(b'RESET_SESSION\n' + b' '*40)
        with self.assertRaises(ProtocolError):
            device.readline()

    def test_no_extra_network_roundtrip(self):
        wire, host, _ = self.pair()
        host.write(b'INFO\n')
        self.assertEqual(len(wire.sent), 5 + 64)
        self.assertEqual(host.rx_sequence, 0)
