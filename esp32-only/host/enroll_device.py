"""Explicit enrollment, using a fingerprint checked over a trusted local path."""
import argparse
import time
import serial
from pathlib import Path
from host.tcp_connection import TcpConnection

from host.device_auth import TRUST_FILE, check_enrollment_key, fingerprint
from host.serial_protocol import SerialProtocol

# upload in arduino IDE and press the reset button will get the fingerprint
# python -m host.enroll_device --host ip --output host/camera2.pub --fingerprint ""

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--baud', type=int, default=921600)
    parser.add_argument('--host', help='ESP32 IP; use TCP for WiFi firmware')
    parser.add_argument('--tcp-port', type=int, default=9000)
    parser.add_argument('--output', type=Path, default=TRUST_FILE,
                        help='public key file; existing files are never overwritten')
    parser.add_argument('--fingerprint', required=True,
                        help='64 hex characters from the physically trusted device boot log')
    args = parser.parse_args()
    if len(args.fingerprint) != 64 or any(c not in '0123456789abcdefABCDEF' for c in args.fingerprint):
        parser.error('fingerprint must contain exactly 64 hexadecimal characters')
    if not 1 <= args.tcp_port <= 65535:
        parser.error('tcp-port must be between 1 and 65535')
    if args.output.exists():
        parser.error(f'Trust already exists at {args.output}; refusing to replace it automatically')
    connection = (TcpConnection(args.host, args.tcp_port) if args.host else
                  serial.Serial(args.port, args.baud, timeout=5, write_timeout=10))
    with connection as port:
        if not args.host:
            port.dtr = False
            port.rts = False
            time.sleep(3)
            port.reset_input_buffer()
        protocol = SerialProtocol(port)
        if args.host:
            # Consume the optional boot READY line before the binary transaction.
            protocol.send_line('INFO')
            protocol.read_until_prefix('INFO ', timeout=10.0)
        protocol.send_line('GET_DSA_PUBLIC_KEY')
        protocol.expect('OK')
        key = protocol.receive_frame(1312)
    check_enrollment_key(key, args.fingerprint)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('xb') as output:
        output.write(key)
    print(f'[PASS] Trusted device enrolled: {fingerprint(key)} -> {args.output.resolve()}')


if __name__ == '__main__':
    main()
