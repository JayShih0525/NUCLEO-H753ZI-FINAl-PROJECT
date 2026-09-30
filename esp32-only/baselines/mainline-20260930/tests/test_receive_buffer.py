import io
import json
import socket
import struct
import threading
import unittest
from unittest.mock import Mock, patch
from host.tcp_connection import TcpConnection, TcpTransportError
from host.serial_protocol import SerialProtocol
from host.live_display import UserStop
from host.trace_writer import TraceWriter


class ReceiveBufferTests(unittest.TestCase):
    def connection(self, chunks):
        connection = TcpConnection.__new__(TcpConnection)
        connection.socket = Mock()
        connection.socket.recv.side_effect = chunks
        connection.deadline = None
        connection.stop_event = threading.Event()
        return connection

    def test_coalesced_line_and_binary_frame_use_one_recv(self):
        payload = b'\x00\n\xff' * 100
        connection = self.connection([b'OK\n' + struct.pack('>I', len(payload)) + payload])
        protocol = SerialProtocol(connection)
        protocol.expect('OK')
        self.assertEqual(protocol.receive_frame(len(payload)), payload)
        self.assertEqual(connection.socket.recv.call_count, 1)

    def test_fragmented_line_and_payload_preserve_every_byte(self):
        connection = self.connection([b'O', b'K\n\0\0', b'\0\x03a', b'bcNEXT\n'])
        protocol = SerialProtocol(connection)
        protocol.expect('OK')
        # recv(size) cannot return more than size; use a real socket below for this case.
        connection.socket.recv.side_effect = [b'\0\x03', b'a', b'bc']
        self.assertEqual(protocol.receive_frame(3), b'abc')

    def test_real_socket_multiple_responses_and_binary_newlines(self):
        a, b = socket.socketpair()
        connection = TcpConnection.__new__(TcpConnection)
        connection.socket = a
        connection.deadline = None
        connection.stop_event = threading.Event()
        try:
            b.sendall(b'FIRST\n' + struct.pack('>I', 4) + b'\n\0\xffx' + b'SECOND\n')
            protocol = SerialProtocol(connection)
            protocol.expect('FIRST')
            self.assertEqual(protocol.receive_frame(4), b'\n\0\xffx')
            protocol.expect('SECOND')
        finally:
            a.close()
            b.close()

    def test_cancellation_and_expiry_apply_to_buffered_data(self):
        connection = self.connection([b'OK\nsecret'])
        self.assertEqual(connection.readline(), b'OK\n')
        connection.stop_event.set()
        with self.assertRaises(UserStop):
            connection.read(6)
        connection.stop_event.clear()
        connection.deadline = 1
        connection.command_timeout = 1
        with patch('host.tcp_connection.time.monotonic', return_value=2):
            with self.assertRaises(TcpTransportError):
                connection.read(6)

    def test_line_limit_not_confused_by_large_binary_tail(self):
        connection = self.connection([b'OK\n' + b'x' * 60000])
        self.assertEqual(connection.readline(), b'OK\n')
        self.assertEqual(len(connection.read(60000)), 60000)
        invalid = self.connection([b'x' * 4096 + b'\n'])
        with self.assertRaises(ValueError):
            invalid.readline()

    def test_partial_eof_reports_line_progress(self):
        connection = self.connection([b'PART', b''])
        with self.assertRaisesRegex(TcpTransportError, 'after 4 bytes'):
            connection.readline()


class TraceWriterTests(unittest.TestCase):
    def test_periodic_flush_and_immediate_failure_preserve_records(self):
        stream = Mock(wraps=io.StringIO())
        clock = [0]
        with patch('host.trace_writer.time.monotonic', side_effect=lambda: clock[0]):
            writer = TraceWriter(stream)
            for i in range(100):
                writer.write(dict(event='camera_timing', frame=i))
            stream.flush.assert_not_called()
            clock[0] = 1
            writer.write(dict(event='camera_verified'))
            self.assertEqual(stream.flush.call_count, 1)
            writer.write(dict(event='run_error'))
            self.assertEqual(stream.flush.call_count, 2)
        records = [json.loads(line) for line in stream.getvalue().splitlines()]
        self.assertEqual(len(records), 102)
        self.assertEqual([r['frame'] for r in records[:100]], list(range(100)))
