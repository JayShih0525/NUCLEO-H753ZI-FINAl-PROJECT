from __future__ import annotations

from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from pqcrypto.kem import ml_kem_768
from pqcrypto.sign import ml_dsa_44


def kem_roundtrip() -> tuple[bytes, bytes, bytes, bytes]:
    """Run a complete host-only ML-KEM-768 round trip."""
    public_key, secret_key = ml_kem_768.generate_keypair()
    ciphertext, sender_secret = ml_kem_768.encrypt(public_key)
    receiver_secret = ml_kem_768.decrypt(secret_key, ciphertext)
    return public_key, ciphertext, sender_secret, receiver_secret


def aes_encrypt(
    key: bytes, nonce: bytes, plaintext: bytes, aad: bytes
) -> tuple[bytes, bytes]:
    encrypted = AESGCM(key).encrypt(nonce, plaintext, aad)
    return encrypted[:-16], encrypted[-16:]


def aes_decrypt(
    key: bytes, nonce: bytes, ciphertext: bytes, tag: bytes, aad: bytes
) -> bytes:
    return AESGCM(key).decrypt(nonce, ciphertext + tag, aad)


def dsa_roundtrip(message: bytes) -> tuple[bytes, bytes, bool]:
    """Run a complete host-only ML-DSA-44 round trip."""
    public_key, secret_key = ml_dsa_44.generate_keypair()
    signature = ml_dsa_44.sign(secret_key, message)
    return public_key, signature, verify_dsa(public_key, message, signature)


def verify_dsa(public_key: bytes, message: bytes, signature: bytes) -> bool:
    try:
        return bool(ml_dsa_44.verify(public_key, message, signature))
    except Exception:
        return False
