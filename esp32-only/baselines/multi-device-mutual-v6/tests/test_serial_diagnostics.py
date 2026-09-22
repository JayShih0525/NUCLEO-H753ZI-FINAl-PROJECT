import struct
import unittest
from host.serial_protocol import SerialProtocol, ProtocolError


class Port:
    def __init__(self, chunks):
        self.chunks = list(chunks)

    def read(self, length):
        if not self.chunks:
            return b''
        chunk = self.chunks.pop(0)
        if len(chunk) > length:
            self.chunks.insert(0, chunk[length:])
        return chunk[:length]


class SerialDiagnosticTests(unittest.TestCase):
    def test_fragmented_frame_is_assembled(self):
        events = []
        protocol = SerialProtocol(Port([b'\x00', b'\x00\x00\x03', b'a', b'bc']), events.append)
        self.assertEqual(protocol.receive_frame(3, label='tag'), b'abc')
        self.assertEqual(events[-1]['received'], 3)

    def test_three_byte_header_timeout_preserves_context(self):
        events = []
        protocol = SerialProtocol(Port([b'\x00\x00\x10']), events.append,
                                  dict(last_verified_frame=186, expected_epoch=19))
        with self.assertRaises(TimeoutError):
            protocol.receive_frame(16, label='tag')
        event = events[-1]
        self.assertEqual(event['header_hex'], '000010')
        self.assertEqual(event['received'], 3)
        self.assertEqual(event['label'], 'tag')
        self.assertEqual(event['last_verified_frame'], 186)

    def test_payload_timeout_does_not_log_payload(self):
        events = []
        protocol = SerialProtocol(Port([struct.pack('>I', 6), b'secret'[:3]]), events.append)
        with self.assertRaises(TimeoutError):
            protocol.receive_frame(6)
        self.assertEqual(events[-1]['received'], 3)
        self.assertIsNone(events[-1]['header_hex'])
        self.assertNotIn('sec', str(events))

    def test_oversized_length_recorded_before_rejection(self):
        events = []
        protocol = SerialProtocol(Port([struct.pack('>I', 999)]), events.append)
        with self.assertRaises(ProtocolError):
            protocol.receive_frame(16, label='tag')
        self.assertEqual(events[-1]['declared'], 999)


if __name__ == '__main__':
    unittest.main()
