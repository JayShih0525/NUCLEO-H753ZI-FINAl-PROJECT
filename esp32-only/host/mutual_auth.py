"""v6 mutual ML-DSA authentication, transcript-bound KEM, bilateral key proof."""
import hashlib
import hmac
import os
import struct
import time
from pqcrypto.kem import ml_kem_768
from pqcrypto.sign import ml_dsa_44
from host.device_auth import protocol_trusted_key
from host.host_identity import load_identity
from host.serial_protocol import ProtocolError

DEVICE = b'esp32-only/mutual-device/v1\0'
HOST = b'esp32-only/mutual-host/v1\0'
HOST_KEY = b'esp32-only/mutual-host-key/v1\0'
DEVICE_KEY = b'esp32-only/mutual-device-key/v1\0'


def context(host_pk, device_pk, nonce, device_nonce, kem_pk, limit):
    if (len(host_pk), len(device_pk), len(nonce), len(device_nonce), len(kem_pk)) != (1312, 1312, 32, 32, 1184):
        raise ProtocolError('Invalid mutual authentication field lengths')
    if not 1 <= limit <= 100000:
        raise ProtocolError('Invalid mutual authentication rekey limit')
    return (hashlib.sha256(host_pk).digest() + hashlib.sha256(device_pk).digest() +
            nonce + device_nonce + kem_pk + struct.pack('>I', limit))


def establish(protocol):
    started = time.perf_counter()
    try:
        host_pk, host_sk = load_identity()
    except (OSError, ValueError, KeyError) as error:
        raise ProtocolError('Host identity unavailable or invalid; run python -m host.host_identity') from error
    trusted = protocol_trusted_key(protocol)
    nonce = os.urandom(32)
    limit = getattr(protocol, 'rekey_interval', 10)
    protocol.send_command_frame('MUTUAL_BEGIN', hashlib.sha256(host_pk).digest() + nonce + struct.pack('>I', limit))
    protocol.expect('OK')
    device_nonce = protocol.receive_frame(32)
    kem_pk = protocol.receive_frame(1184)
    signature = protocol.receive_frame(2420)
    transcript = context(host_pk, trusted, nonce, device_nonce, kem_pk, limit)
    if not ml_dsa_44.verify(trusted, DEVICE + transcript, signature):
        raise ProtocolError('Mutual authentication: device proof rejected')
    ct, key = ml_kem_768.encrypt(kem_pk)
    binding = transcript + ct
    signature = ml_dsa_44.sign(host_sk, HOST + binding)
    proof = hmac.digest(key, HOST_KEY + binding, 'sha256')
    protocol.send_command_frame('MUTUAL_FINISH', ct + signature + proof)
    result = protocol.expect_prefix('KEM_OK ')
    try:
        fields = dict(part.split('=', 1) for part in result.split()[1:])
        epoch = int(fields['epoch'])
        if not 1 <= epoch <= 0xffffffff or int(fields['limit']) != limit:
            raise ValueError()
    except (ValueError, KeyError) as error:
        raise ProtocolError('Invalid mutual session response') from error
    actual = protocol.receive_frame(32)
    expected = hmac.digest(key, DEVICE_KEY + binding + struct.pack('>I', epoch), 'sha256')
    if not hmac.compare_digest(expected, actual):
        raise ProtocolError('Mutual authentication: device session proof rejected')
    elapsed = (time.perf_counter() - started) * 1000
    timings = getattr(protocol, '_handshake_timings', None)
    if isinstance(timings, dict):
        timings['mutual_auth_ms'] = elapsed
    protocol.trace('mutual_auth_verified', host_fingerprint=hashlib.sha256(host_pk).hexdigest(),
                   device_fingerprint=hashlib.sha256(trusted).hexdigest(), epoch=epoch, elapsed_ms=elapsed)
    print(f'[PASS] Mutual ML-DSA identity and bilateral session-key proof verified: epoch={epoch} elapsed_ms={elapsed:.3f}')
    return kem_pk, ct, key, epoch
