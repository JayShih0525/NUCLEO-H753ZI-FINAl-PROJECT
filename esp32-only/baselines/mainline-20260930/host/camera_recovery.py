"""Bounded recovery ONLY after a camera receive timeout on an open UART."""
import secrets
import time

from host.serial_protocol import ProtocolError


def synchronize_camera(protocol, timeout=20.0, maximum=2 * 1024 * 1024):
    token = secrets.token_hex(16)
    marker = b'\nRECOVERED ' + token.encode('ascii') + b'\n'
    port = protocol.serial_port
    previous_timeout = port.timeout
    deadline = time.monotonic() + timeout
    tail = b''
    received = 0
    try:
        port.timeout = 0.1
        # The camera command takes no host binary input. A leading newline also
        # terminates a partial command; its possible ERR is drained below.
        protocol.send_line('\nRECOVER ' + token)
        while time.monotonic() < deadline and received < maximum:
            chunk = port.read(min(4096, maximum - received))
            received += len(chunk)
            tail += chunk
            position = tail.find(marker)
            if position >= 0:
                if position + len(marker) != len(tail):
                    raise ProtocolError('Unexpected data after recovery boundary')
                protocol.trace('recovery_boundary', drained_bytes=received)
                return
            tail = tail[-(len(marker) - 1):]
        raise TimeoutError('Recovery boundary not found within time/byte limit')
    finally:
        port.timeout = previous_timeout
        # Never log discarded ciphertext, raw binary, or session keys.
        protocol.trace('recovery_drain_end', received_bytes=received)


def recover_camera(protocol, interval, establish, snapshot):
    synchronize_camera(protocol)
    state = snapshot(protocol, 'recovery')
    protocol.send_line(f'SET_REKEY_INTERVAL {interval}')
    protocol.expect_prefix('OK rekey_every=')
    protocol.send_line('CAMERA_MODE STREAM')
    protocol.expect_prefix('OK camera_mode=')
    # ACK is synchronization only, never evidence of device identity.
    session = establish(protocol)
    return session, state
