"""Run alone: python -m unittest discover -s tests -p test_device_auth.py -v"""
import hashlib
import hmac
import unittest
from unittest.mock import patch
from pqcrypto.sign import ml_dsa_44 as dsa
from host import device_auth as auth
from host.serial_protocol import ProtocolError


class DeviceAuthTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pk, cls.sk = dsa.generate_keypair()
        cls.other_pk, _ = dsa.generate_keypair()
        cls.challenge = b'A' * 32
        cls.kem = b'K' * 1184
        cls.signature = dsa.sign(cls.sk, auth.proof_message(cls.challenge, cls.kem))

    def test_original_proof(self):
        auth.verify_proof(self.pk, self.challenge, self.kem, self.signature)

    def test_replayed_proof_rejected_for_new_challenge(self):
        with self.assertRaises(ProtocolError):
            auth.verify_proof(self.pk, b'B'*32, self.kem, self.signature)

    def test_substituted_kem_key(self):
        with self.assertRaises(ProtocolError):
            auth.verify_proof(self.pk, self.challenge, b'J'*1184, self.signature)

    def test_wrong_identity(self):
        with self.assertRaises(ProtocolError):
            auth.verify_proof(self.other_pk, self.challenge, self.kem, self.signature)

    def test_tampered_or_truncated_signature(self):
        for signature in (bytes([self.signature[0]^1])+self.signature[1:], self.signature[:-1]):
            with self.subTest(length=len(signature)), self.assertRaises(ProtocolError):
                auth.verify_proof(self.pk, self.challenge, self.kem, signature)

    def test_cross_domain_signature(self):
        sig = dsa.sign(self.sk, b'other-domain'+self.challenge+self.kem)
        with self.assertRaises(ProtocolError):
            auth.verify_proof(self.pk, self.challenge, self.kem, sig)

    def test_enrollment_mismatch(self):
        auth.check_enrollment_key(self.pk, auth.fingerprint(self.pk))
        with self.assertRaises(ProtocolError):
            auth.check_enrollment_key(self.other_pk, auth.fingerprint(self.pk))

    def test_missing_pin_does_not_contact_device(self):
        with patch.object(auth, 'load_trusted_key', side_effect=ProtocolError('missing')):
            with self.assertRaises(ProtocolError):
                auth.authenticated_kem_key(None)

    def test_confirmation_binds_all_fields(self):
        key = b'S'*32
        base = auth.confirmation_message(self.challenge, self.kem, b'C'*1088, 1)
        expected = hmac.digest(key, base, 'sha256')
        for values in ((b'B'*32, self.kem, b'C'*1088, 1),
                       (self.challenge, b'J'*1184, b'C'*1088, 1),
                       (self.challenge, self.kem, b'D'*1088, 1),
                       (self.challenge, self.kem, b'C'*1088, 2)):
            self.assertNotEqual(expected, hmac.digest(key, auth.confirmation_message(*values), 'sha256'))

    def test_confirmation_rejects_bad_proof(self):
        class Reply:
            def send_line(self, value): pass
            def expect(self, value): pass
            def send_frame(self, value): pass
            def receive_frame(self, maximum): return b'X'*32
        with self.assertRaises(ProtocolError):
            auth.confirm_session(Reply(), b'S'*32, self.kem, b'C'*1088, 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
