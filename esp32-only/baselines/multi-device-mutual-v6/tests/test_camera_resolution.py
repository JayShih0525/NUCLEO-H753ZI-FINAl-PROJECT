import unittest
from unittest.mock import Mock, patch

from host.pqc_camera_demo import configure_camera_mode
from host.serial_protocol import ProtocolError


class ResolutionTests(unittest.TestCase):
    def test_modes_keep_quality_and_use_single_existing_exchange(self):
        for resolution, suffix in [('qvga', ''), ('vga', ' VGA'), ('svga', ' SVGA')]:
            protocol = Mock()
            with patch('builtins.print'):
                configure_camera_mode(protocol, 'record', resolution)
            protocol.send_line.assert_called_once_with('CAMERA_MODE STREAM' + suffix)
            protocol.expect_prefix.assert_called_once_with('OK camera_mode=')
            protocol.trace.assert_called_once_with('camera_configuration', mode='record',
                                                  resolution=resolution, jpeg_quality=15)

    def test_old_firmware_rejection_is_not_silent_fallback(self):
        protocol = Mock()
        protocol.expect_prefix.side_effect = ProtocolError('ERR UNKNOWN_COMMAND')
        with self.assertRaises(ProtocolError):
            configure_camera_mode(protocol, 'record', 'svga')
        protocol.trace.assert_not_called()

    def test_invalid_resolution_and_photo_override_rejected_before_send(self):
        for mode, resolution in [('record', 'xga'), ('photo', 'vga')]:
            protocol = Mock()
            with self.assertRaises(ValueError):
                configure_camera_mode(protocol, mode, resolution)
            protocol.send_line.assert_not_called()
