import unittest
from unittest.mock import patch

from host.serial_connection import camera_connection


class FakePort:
    fail_open = False

    def __init__(self, **options):
        self.options = options
        self.is_open = False
        self.rtscts = options['rtscts']
        self.dsrdtr = options['dsrdtr']
        self.closed = False

    def open(self):
        assert self.options['port'] is None
        assert self.dtr is False and self.rts is False
        assert self.port == 'COM3'
        self.is_open = True
        if self.fail_open:
            raise OSError('open failed')

    def close(self):
        self.closed = True
        self.is_open = False


class SerialConnectionTests(unittest.TestCase):
    def run_connection(self, failure=None):
        events = []
        port = FakePort(port=None, rtscts=False, dsrdtr=False)
        port.fail_open = failure == 'open'
        with patch('host.serial_connection.serial.Serial', return_value=port) as factory:
            def execute():
                with camera_connection('COM3', 921600, events.append) as active:
                    self.assertTrue(active.is_open)
                    if failure == 'stream':
                        raise RuntimeError('stream failed')
            if failure:
                with self.assertRaises(OSError if failure == 'open' else RuntimeError):
                    execute()
            else:
                execute()
            factory.assert_called_once_with(port=None, baudrate=921600,
                timeout=5.0, write_timeout=10.0, rtscts=False, dsrdtr=False)
        self.assertTrue(port.closed)
        self.assertEqual(events[0]['phase'], 'before_open')
        self.assertEqual(events[-1], dict(event='port_closed', is_open=False))
        self.assertEqual(sum(e['event'] == 'port_opened' for e in events),
                         0 if failure == 'open' else 1)

    def test_states_configured_before_open(self):
        self.run_connection()

    def test_stream_failure_closes_port(self):
        self.run_connection('stream')

    def test_open_failure_closes_partial_handle(self):
        self.run_connection('open')
