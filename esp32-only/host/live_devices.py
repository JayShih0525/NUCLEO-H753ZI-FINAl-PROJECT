"""Independent live-device preflight. Never enroll or replace trust implicitly."""
import hashlib
import json
import re
from pathlib import Path
from host.multi_camera import Device
from host.trust_store import require_not_revoked


def load_live_devices(path):
    path = Path(path).resolve()
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(data, dict) or set(data) != {'devices'} or not isinstance(data['devices'], list):
        raise ValueError('Configuration must contain a devices list only')
    candidates, issues, names = [], [], {}
    for index, item in enumerate(data['devices'], 1):
        label = f'entry {index}'
        try:
            if not isinstance(item, dict):
                raise ValueError('device must be an object')
            if type(item.get('enabled', True)) is not bool:
                raise ValueError('enabled must be true or false')
            if not item.get('enabled', True):
                continue
            name = item.get('name')
            if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,47}', name):
                raise ValueError('invalid device name')
            label = f'entry {index} ({name})'
            names.setdefault(name.casefold(), []).append(index)
            if name.upper() in {'CON', 'PRN', 'AUX', 'NUL'} | {f'{p}{n}' for p in ('COM', 'LPT') for n in range(1, 10)}:
                raise ValueError('reserved device name')
            if set(item) - {'name', 'host', 'port', 'trust_key', 'enabled'}:
                raise ValueError('unknown device fields')
            port = item.get('port', 9000)
            if type(port) is not int or not 1 <= port <= 65535:
                raise ValueError('invalid port')
            host = item.get('host', 'auto')
            if not isinstance(host, str) or not host or any(c.isspace() for c in host):
                raise ValueError('invalid host')
            key_path = item.get('trust_key')
            if not isinstance(key_path, str) or not key_path:
                raise ValueError('trust_key is required')
            key_path = (path.parent / key_path).resolve()
            key = key_path.read_bytes()
            if len(key) != 1312:
                raise ValueError('expected a 1312-byte ML-DSA public key')
            require_not_revoked(key)
            device = Device(name, 'auto', port, key_path, hashlib.sha256(key).hexdigest())
            candidates.append((index, device, key))
        except (ValueError, OSError) as error:
            issues.append(f'{label}: {error}')
    identities = {}
    for index, device, key in candidates:
        identities.setdefault(device.fingerprint, []).append(index)
    accepted = []
    for index, device, key in candidates:
        if len(names[device.name.casefold()]) > 1 or len(identities[device.fingerprint]) > 1:
            issues.append(f'entry {index} ({device.name}): conflicting name or device identity; all conflicting entries skipped')
        else:
            accepted.append((device, key))
    return accepted, issues
