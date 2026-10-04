"""Manage local device trust and the firmware Host allowlist. Stop streams first."""
import argparse
import json
from pathlib import Path
import os
import re

from host import trust_store as store
from host.host_identity import HEADER
from host.live_logging import InstanceLock


def config_data(path):
    data = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    if not isinstance(data, dict) or set(data) != {'devices'} or not isinstance(data['devices'], list):
        raise ValueError('Expected a devices list')
    if any(not isinstance(item, dict) for item in data['devices']):
        raise ValueError('Invalid device entry')
    return data


def device_add(path, name, public_key, expected):
    if (not re.fullmatch('[A-Za-z0-9][A-Za-z0-9_-]{0,47}', name) or
            name.upper() in {'CON', 'PRN', 'AUX', 'NUL'} | {f'{p}{n}' for p in ('COM','LPT') for n in range(1,10)}):
        raise ValueError('Invalid or reserved device name')
    path, public_key = Path(path).resolve(), Path(public_key).resolve()
    key = store.checked_key(public_key, expected)
    store.require_not_revoked(key)
    data = config_data(path) if path.exists() else {'devices': []}
    for item in data['devices']:
        if str(item.get('name', '')).casefold() == name.casefold():
            raise ValueError('Device name already exists; no replacement performed')
        existing = (path.parent / item['trust_key']).read_bytes()
        if store.fingerprint(existing) == store.fingerprint(key):
            raise ValueError('Device identity already exists under another name')
    data['devices'].append(dict(name=name, host='auto', port=9000,
        trust_key=os.path.relpath(public_key, path.parent).replace('\\', '/'), enabled=True))
    store.write_json(path, data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=store.ROOT/'devices.json')
    parser.add_argument('--header', type=Path, default=HEADER)
    commands = parser.add_subparsers(dest='action', required=True)
    commands.add_parser('device-list')
    add = commands.add_parser('device-add')
    add.add_argument('--name', required=True)
    add.add_argument('--public-key', type=Path, required=True)
    add.add_argument('--fingerprint', required=True)
    revoke = commands.add_parser('device-revoke')
    revoke.add_argument('--fingerprint', required=True)
    restore = commands.add_parser('device-restore')
    restore.add_argument('--public-key', type=Path, required=True)
    restore.add_argument('--fingerprint', required=True)
    commands.add_parser('host-list')
    add = commands.add_parser('host-add')
    add.add_argument('--public-key', type=Path, required=True)
    add.add_argument('--fingerprint', required=True)
    revoke = commands.add_parser('host-revoke')
    revoke.add_argument('--fingerprint', required=True)
    args = parser.parse_args(argv)
    try:
        # Serialize management writes; no connection is made by this tool.
        with InstanceLock(store.ROOT/'.trust'/'management.lock', 'Another trust management command is running'):
            if args.action == 'device-list':
                revoked = store.revoked_devices()
                rows = []
                for item in config_data(args.config)['devices']:
                    row = dict(name=item.get('name'), enabled=item.get('enabled', True), trust_key=item.get('trust_key'))
                    try:
                        fp = store.fingerprint((args.config.resolve().parent/item['trust_key']).read_bytes())
                        row.update(fingerprint=fp, revoked=fp in revoked)
                    except (OSError, ValueError, KeyError, TypeError) as error:
                        row['error'] = str(error)
                    rows.append(row)
                print(json.dumps(dict(devices=rows, revoked_fingerprints=sorted(revoked)), indent=2))
            elif args.action == 'device-add':
                device_add(args.config, args.name, args.public_key, args.fingerprint)
                print('Device added. Restart Host to load the updated configuration.')
            elif args.action == 'device-revoke':
                store.set_revoked(args.fingerprint, True)
                print('Device revoked for subsequent authentication, including renamed/copied public keys. Stop existing streams.')
            elif args.action == 'device-restore':
                store.checked_key(args.public_key, args.fingerprint)
                store.set_revoked(args.fingerprint, False)
                print('Device trust restored explicitly. Existing configuration and public keys were retained.')
            else:
                keys = store.read_host_keys(args.header)
                if args.action == 'host-list':
                    print(json.dumps(dict(source='local firmware header; NOT live device state',
                        hosts=[store.fingerprint(key) for key in keys]), indent=2))
                elif args.action == 'host-add':
                    keys.append(store.checked_key(args.public_key, args.fingerprint))
                    store.write_host_keys(args.header, keys)
                else:
                    fp = store.checked_fingerprint(args.fingerprint)
                    if fp not in [store.fingerprint(key) for key in keys]:
                        raise ValueError('Host identity not found; no changes made')
                    store.write_host_keys(args.header, [key for key in keys if store.fingerprint(key) != fp])
                if args.action != 'host-list':
                    print('Local Host allowlist updated. Compile/upload to each ESP32 for this change to take effect; preserve NVS.')
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        parser.error(str(error))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
