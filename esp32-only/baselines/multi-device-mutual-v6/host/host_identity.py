"""Create a local Host ML-DSA identity and firmware public-key allowlist."""
import argparse
import hashlib
import json
from pathlib import Path
from pqcrypto.sign import ml_dsa_44

ROOT = Path(__file__).resolve().parent.parent
IDENTITY = ROOT / '.host-identity' / 'identity.json'
HEADER = ROOT / 'firmware' / 'esp32_pqc_demo' / 'host_trust.h'


def load_identity(path=IDENTITY):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    pk, sk = bytes.fromhex(data['public_key']), bytes.fromhex(data['secret_key'])
    if len(pk) != 1312 or len(sk) != 2560:
        raise ValueError('Invalid Host ML-DSA-44 key sizes')
    message = b'esp32-only/host-key-pair-check/v1'
    if not ml_dsa_44.verify(pk, message, ml_dsa_44.sign(sk, message)):
        raise ValueError('Host identity key pair does not match')
    return pk, sk


def main(argv=None):
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
    keys = [pk] + [path.read_bytes() for path in args.additional_public_key]
    if not 1 <= len(keys) <= 4 or any(len(key) != 1312 for key in keys) or len(set(keys)) != len(keys):
        parser.error('Allowlist requires 1..4 distinct 1312-byte public keys')
    rows = ['{' + ','.join(f'0x{byte:02x}' for byte in key) + '}' for key in keys]
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text('#pragma once\n#include <stdint.h>\n'
                          f'constexpr unsigned int HOST_TRUST_COUNT = {len(keys)};\n'
                          'static const uint8_t HOST_TRUST_KEYS[][1312] = {\n' + ',\n'.join(rows) + '\n};\n', encoding='utf-8')
    args.identity.with_suffix('.pub').write_bytes(pk)
    print(f'Host fingerprint: {hashlib.sha256(pk).hexdigest()}')
    print(f'Identity retained at: {args.identity.resolve()} (private; do not share)')
    print(f'Firmware allowlist: {args.header.resolve()}; upload to each ESP32')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
