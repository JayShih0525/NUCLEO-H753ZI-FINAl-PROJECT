"""Find advertised cameras. Discovery metadata is untrusted, never enrollment."""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass, replace
import ipaddress
import json
import math
import re
import threading
import time

SERVICE = '_pqc-camera._tcp.local.'

# python -m host.discover_devices

@dataclass(frozen=True)
class Advertisement:
    name: str
    host: str
    port: int
    fingerprint: str


def parse_advertisement(info):
    if info is None or not isinstance(info.port, int) or not 1 <= info.port <= 65535:
        return []
    properties = info.properties
    fingerprint = properties.get(b'fingerprint', b'')
    if properties.get(b'discovery') != b'1' or not isinstance(fingerprint, bytes):
        return []
    if not re.fullmatch(rb'[0-9a-f]{64}', fingerprint):
        return []
    records = []
    for address in info.parsed_addresses():
        try:
            ip = ipaddress.ip_address(address)
        except ValueError:
            continue
        if ip.version != 4 or ip.is_unspecified or ip.is_multicast or ip.is_loopback or ip.is_reserved:
            continue
        records.append(Advertisement(info.name, str(ip), info.port, fingerprint.decode('ascii')))
    return records


def discover(timeout=5.0):
    """Bounded IPv4 mDNS browse; no TCP connection, no camera commands."""
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('discovery timeout must be positive and finite')
    try:
        from zeroconf import IPVersion, ServiceBrowser, ServiceListener, Zeroconf
    except ImportError as error:
        raise ValueError('Install discovery dependency: python -m pip install zeroconf==0.151.3') from error

    class Listener(ServiceListener):
        def __init__(self):
            self.names = set()
            self.lock = threading.Lock()

        def add_service(self, zc, kind, name):
            with self.lock:
                if len(self.names) < 128:
                    self.names.add(name)

        def update_service(self, zc, kind, name):
            self.add_service(zc, kind, name)

        def remove_service(self, zc, kind, name):
            with self.lock:
                self.names.discard(name)

    listener = Listener()
    zc = Zeroconf(ip_version=IPVersion.V4Only)
    browser = None
    try:
        deadline = time.monotonic() + timeout
        browser = ServiceBrowser(zc, SERVICE, listener)
        time.sleep(timeout * .6)
        with listener.lock:
            names = sorted(listener.names)
        records = []
        for name in names:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            info = zc.get_service_info(SERVICE, name, timeout=max(1, min(1000, int(remaining * 1000))))
            records.extend(parse_advertisement(info))
        return sorted(set(records), key=lambda record: (record.fingerprint, record.host, record.port))
    finally:
        if browser is not None:
            browser.cancel()
        zc.close()


def resolve_devices(devices, advertisements):
    """Match only already-pinned identities; the worker must still verify DSA."""
    resolved = []
    endpoints = set()
    for device in devices:
        matches = {(item.host, item.port) for item in advertisements
                   if item.fingerprint == device.fingerprint}
        if not matches:
            raise ValueError(f'{device.name}: not discovered; check WiFi/multicast or use --device NAME=IP')
        if len(matches) != 1:
            raise ValueError(f'{device.name}: ambiguous discovery; use a verified manual IP')
        host, port = matches.pop()
        if (host, port) in endpoints:
            raise ValueError('Multiple identities advertised at the same endpoint')
        endpoints.add((host, port))
        resolved.append(replace(device, host=host, port=port))
    return resolved


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=5.0)
    args = parser.parse_args(argv)
    try:
        records = discover(args.timeout)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(json.dumps({'authenticated': False, 'devices': [asdict(item) for item in records]}, indent=2))
    return 0 if records else 1


if __name__ == '__main__':
    raise SystemExit(main())
