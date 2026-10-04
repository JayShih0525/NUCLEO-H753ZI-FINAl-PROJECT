"""Create a local Host ML-DSA identity and firmware public-key allowlist."""
import argparse
import hashlib
import json
from pathlib import Path
from pqcrypto.sign import ml_dsa_44
from host.trust_store import read_host_keys, write_host_keys
from host.live_logging import InstanceLock

ROOT = Path(__file__).resolve().parent.parent
IDENTITY = ROOT / '.host-identity' / 'identity.json'
HEADER = ROOT / 'firmware' / 'esp32_pqc_demo' / 'host_trust.h'

#python -m host.host_identity --additional-public-key laptop2.pub --additional-public-key laptop3.pub

def load_identity(path=IDENTITY):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    pk, sk = bytes.fromhex(data['public_key']), bytes.fromhex(data['secret_key'])
    if len(pk) != 1312 or len(sk) != 2560:
        raise ValueError('Invalid Host ML-DSA-44 key sizes')
    message = b'esp32-only/host-key-pair-check/v1'
    if not ml_dsa_44.verify(pk, message, ml_dsa_44.sign(sk, message)):
        raise ValueError('Host identity key pair does not match')
    return pk, sk


def _main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--identity', type=Path, default=IDENTITY)
    parser.add_argument('--header', type=Path, default=HEADER)
    parser.add_argument('--additional-public-key', type=Path, action='append', default=[])
    args = parser.parse_args(argv)
    if not args.identity.exists():
        pk, sk = ml_dsa_44.generate_keypair()
        args.identity.parent.mkdir(parents=True, exist_ok=True)
        with args.identity.open('x', encoding='utf-8') as out:
            json.dump(dict(public_key=pk.hex(), secret_key=sk.hex()), out)
    pk, _ = load_identity(args.identity)
    # Rerunning setup must not silently delete other trusted Hosts or re-add a
    # previously removed local identity. Explicit management handles changes.
    keys = read_host_keys(args.header) if args.header.exists() else [pk]
    if pk not in keys:
        parser.error('Existing allowlist excludes this Host. Use trust_manage host-add explicitly; refusing to restore it silently.')
    for path in args.additional_public_key:
        additional = path.read_bytes()
        if additional not in keys:
            keys.append(additional)
    if not 1 <= len(keys) <= 4 or any(len(key) != 1312 for key in keys) or len(set(keys)) != len(keys):
        parser.error('Allowlist requires 1..4 distinct 1312-byte public keys')
    write_host_keys(args.header, keys)
    args.identity.with_suffix('.pub').write_bytes(pk)
    print(f'Host fingerprint: {hashlib.sha256(pk).hexdigest()}')
    print(f'Identity retained at: {args.identity.resolve()} (private; do not share)')
    print(f'Firmware allowlist: {args.header.resolve()}; upload to each ESP32')
    return 0


def main(argv=None):
    with InstanceLock(ROOT / '.trust' / 'management.lock', 'Another trust management command is running'):
        return _main(argv)


if __name__ == '__main__':
    raise SystemExit(main())
