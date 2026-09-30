"""Diagnostics only: call between transactions, never during binary reception."""
import time


def capture_startup(protocol, duration=3.0, maximum=65536):
    port = protocol.serial_port
    previous_timeout = port.timeout
    captured = bytearray()
    deadline = time.monotonic() + duration
    try:
        port.timeout = 0.1
        while time.monotonic() < deadline and len(captured) < maximum:
            captured.extend(port.read(min(4096, maximum - len(captured))))
    finally:
        port.timeout = previous_timeout
        # Boot ROM may use a different baud; preserve its bytes for diagnosis.
        protocol.trace('startup_capture', bytes_received=len(captured),
                       limit_reached=len(captured) >= maximum,
                       raw_hex=captured.hex(),
                       text=captured.decode('utf-8', errors='replace'))


def request_snapshot(protocol, stage):
    snapshot = {}
    for command, prefix in [('BOOT_INFO', 'BOOT '),
                            ('CAMERA_INFO', 'CAMERA '), ('TX_INFO', 'TX ')]:
        protocol.send_line(command)
        line = protocol.expect_prefix(prefix)
        fields = dict(item.split('=', 1) for item in line.split()[1:] if '=' in item)
        snapshot[command] = fields
        print(f'[DIAG {stage}] {line}')
    protocol.trace('device_snapshot', snapshot_stage=stage, snapshot=snapshot)
    return snapshot
