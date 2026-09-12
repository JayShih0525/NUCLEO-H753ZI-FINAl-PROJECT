"""Explicit enrollment, using a fingerprint checked over a trusted local path."""
import argparse
import time
import serial

from host.device_auth import TRUST_FILE, check_enrollment_key, fingerprint
from host.serial_protocol import SerialProtocol

# upload in arduino IDE and press the reset button will get the fingerprint
# python -m host.enroll_device --port COM3 --baud 921600 --fingerprint "指紋"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--baud', type=int, default=921600)
    parser.add_argument('--fingerprint', required=True,
                        help='64 hex characters from the physically trusted device boot log')
    args = parser.parse_args()
    if TRUST_FILE.exists():
        parser.error(f'Trust already exists at {TRUST_FILE}; refusing to replace it automatically')
    with serial.Serial(args.port, args.baud, timeout=5, write_timeout=10) as port:
        port.dtr = False
        port.rts = False
        time.sleep(3)
        port.reset_input_buffer()
        protocol = SerialProtocol(port)
        protocol.send_line('GET_DSA_PUBLIC_KEY')
        protocol.expect('OK')
        key = protocol.receive_frame(1312)
    check_enrollment_key(key, args.fingerprint)
    with TRUST_FILE.open('xb') as output:
        output.write(key)
    print(f'[PASS] Trusted device enrolled: {fingerprint(key)} -> {TRUST_FILE}')


if __name__ == '__main__':
    main()
