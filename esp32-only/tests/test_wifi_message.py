import os
import unittest
from unittest.mock import Mock
from cryptography.exceptions import InvalidTag
from host.crypto_ops import aes_encrypt
from host.pqc_wifi_demo import exchange
from host.serial_protocol import ProtocolError, SerialProtocol
from host.tcp_connection import TcpConnection


class WifiMessageTests(unittest.TestCase):
    def setUp(self):
        self.key = os.urandom(32)
        self.message = b'private message'
        self.protocol = Mock()
        self.protocol.expect_prefix.return_value = 'OK epoch=3 count=1 rekey=0'

    def response(self, index=1):
        aad = b'esp32-only/wifi-echo/v1\0' + (3).to_bytes(4, 'big') + index.to_bytes(4, 'big')
        nonce = os.urandom(12)
        ciphertext, tag = aes_encrypt(self.key, nonce, self.message, aad)
        self.protocol.receive_frame.side_effect = [nonce, ciphertext, tag]

    def test_encrypted_roundtrip_without_plaintext_frames(self):
        self.response()
        exchange(self.protocol, self.key, 3, 1, self.message, 1, 10)
        sent = [call.args[0] for call in self.protocol.send_frame.call_args_list]
        self.assertNotIn(self.message, sent)

    def test_previous_message_response_rejected(self):
        self.response(index=1)
        with self.assertRaises(InvalidTag):
            exchange(self.protocol, self.key, 3, 2, self.message, 1, 10)

    def test_reflected_request_rejected(self):
        sent = []
        self.protocol.send_frame.side_effect = sent.append
        self.protocol.receive_frame.side_effect = lambda size: sent[0]
        with self.assertRaises(ProtocolError):
            exchange(self.protocol, self.key, 3, 1, self.message, 1, 10)

    def test_tcp_fragmented_frame(self):
        connection = TcpConnection.__new__(TcpConnection)
        connection.socket = Mock()
        connection.socket.recv.side_effect = [b'\0', b'\0\0\3', b'a', b'bc']
        self.assertEqual(SerialProtocol(connection).receive_frame(3), b'abc')

    def test_tcp_disconnect_stops_read(self):
        connection = TcpConnection.__new__(TcpConnection)
        connection.socket = Mock()
        connection.socket.recv.return_value = b''
        with self.assertRaises(ConnectionError):
            connection.read(4)

    def test_tcp_context_closes_after_failure(self):
        connection = TcpConnection.__new__(TcpConnection)
        connection.socket = Mock()
        with self.assertRaises(RuntimeError):
            with connection:
                raise RuntimeError('failed')
        connection.socket.close.assert_called_once()
