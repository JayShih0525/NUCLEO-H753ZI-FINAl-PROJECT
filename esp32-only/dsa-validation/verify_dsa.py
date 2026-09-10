"""Standalone ML-DSA-44 diagnostics; no changes to camera/session firmware."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import traceback
from datetime import datetime


def flip(data: bytes, index: int) -> bytes:
    result = bytearray(data)
    result[index] ^= 1
    return bytes(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('local', 'device'), default='local')
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--baud', type=int, default=921600)
    args = parser.parse_args()
    output = Path(__file__).resolve().parent / 'results'
    output.mkdir(exist_ok=True)
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    base = output / f'{args.mode}_{stamp}'
    report = {'mode': args.mode, 'started': stamp, 'cases': [], 'error': None}
    serial_port = None
    with base.with_suffix('.txt').open('w', encoding='utf-8') as log:
        def emit(message):
            print(message, flush=True)
            log.write(message + '\n')
            log.flush()

        try:
            from pqcrypto.sign import ml_dsa_44 as dsa
            emit(f'ML-DSA-44 validation mode={args.mode}')
            wrong_pk, wrong_sk = dsa.generate_keypair()
            if args.mode == 'local':
                pk, sk = dsa.generate_keypair()

                def sign(message):
                    return dsa.sign(sk, message)
            else:
                import serial
                # Allow direct execution from any working directory.
                sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
                from host.serial_protocol import SerialProtocol
                serial_port = serial.Serial(args.port, args.baud, timeout=5, write_timeout=10)
                serial_port.dtr = False
                serial_port.rts = False
                time.sleep(3)
                serial_port.reset_input_buffer()
                protocol = SerialProtocol(serial_port)
                protocol.send_line('INFO')
                info = protocol.read_until_prefix('INFO ', timeout=15)
                emit(info)
                report.update(port=args.port, baud=args.baud, device_info=info)
                protocol.send_line('GET_DSA_PUBLIC_KEY')
                protocol.expect('OK')
                pk = protocol.receive_frame(1312)

                def sign(message):
                    protocol.send_line('DSA_SIGN')
                    protocol.expect('READY')
                    protocol.send_frame(message)
                    status = protocol.expect_prefix('OK ')
                    signature = protocol.receive_frame(2420)
                    emit(f'[DEVICE] message_bytes={len(message)} {status}')
                    return signature

            if len(pk) != 1312:
                raise RuntimeError(f'Unexpected public key size: {len(pk)}')
            report['public_key_sha256'] = hashlib.sha256(pk).hexdigest()
            emit('Public key fingerprint: ' + report['public_key_sha256'])

            def check(name, key, message, signature, expected):
                started = time.perf_counter()
                reason = ''
                try:
                    accepted = bool(dsa.verify(key, message, signature))
                except ValueError as error:
                    # Known malformed-input rejection; unexpected errors must abort.
                    accepted = False
                    reason = f'ValueError: {error}'
                elapsed = (time.perf_counter() - started) * 1000
                passed = accepted == expected
                report['cases'].append(dict(name=name, passed=passed, expected=expected,
                                            accepted=accepted, verify_ms=elapsed, reason=reason))
                emit(f"[{'PASS' if passed else 'FAIL'}] {name}: expected={'ACCEPT' if expected else 'REJECT'} actual={'ACCEPT' if accepted else 'REJECT'} verify_ms={elapsed:.3f} {reason}")

            message = hashlib.sha256(b'esp32-only/dsa-validation/transcript/v1').digest()
            signature = sign(message)
            if len(signature) != 2420:
                raise RuntimeError(f'Unexpected signature size: {len(signature)}')
            check('original_digest', pk, message, signature, True)
            for index in (0, len(message)//2, len(message)-1):
                check(f'message_bit_flip_{index}', pk, flip(message, index), signature, False)
            check('message_appended', pk, message+b'!', signature, False)
            check('message_truncated', pk, message[:-1], signature, False)
            for index in (0, len(signature)//2, len(signature)-1):
                check(f'signature_bit_flip_{index}', pk, message, flip(signature, index), False)
            check('signature_truncated', pk, message, signature[:-1], False)
            check('signature_extended', pk, message, signature+b'\x00', False)
            check('signature_empty', pk, message, b'', False)
            check('wrong_public_key', wrong_pk, message, signature, False)
            check('public_key_truncated', pk[:-1], message, signature, False)
            check('public_key_extended', pk+b'\x00', message, signature, False)
            other_signature = dsa.sign(wrong_sk, message)
            check('other_signer_under_original_key', pk, message, other_signature, False)
            check('other_signer_with_own_key', wrong_pk, message, other_signature, True)
            check('replayed_valid_signature_still_valid', pk, message, signature, True)
            emit('[NOTE] Replay acceptance is expected: signatures alone do not provide freshness or trusted identity.')
            # Exercise the existing firmware message-size boundary without sending
            # invalid frames that could desynchronize its serial protocol.
            for size in (1, 4096):
                payload = bytes((i % 251 for i in range(size)))
                signed = sign(payload)
                if len(signed) != 2420:
                    raise RuntimeError('Unexpected signature size')
                check(f'valid_message_{size}_bytes', pk, payload, signed, True)
            failed = sum(not case['passed'] for case in report['cases'])
            report['passed'] = failed == 0
            emit(f"SUMMARY: {len(report['cases'])-failed}/{len(report['cases'])} PASS; mode={args.mode}")
            emit('[SCOPE] Verification is on PC. Device mode tests ESP32 signing, not ESP32 verification or certificate authentication.')
        except Exception:
            report['error'] = traceback.format_exc()
            report['passed'] = False
            emit('[ERROR] ' + report['error'])
        finally:
            if serial_port is not None:
                serial_port.close()
            base.with_suffix('.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
            emit('Results: ' + str(base.with_suffix('.txt')))
    return 0 if report.get('passed') else 1


if __name__ == '__main__':
    raise SystemExit(main())
