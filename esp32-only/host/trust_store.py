"""Local trust policy and strict firmware allowlist serialization. No network I/O."""
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

ROOT = Path(__file__).resolve().parent.parent
REVOCATIONS = ROOT / '.trust' / 'revoked_devices.json'


def fingerprint(key):
    if len(key) != 1312:
        raise ValueError('Expected a 1312-byte ML-DSA-44 public key')
    return hashlib.sha256(key).hexdigest()


def checked_fingerprint(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-fA-F]{64}', value):
        raise ValueError('Fingerprint must be 64 hexadecimal characters')
    return value.lower()


def checked_key(path, expected):
    key = Path(path).read_bytes()
    if fingerprint(key) != checked_fingerprint(expected):
        raise ValueError('Fingerprint mismatch; no trust changes made')
    return key


def atomic_write(path, data):
    """Replace one file atomically; leave the previous content on write failure."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def write_json(path, data):
    atomic_write(path, (json.dumps(data, indent=2, ensure_ascii=False) + '\n').encode('utf-8'))


def revoked_devices(path=None):
    path = Path(path) if path is not None else REVOCATIONS
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except FileNotFoundError:
        return set()
    if (not isinstance(data, dict) or set(data) != {'version', 'fingerprints'} or
            type(data['version']) is not int or data['version'] != 1 or not isinstance(data['fingerprints'], list)):
        raise ValueError('Invalid device revocation policy; refusing authentication')
    return {checked_fingerprint(value) for value in data['fingerprints']}


def require_not_revoked(key):
    if fingerprint(key) in revoked_devices():
        raise ValueError('Device identity is revoked on this Host; explicit restore required')


def set_revoked(value, revoked, path=None):
    value = checked_fingerprint(value)
    keys = revoked_devices(path)
    if revoked:
        keys.add(value)
    else:
        if value not in keys:
            raise ValueError('Device is not revoked')
        keys.remove(value)
    write_json(path or REVOCATIONS, dict(version=1, fingerprints=sorted(keys)))


def validate_host_keys(keys):
    if not 1 <= len(keys) <= 4:
        raise ValueError('Firmware requires 1..4 Host keys; cannot remove the last key')
    for key in keys:
        fingerprint(key)
    if len(set(keys)) != len(keys):
        raise ValueError('Duplicate Host identity')


def read_host_keys(path):
    # Only accept our generated header grammar; never eval arbitrary C++.
    text = Path(path).read_text(encoding='utf-8-sig')
    match = re.fullmatch(r'\s*#pragma once\s+#include <stdint.h>\s+'
        r'constexpr unsigned int HOST_TRUST_COUNT = (\d+);\s+'
        r'static const uint8_t HOST_TRUST_KEYS\[\]\[1312\] = \{(.*?)\};\s*', text, re.S)
    if not match:
        raise ValueError('Unsupported host_trust.h format; refusing to overwrite')
    body = match[2]
    rows = re.findall(r'\{([^{}]*)\}', body)
    remainder = re.sub(r'\{[^{}]*\}', '', body)
    if remainder.strip().strip(',').strip():
        raise ValueError('Invalid Host key rows')
    keys = []
    for row in rows:
        tokens = row.split(',')
        if any(not re.fullmatch(r'\s*0x[0-9a-fA-F]{2}\s*', token) for token in tokens):
            raise ValueError('Invalid Host public key bytes')
        keys.append(bytes(int(token.strip(), 16) for token in tokens))
    validate_host_keys(keys)
    if len(keys) != int(match[1]):
        raise ValueError('Host key count mismatch')
    return keys


def write_host_keys(path, keys):
    validate_host_keys(keys)
    rows = ['{' + ','.join(f'0x{byte:02x}' for byte in key) + '}' for key in keys]
    text = ('#pragma once\n#include <stdint.h>\n'
            f'constexpr unsigned int HOST_TRUST_COUNT = {len(keys)};\n'
            'static const uint8_t HOST_TRUST_KEYS[][1312] = {\n' + ',\n'.join(rows) + '\n};\n')
    atomic_write(path, text.encode('utf-8'))
