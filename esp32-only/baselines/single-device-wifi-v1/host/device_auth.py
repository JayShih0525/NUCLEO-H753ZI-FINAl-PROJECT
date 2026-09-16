"""Pinned device identity and fresh ML-DSA proof for each KEM exchange."""
from __future__ import annotations

import hashlib
import hmac
import os
from pathlib import Path

from pqcrypto.sign import ml_dsa_44
from host.serial_protocol import ProtocolError

DOMAIN = b'esp32-only/auth-kem/v1\x00'
CONFIRM_DOMAIN = b'esp32-only/confirm/v1\x00'
TRUST_FILE = Path(__file__).resolve().parent / 'trusted_device.pub'


def fingerprint(key: bytes) -> str:
    return hashlib.sha256(key).hexdigest()


def load_trusted_key(path=None) -> bytes:
    try:
        key = (Path(path) if path is not None else TRUST_FILE).read_bytes()
    except FileNotFoundError as error:
        raise ProtocolError('No trusted device key. Run python -m host.enroll_device first; see AUTHENTICATION.md.') from error
    if len(key) != 1312:
        raise ProtocolError('Trusted ML-DSA key must contain exactly 1312 bytes')
    return key


def protocol_trusted_key(protocol) -> bytes:
    key = getattr(protocol, 'trusted_key', None)
    return key if isinstance(key, bytes) else load_trusted_key()


def proof_message(challenge: bytes, kem_key: bytes) -> bytes:
    if len(challenge) != 32 or len(kem_key) != 1184:
        raise ProtocolError('Invalid authentication challenge or KEM key size')
    return DOMAIN + challenge + kem_key


def verify_proof(trusted_key: bytes, challenge: bytes, kem_key: bytes, signature: bytes) -> None:
    message = proof_message(challenge, kem_key)
    if len(trusted_key) != 1312 or len(signature) != 2420:
        raise ProtocolError('Invalid device authentication key/signature size')
    try:
        valid = ml_dsa_44.verify(trusted_key, message, signature)
    except ValueError as error:
        raise ProtocolError('Malformed device authentication proof') from error
    if not valid:
        raise ProtocolError('Device authentication rejected: identity, challenge or KEM key does not match')


def authenticated_kem_key(protocol) -> bytes:
    trusted = protocol_trusted_key(protocol)
    challenge = os.urandom(32)
    protocol.send_line('AUTH_KEM')
    protocol.expect('READY')
    protocol.send_frame(challenge)
    protocol.expect('OK')
    kem_key = protocol.receive_frame(1184)
    signature = protocol.receive_frame(2420)
    verify_proof(trusted, challenge, kem_key, signature)
    print(f'[PASS] Device identity and fresh KEM proof verified: {fingerprint(trusted)}')
    return kem_key


def check_enrollment_key(key: bytes, expected_fingerprint: str) -> None:
    if len(key) != 1312 or not hmac.compare_digest(fingerprint(key), expected_fingerprint.lower()):
        raise ProtocolError('Enrollment fingerprint mismatch; trusted key was not saved')


def confirmation_message(challenge, kem_key, ciphertext, epoch):
    if len(challenge) != 32 or len(kem_key) != 1184 or len(ciphertext) != 1088 or not 1 <= epoch <= 0xffffffff:
        raise ProtocolError('Invalid session confirmation fields')
    return CONFIRM_DOMAIN + challenge + kem_key + ciphertext + epoch.to_bytes(4, 'big')


def confirm_session(protocol, shared_secret, kem_key, ciphertext, epoch):
    challenge = os.urandom(32)
    message = confirmation_message(challenge, kem_key, ciphertext, epoch)
    protocol.send_line('CONFIRM_SESSION')
    protocol.expect('READY')
    protocol.send_frame(challenge)
    protocol.expect('OK')
    actual = protocol.receive_frame(32)
    expected = hmac.digest(shared_secret, message, 'sha256')
    if not hmac.compare_digest(expected, actual):
        raise ProtocolError('Session key confirmation failed')
    print(f'[PASS] Device possession of session key confirmed: epoch={epoch}')
