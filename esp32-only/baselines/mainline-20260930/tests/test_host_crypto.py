from __future__ import annotations

import os
import unittest

from cryptography.exceptions import InvalidTag

from host.crypto_ops import (
    aes_decrypt,
    aes_encrypt,
    dsa_roundtrip,
    kem_roundtrip,
    verify_dsa,
)
from host.pqc_host_demo import (
    AesStatus,
    MemoryStatus,
    format_memory_status,
    parse_aes_status,
    parse_memory_status,
)
from host.pqc_camera_demo import CameraMetadata, parse_camera_metadata
from host.serial_protocol import ProtocolError


class HostCryptoTests(unittest.TestCase):
    def test_camera_metadata_parser(self) -> None:
        metadata = b"CAM2" + (2).to_bytes(4, "big") + (7).to_bytes(4, "big")
        metadata += (320).to_bytes(2, "big") + (240).to_bytes(2, "big")
        metadata += (4096).to_bytes(4, "big")
        self.assertEqual(
            parse_camera_metadata(metadata),
            CameraMetadata(2, 7, 320, 240, 4096),
        )

    def test_rekey_status_parser(self) -> None:
        self.assertEqual(
            parse_aes_status("OK epoch=2 count=10 rekey=1"),
            AesStatus(epoch=2, count=10, rekey=True),
        )
        with self.assertRaises(ProtocolError):
            parse_aes_status("OK epoch=2 count=10")

    def test_memory_status_parser_and_formatter(self) -> None:
        status = parse_memory_status(
            "MEM internal_free=172032 internal_min=153600 "
            "internal_largest=131072 psram_free=7602176 "
            "psram_min=7340032 psram_largest=7471104 pqc_stack_min=55296"
        )
        self.assertEqual(
            status,
            MemoryStatus(
                internal_free=172032,
                internal_min=153600,
                internal_largest=131072,
                psram_free=7602176,
                psram_min=7340032,
                psram_largest=7471104,
                pqc_stack_min=55296,
            ),
        )
        formatted = format_memory_status(status)
        self.assertIn("Internal free=168.0 KiB", formatted)
        self.assertIn("PQC stack minimum=54.0 KiB", formatted)

        with self.assertRaises(ProtocolError):
            parse_memory_status("MEM internal_free=100")

    def test_ml_kem_768_roundtrip(self) -> None:
        public_key, ciphertext, sender_secret, receiver_secret = kem_roundtrip()
        self.assertEqual(len(public_key), 1184)
        self.assertEqual(len(ciphertext), 1088)
        self.assertEqual(len(sender_secret), 32)
        self.assertEqual(sender_secret, receiver_secret)

    def test_aes_256_gcm_roundtrip_and_tamper_rejection(self) -> None:
        key = os.urandom(32)
        nonce = os.urandom(12)
        aad = b"esp32-only/test/v1"
        plaintext = b"AES-GCM cross-platform test"
        ciphertext, tag = aes_encrypt(key, nonce, plaintext, aad)

        self.assertEqual(
            aes_decrypt(key, nonce, ciphertext, tag, aad), plaintext
        )

        bad_tag = bytearray(tag)
        bad_tag[-1] ^= 1
        with self.assertRaises(InvalidTag):
            aes_decrypt(key, nonce, ciphertext, bytes(bad_tag), aad)

    def test_ml_dsa_44_roundtrip_and_tamper_rejection(self) -> None:
        message = b"ML-DSA-44 host self-test"
        public_key, signature, verified = dsa_roundtrip(message)
        self.assertEqual(len(public_key), 1312)
        self.assertEqual(len(signature), 2420)
        self.assertTrue(verified)
        self.assertFalse(verify_dsa(public_key, message + b"!", signature))


if __name__ == "__main__":
    unittest.main()
