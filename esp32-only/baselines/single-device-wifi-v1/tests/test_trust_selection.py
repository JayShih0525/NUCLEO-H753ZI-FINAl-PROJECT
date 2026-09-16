import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
from host.device_auth import load_trusted_key, protocol_trusted_key, authenticated_kem_key
from host.serial_protocol import SerialProtocol, ProtocolError
from host.pqc_host_demo import test_device_signature as sign_test


class TrustSelectionTests(unittest.TestCase):
    def test_two_protocols_remain_independent(self):
        a=SerialProtocol(Mock(), trusted_key=b'a'*1312)
        b=SerialProtocol(Mock(), trusted_key=b'b'*1312)
        for protocol in (a,b,a):
            with patch.object(protocol,'receive_frame', side_effect=[b'k'*1184,b's'*2420]), \
                 patch.object(protocol,'expect'), patch('host.device_auth.verify_proof') as verify, \
                 patch('builtins.print'):
                authenticated_kem_key(protocol)
                self.assertEqual(verify.call_args.args[0], protocol.trusted_key)

    def test_explicit_file_missing_does_not_fall_back(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'key.pub'
            with self.assertRaises(ProtocolError): load_trusted_key(path)
            path.write_bytes(b'x')
            with self.assertRaises(ProtocolError): load_trusted_key(path)
            path.write_bytes(b'a'*1312)
            protocol=SerialProtocol(Mock(),trusted_key=load_trusted_key(path))
            path.write_bytes(b'b'*1312)
            self.assertEqual(protocol_trusted_key(protocol), b'a'*1312)

    def test_final_signature_uses_selected_key(self):
        protocol=Mock(trusted_key=b'b'*1312)
        protocol.receive_frame.return_value=b's'*2420
        with patch('host.pqc_host_demo.verify_dsa',side_effect=[True,False]) as verify, patch('builtins.print'):
            sign_test(protocol,b'd'*32)
        self.assertTrue(all(c.args[0]==b'b'*1312 for c in verify.call_args_list))
