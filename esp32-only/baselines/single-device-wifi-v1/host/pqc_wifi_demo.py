"""Single-device authenticated TCP encrypted message echo; no plaintext on wire."""
import argparse
import os
import uuid
import json
import traceback
from datetime import datetime
from pathlib import Path

from host.crypto_ops import aes_encrypt, aes_decrypt
from host.device_auth import load_trusted_key
from host.pqc_host_demo import establish_session, parse_aes_status, request_info
from host.serial_protocol import SerialProtocol, ProtocolError
from host.tcp_connection import TcpConnection


def exchange(protocol, key, epoch, index, message, count, limit):
    aad = b'esp32-only/wifi-echo/v1\0' + epoch.to_bytes(4, 'big') + index.to_bytes(4, 'big')
    nonce = os.urandom(12)
    ciphertext, tag = aes_encrypt(key, nonce, message, aad)
    protocol.send_line('AES_ECHO')
    protocol.expect('READY')
    for part in (nonce, aad, ciphertext, tag):
        protocol.send_frame(part)
    status = parse_aes_status(protocol.expect_prefix('OK '))
    reply_nonce = protocol.receive_frame(12)
    reply = protocol.receive_frame(4096)
    reply_tag = protocol.receive_frame(16)
    if len(reply_nonce) != 12 or len(reply_tag) != 16 or reply_nonce == nonce:
        raise ProtocolError('Invalid or reflected echo response')
    recovered = aes_decrypt(key, reply_nonce, reply, reply_tag, aad)
    if recovered != message:
        raise ProtocolError('Echo plaintext mismatch')
    if status.epoch != epoch or status.count != count or status.rekey != (count == limit):
        raise ProtocolError('Echo session status mismatch')
    return status

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='ESP32 IP shown on Serial Monitor')
    parser.add_argument('--tcp-port', type=int, default=9000)
    parser.add_argument('--trust-key', type=Path, help='trusted device .pub file')
    parser.add_argument('--message', default='Hello encrypted WiFi')
    parser.add_argument('--exchange-count', type=int, default=20)
    parser.add_argument('--rekey-every', type=int, default=10)
    parser.add_argument('--diagnostics', type=Path)
    args = parser.parse_args()
    message = args.message.encode('utf-8')
    if not 1 <= len(message) <= 4096 or not 1 <= args.exchange_count <= 100000:
        parser.error('message must be 1..4096 UTF-8 bytes; exchange-count 1..100000')
    if not 1 <= args.rekey_every <= 100000 or not 1 <= args.tcp_port <= 65535:
        parser.error('invalid rekey interval or TCP port')
    trusted_key = load_trusted_key(args.trust_key)
    trace_path = args.diagnostics or Path('diagnostics') / ('wifi_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.jsonl')
    trace_path.parent.mkdir(parents=True, exist_ok=True)
    print(f'[DIAG] {trace_path.resolve()}')
    with trace_path.open('x', encoding='utf-8') as trace_file:
        run_id = uuid.uuid4().hex
        def record(event):
            trace_file.write(json.dumps(dict(event, run_id=run_id, wall_time=datetime.now().astimezone().isoformat())) + '\n')
            trace_file.flush()
        record(dict(event='wifi_run_start', host=args.host, port=args.tcp_port))
        try:
            with TcpConnection(args.host, args.tcp_port, diagnostic=record) as connection:
                protocol = SerialProtocol(connection, diagnostic=record)
                protocol.trusted_key = trusted_key
                connection.diagnostic = lambda event: protocol.trace(event['event'], **{k:v for k,v in event.items() if k != 'event'})
                print(request_info(protocol))
                protocol.send_line(f'SET_REKEY_INTERVAL {args.rekey_every}')
                protocol.expect_prefix('OK rekey_every=')
                public_key, _, key, epoch = establish_session(protocol)
                count = 0
                for index in range(1, args.exchange_count + 1):
                    protocol.context = dict(message_index=index, expected_epoch=epoch)
                    count += 1
                    status = exchange(protocol, key, epoch, index, message, count, args.rekey_every)
                    protocol.trace('wifi_echo_verified', message_index_verified=index)
                    print(f'[PASS] encrypted WiFi echo #{index} bytes={len(message)} epoch={epoch} count={count}')
                    if status.rekey and index < args.exchange_count:
                        new_public, _, new_key, new_epoch = establish_session(protocol)
                        if new_public == public_key or new_key == key or new_epoch != epoch + 1:
                            raise ProtocolError('Invalid WiFi rekey')
                        public_key, key, epoch, count = new_public, new_key, new_epoch, 0
                protocol.send_line('RESET_SESSION')
                protocol.expect('OK')
            record(dict(event='run_complete'))
        except BaseException:
            record(dict(event='run_error', traceback=traceback.format_exc()))
            raise
    print('Authenticated WiFi encrypted message test passed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
