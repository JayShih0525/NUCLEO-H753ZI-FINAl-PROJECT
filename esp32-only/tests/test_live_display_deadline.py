import argparse
import socket
import threading
import unittest
from unittest.mock import Mock, patch

from host.live_display import LatestDisplay, UserStop, run_with_display
from host.serial_protocol import SerialProtocol
from host.tcp_connection import TcpConnection, TcpTransportError


class DeadlineTests(unittest.TestCase):
    def connection(self):
        connection = TcpConnection.__new__(TcpConnection)
        connection.socket = Mock()
        connection.command_timeout = 1
        connection.stop_event = threading.Event()
        connection.deadline = None
        return connection

    def test_trickling_payload_cannot_extend_command_budget(self):
        connection = self.connection()
        protocol = SerialProtocol(connection)
        now = [0]
        def receive(size):
            now[0] += .4
            return b'x'
        connection.socket.recv.side_effect = receive
        with patch('host.tcp_connection.time.monotonic', side_effect=lambda: now[0]):
            with self.assertRaisesRegex(TcpTransportError, 'total deadline'):
                with connection:
                    protocol.send_line('INFO')
                    protocol._read_exact(10)
        connection.socket.close.assert_called_once()
        self.assertEqual(connection.socket.recv.call_count, 3)

    def test_poll_timeout_does_not_abandon_valid_response(self):
        connection = self.connection()
        connection.socket.recv.side_effect = [socket.timeout(), b'OK\n']
        connection.begin_command()
        self.assertEqual(connection.read(3), b'OK\n')

    def test_cancellation_closes_partial_connection(self):
        connection = self.connection()
        with self.assertRaises(UserStop):
            with connection:
                connection.begin_command()
                connection.stop_event.set()
                connection.read(4)
        connection.socket.close.assert_called_once()

    def test_continuation_write_keeps_same_deadline(self):
        connection = self.connection()
        protocol = SerialProtocol(connection)
        protocol.send_line('AUTH_KEM')
        deadline = connection.deadline
        protocol.send_frame(bytes(32))
        self.assertEqual(connection.deadline, deadline)


class DisplayTests(unittest.TestCase):
    def test_latest_only_and_retained_image_during_reconnect(self):
        view = LatestDisplay()
        first, second = object(), object()
        view.publish(first)
        view.publish(second)
        self.assertEqual(view.skipped, 1)
        self.assertIs(view.snapshot()[0], second)
        view.set_status('Reconnecting')
        self.assertIs(view.snapshot()[0], second)
        self.assertEqual(view.snapshot()[2], 'Reconnecting')

    def test_gui_on_main_receiver_on_worker_and_quit_joins(self):
        main_id = threading.get_ident()
        cv2 = Mock()
        cv2.waitKey.return_value = ord('q')
        cv2.WND_PROP_VISIBLE = 0
        cv2.FONT_HERSHEY_SIMPLEX = 0
        cv2.imshow.side_effect = lambda *args: self.assertEqual(threading.get_ident(), main_id)
        args = argparse.Namespace(device_name='test', diagnostics=None)
        def receiver():
            self.assertNotEqual(threading.get_ident(), main_id)
            self.assertTrue(args._stop_event.wait(2))
            return 0
        self.assertEqual(run_with_display(args, receiver, cv2), 0)
        cv2.imshow.assert_called()
        cv2.destroyAllWindows.assert_called_once()
