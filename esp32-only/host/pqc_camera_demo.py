from __future__ import annotations

import argparse
import hashlib
import json
import math
import traceback
from datetime import datetime
import struct
import time
from dataclasses import dataclass
from pathlib import Path

from cryptography.exceptions import InvalidTag

from host.crypto_ops import aes_decrypt
from host.device_auth import load_trusted_key
from host.pqc_host_demo import (
    AesStatus,
    add_transcript_field,
    establish_session,
    format_memory_status,
    parse_aes_status,
    request_info,
    request_memory_status,
    test_device_signature,
)
from host.serial_protocol import ProtocolError, SerialProtocol
from host.run_diagnostics import capture_startup, request_snapshot
from host.serial_connection import camera_connection
from host.tcp_connection import TcpConnection
from host.camera_replay import CameraReplayGuard
from host.camera_recovery import recover_camera
from host.live_display import UserStop


CAMERA_METADATA_SIZE = 20
CAMERA_MAGIC = b"CAM2"
MAX_JPEG_SIZE = 1024 * 1024

# remind me delete the output when this project done, too much message for test the performance

@dataclass(frozen=True)
class CameraMetadata:
    epoch: int
    frame_id: int
    width: int
    height: int
    jpeg_length: int


@dataclass(frozen=True)
class CameraResult:
    metadata: CameraMetadata
    metadata_bytes: bytes
    nonce: bytes
    ciphertext: bytes
    tag: bytes
    jpeg: bytes
    status: AesStatus


def parse_camera_metadata(data: bytes) -> CameraMetadata:
    if len(data) != CAMERA_METADATA_SIZE:
        raise ProtocolError(f"Camera metadata has {len(data)} bytes, expected 20")
    magic, epoch, frame_id, width, height, jpeg_length = struct.unpack(
        ">4sIIHHI", data
    )
    if magic != CAMERA_MAGIC:
        raise ProtocolError(f"Unexpected camera metadata magic: {magic!r}")
    if epoch < 1 or width < 1 or height < 1:
        raise ProtocolError("Camera metadata contains invalid dimensions or epoch")
    if not 4 <= jpeg_length <= MAX_JPEG_SIZE:
        raise ProtocolError(f"Invalid encrypted JPEG length: {jpeg_length}")
    return CameraMetadata(epoch, frame_id, width, height, jpeg_length)


def request_encrypted_frame(
    protocol: SerialProtocol, shared_secret: bytes, expected_epoch: int,
    replay_guard: CameraReplayGuard,
    *, inject_timeout: bool = False, pipeline=None,
) -> CameraResult:
    timing_start = time.perf_counter()
    if pipeline is None:
        protocol.send_line("CAMERA_CAPTURE_ENCRYPTED")
    else:
        shared_secret, expected_epoch = pipeline.prepare_capture(replay_guard)
    status_line = protocol.expect_prefix("OK ")
    status_at = time.perf_counter()
    status = parse_aes_status(status_line)
    protocol.trace('camera_status', status=status_line)
    metadata_bytes = protocol.receive_frame(CAMERA_METADATA_SIZE, label='metadata')
    nonce = protocol.receive_frame(12, label='nonce')
    ciphertext = protocol.receive_frame(MAX_JPEG_SIZE, label='ciphertext')
    if inject_timeout:
        protocol._read_exact(3, label='tag', phase='header')
        protocol.trace('injected_camera_timeout', received_header_bytes=3)
        raise TimeoutError('Injected timeout after 3/4 tag header bytes')
    tag = protocol.receive_frame(16, label='tag')
    extension = protocol.receive_frame(3717, label='pipeline') if pipeline is not None else None
    received_at = time.perf_counter()
    metadata = parse_camera_metadata(metadata_bytes)

    if status.epoch != expected_epoch or metadata.epoch != expected_epoch:
        raise ProtocolError(
            f"Camera frame epoch mismatch: session={expected_epoch}, "
            f"status={status.epoch}, metadata={metadata.epoch}"
        )
    if len(nonce) != 12 or len(tag) != 16:
        raise ProtocolError("Camera AES nonce or tag has the wrong size")
    if len(ciphertext) != metadata.jpeg_length:
        raise ProtocolError(
            f"Ciphertext length {len(ciphertext)} != JPEG length {metadata.jpeg_length}"
        )

    jpeg = aes_decrypt(
        shared_secret, nonce, ciphertext, tag, metadata_bytes
    )
    if not (jpeg.startswith(b"\xff\xd8") and jpeg.endswith(b"\xff\xd9")):
        raise ProtocolError("Decrypted camera data is not a complete JPEG")

    try:
        replay_guard.accept_authenticated(metadata.epoch, metadata.frame_id, status)
    except ProtocolError as error:
        protocol.trace('camera_replay_rejected', frame_id=metadata.frame_id, reason=str(error))
        raise

    if pipeline is not None:
        pipeline.accept_extension(extension)
        pipeline.frame_accepted()

    if getattr(protocol, 'profile', False):
        protocol.trace('camera_timing', frame_id=metadata.frame_id,
                       request_to_status_ms=(status_at - timing_start) * 1000,
                       receive_ms=(received_at - status_at) * 1000,
                       validate_ms=(time.perf_counter() - received_at) * 1000,
                       jpeg_bytes=len(jpeg), width=metadata.width, height=metadata.height)
    return CameraResult(
        metadata, metadata_bytes, nonce, ciphertext, tag, jpeg, status
    )


def decode_jpeg(result: CameraResult) -> np.ndarray:
    import cv2
    import numpy as np
    image = cv2.imdecode(
        np.frombuffer(result.jpeg, dtype=np.uint8), cv2.IMREAD_COLOR
    )
    if image is None:
        raise ProtocolError(f"OpenCV could not decode frame {result.metadata.frame_id}")
    height, width = image.shape[:2]
    if (width, height) != (result.metadata.width, result.metadata.height):
        raise ProtocolError(
            f"Decoded size {width}x{height} != signed metadata "
            f"{result.metadata.width}x{result.metadata.height}"
        )
    return image


def add_session_to_transcript(
    transcript: "hashlib._Hash",
    public_key: bytes,
    ciphertext: bytes,
    epoch: int,
) -> None:
    add_transcript_field(transcript, b"kem-public-key", public_key)
    add_transcript_field(transcript, b"kem-ciphertext", ciphertext)
    add_transcript_field(transcript, b"epoch", epoch.to_bytes(4, "big"))


def add_camera_to_transcript(
    transcript: "hashlib._Hash", result: CameraResult
) -> None:
    add_transcript_field(transcript, b"camera-metadata", result.metadata_bytes)
    add_transcript_field(transcript, b"camera-nonce", result.nonce)
    add_transcript_field(transcript, b"camera-ciphertext", result.ciphertext)
    add_transcript_field(transcript, b"camera-tag", result.tag)


def configure_camera_mode(protocol, mode, resolution):
    if resolution not in ('qvga', 'vga', 'svga'):
        raise ValueError('resolution must be qvga, vga or svga')
    if mode == 'photo' and resolution != 'qvga':
        raise ValueError('--resolution controls record mode; photo uses SVGA quality 12')
    command = 'CAMERA_MODE PHOTO' if mode == 'photo' else 'CAMERA_MODE STREAM'
    if mode == 'record' and resolution != 'qvga':
        command += ' ' + resolution.upper()
    protocol.send_line(command)
    print(f"[PASS] {protocol.expect_prefix('OK camera_mode=')}")
    protocol.trace('camera_configuration', mode=mode,
                   resolution='svga' if mode == 'photo' else resolution,
                   jpeg_quality=12 if mode == 'photo' else 15)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Encrypted ESP32-S3-CAM photo and recording demo"
    )
    parser.add_argument("--port", default="COM3")
    parser.add_argument('--trust-key', type=Path, help='trusted device .pub file; default: host/camera1.pub')
    parser.add_argument('--device-name', default='ESP32-S3-CAM', help='label for this camera window')
    parser.add_argument('--host', help='ESP32 IP; selects TCP instead of UART')
    parser.add_argument('--tcp-port', type=int, default=9000)
    parser.add_argument("--response-timeout", type=float, default=10.0, help="TCP total deadline per command in seconds")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--mode", choices=("photo", "record"), default="record")
    parser.add_argument("--resolution", choices=("qvga", "vga", "svga"), default="qvga",
                        help="record resolution: 320x240, 640x480 or 800x600; JPEG quality remains 15")
    parser.add_argument("--output", type=Path, help="photo output path; ignored in record mode")
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--output-fps", type=float, default=12.0, help="legacy option; no video is saved")
    parser.add_argument("--rekey-every", type=int, default=10)
    parser.add_argument('--rekey-mode', choices=('blocking', 'pipeline'), default='blocking',
                        help='experimental pipeline requires updated WiFi firmware; keeps the same key-use limit')
    parser.add_argument(
        "--memory-every",
        type=int,
        default=10,
        help="query ESP32 memory every N frames; use 0 to disable",
    )
    parser.add_argument("--display", action="store_true")
    parser.add_argument('--profile', action='store_true', help='trace per-frame Host timings')
    parser.add_argument("--skip-device-selftest", action="store_true")
    parser.add_argument('--max-recoveries', type=int, default=2,
                        help='total camera receive-timeout recoveries per run; 0 disables')
    parser.add_argument('--inject-camera-timeout-at', type=int, default=0,
                        help='test only: abandon tag header once at request N; 0 disables')
    parser.add_argument('--diagnostics', type=Path, help='JSONL trace path (default: diagnostics/camera_TIMESTAMP.jsonl)')
    return parser.parse_args()


def run_once(args) -> int:
    import cv2
    view = getattr(args, "_live_display", None)
    response_timeout = getattr(args, "response_timeout", 10.0)
    if not math.isfinite(response_timeout) or response_timeout <= 0:
        raise ValueError("response-timeout must be positive and finite")
    use_tcp = bool(args.host)
    pipeline_enabled = getattr(args, 'rekey_mode', 'blocking') == 'pipeline'
    if pipeline_enabled and (not use_tcp or args.mode != 'record' or args.rekey_every < 3):
        raise ValueError('Pipeline requires TCP record mode and rekey-every >= 3')
    if args.mode == "photo" and getattr(args, "resolution", "qvga") != "qvga":
        raise ValueError("--resolution controls record mode; photo uses SVGA quality 12")
    if not 1 <= args.tcp_port <= 65535:
        raise ValueError('tcp-port must be between 1 and 65535')
    if use_tcp and args.inject_camera_timeout_at:
        raise ValueError('UART timeout injection is not supported in TCP mode')
    # Fail before opening the port or running device self-tests if not enrolled.
    trusted_key = getattr(args, '_trusted_key', None)
    if trusted_key is None:
        trusted_key = load_trusted_key(getattr(args, 'trust_key', None))
        args._trusted_key = trusted_key
    if not 1 <= args.rekey_every <= 100000:
        raise ValueError("rekey-every must be between 1 and 100000")
    if args.seconds <= 0:
        raise ValueError("seconds must be greater than zero")
    if args.memory_every < 0:
        raise ValueError("memory-every must be zero or greater")
    if not 0 <= args.max_recoveries <= 10:
        raise ValueError('max-recoveries must be between 0 and 10')
    if args.inject_camera_timeout_at < 0:
        raise ValueError('inject-camera-timeout-at must be zero or greater')

    output = (args.output or Path("encrypted_photo.jpg")) if args.mode == "photo" else None
    if args.mode == "record":
        print('[INFO] Live validation only; no video file will be saved (--output is ignored).')

    print(f'Opening TCP {args.host}:{args.tcp_port}...' if use_tcp
          else f"Opening {args.port} at {args.baud} baud...")
    frames_received = 0
    last_memory_frame = -1
    started: float | None = None

    trace_path = args.diagnostics or Path('diagnostics') / ('camera_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.jsonl')
    trace_path.parent.mkdir(parents=True, exist_ok=True)
    trace_file = trace_path.open('x', encoding='utf-8')
    last_verified_frame = None
    recovery_count = 0
    injection_used = False
    from host.trace_writer import TraceWriter
    trace_writer = TraceWriter(trace_file)

    def record_trace(event):
        event = dict(event)
        event.setdefault('wall_time', datetime.now().astimezone().isoformat())
        event.setdefault('monotonic', time.monotonic())
        trace_writer.write(event)

    print(f'[DIAG] {trace_path.resolve()}')
    try:
        record_trace(dict(event='port_open_begin', port=args.port, baud=args.baud,
                          transport='tcp' if use_tcp else 'uart', host=args.host, tcp_port=args.tcp_port,
                          seconds=args.seconds, mode=args.mode,
                          output=str(output) if output is not None else None, save_video=False))
        connection = (TcpConnection(args.host, args.tcp_port, timeout=response_timeout, diagnostic=record_trace,
                                    command_timeout=response_timeout, stop_event=getattr(args, "_stop_event", None)) if use_tcp
                      else camera_connection(args.port, args.baud, record_trace))
        with connection as serial_port:
            if not use_tcp:
                serial_port.reset_output_buffer()
            protocol = SerialProtocol(serial_port, diagnostic=record_trace)
            protocol.trusted_key = trusted_key
            protocol.profile = getattr(args, 'profile', False)
            protocol.context = dict(stage='startup')
            if use_tcp:
                serial_port.diagnostic = lambda event: protocol.trace(event['event'], **{k:v for k,v in event.items() if k != 'event'})
            else:
                capture_startup(protocol)

            print(request_info(protocol))
            protocol.rekey_interval = args.rekey_every
            initial_session = establish_session(protocol) if protocol.mutual_auth else None
            if pipeline_enabled and protocol.pipeline_rekey is not True:
                raise ProtocolError('Firmware does not advertise pipeline_rekey=1; upload the updated sketch')
            initial_snapshot = request_snapshot(protocol, 'start')
            accepted_boot = initial_snapshot['BOOT_INFO']['boot_id']

            if not args.skip_device_selftest and not protocol.mutual_auth:
                protocol.send_line("SELFTEST")
                selftest = protocol.read_until_prefix("SELFTEST ", timeout=30.0)
                if "FAIL" in selftest:
                    raise ProtocolError(selftest)
                print(f"[PASS] Device crypto self-test: {selftest}")

            if args.memory_every:
                print(format_memory_status(request_memory_status(protocol)))
                last_memory_frame = 0

            if not protocol.mutual_auth:
                protocol.send_line(f"SET_REKEY_INTERVAL {args.rekey_every}")
                print(f"[PASS] {protocol.expect_prefix('OK rekey_every=')}")
            configure_camera_mode(protocol, args.mode, getattr(args, 'resolution', 'qvga'))

            transcript = hashlib.sha256()
            transcript.update(b"esp32-only/camera-transcript/v1")
            public_key, kem_ciphertext, shared_secret, epoch = initial_session or establish_session(protocol)
            pipeline = None
            if pipeline_enabled:
                from host.rekey_pipeline import RekeyPipeline
                pipeline = RekeyPipeline(protocol, (public_key, kem_ciphertext, shared_secret, epoch), args.rekey_every)
            replay_guard = CameraReplayGuard(args.rekey_every)
            replay_guard.begin_session(epoch)
            protocol.trace('camera_replay_session', epoch=epoch, last_frame=None)
            print('[PASS] Camera replay/order guard enabled; frame IDs must remain consecutive across rekey')
            add_session_to_transcript(
                transcript, public_key, kem_ciphertext, epoch
            )
            previous_public_key = public_key
            previous_shared_secret = shared_secret
            started = time.monotonic()
            protocol.trace('stream_start', rekey_every=args.rekey_every,
                           memory_every=args.memory_every, display=args.display,
                           profile=protocol.profile, resolution=getattr(args, 'resolution', 'qvga'), rekey_mode='pipeline' if pipeline_enabled else 'blocking')

            while args.mode == "photo" or time.monotonic() - started < args.seconds:
                if view is not None and view.stop.is_set():
                    raise UserStop()
                protocol.context = dict(stage='stream', request_index=frames_received + 1, last_verified_frame=last_verified_frame, expected_epoch=epoch)
                try:
                    inject = not injection_used and args.inject_camera_timeout_at == frames_received + 1
                    injection_used = injection_used or inject
                    result = request_encrypted_frame(protocol, shared_secret, epoch, replay_guard,
                                                     inject_timeout=inject, pipeline=pipeline)
                except TimeoutError as error:
                    if use_tcp or args.mode != 'record' or recovery_count >= args.max_recoveries:
                        reason = ('tcp_reconnect_required' if use_tcp else
                                  'recovery_limit_reached' if args.mode == 'record' else 'photo_recovery_disabled')
                        protocol.trace('recovery_stopped', reason=reason,
                                       attempts=recovery_count, limit=args.max_recoveries)
                        print(f'[RECOVERY STOP] {reason}; attempts={recovery_count}')
                        raise
                    recovery_count += 1
                    protocol.trace('recovery_start', attempt=recovery_count, reason=str(error))
                    print(f'[RECOVERY] Incomplete camera response; attempt {recovery_count}/{args.max_recoveries}')
                    # Do not carry the old key or ordering state into recovery.
                    shared_secret = None
                    previous_shared_secret = None
                    replay_guard = None
                    try:
                        session, recovered_snapshot = recover_camera(
                            protocol, args.rekey_every, establish_session, request_snapshot)
                    except Exception as recovery_error:
                        protocol.trace('recovery_failed', attempt=recovery_count,
                                       error_type=type(recovery_error).__name__, reason=str(recovery_error))
                        print(f'[RECOVERY STOP] Recovery failed: {recovery_error}')
                        raise
                    public_key, kem_ciphertext, shared_secret, epoch = session
                    if public_key == previous_public_key:
                        raise ProtocolError('Recovery did not replace the KEM public key')
                    previous_public_key = public_key
                    previous_shared_secret = shared_secret
                    replay_guard = CameraReplayGuard(args.rekey_every)
                    replay_guard.begin_session(epoch)
                    accepted_boot = recovered_snapshot['BOOT_INFO']['boot_id']
                    add_transcript_field(transcript, b'recovery-segment', recovery_count.to_bytes(4, 'big'))
                    add_session_to_transcript(transcript, public_key, kem_ciphertext, epoch)
                    protocol.context = dict(stage='stream', last_verified_frame=last_verified_frame,
                                            expected_epoch=epoch)
                    protocol.trace('recovery_complete', attempt=recovery_count,
                                   boot_id=accepted_boot, new_epoch=epoch)
                    print('[RECOVERY] Boundary synchronized, device authenticated, new session confirmed')
                    continue
                if pipeline is not None and pipeline.active[3] != epoch:
                    public_key, kem_ciphertext, shared_secret, epoch = pipeline.active
                    add_session_to_transcript(transcript, public_key, kem_ciphertext, epoch)
                    previous_public_key, previous_shared_secret = public_key, shared_secret
                    protocol.context['expected_epoch'] = epoch
                    protocol.trace('camera_replay_session', epoch=epoch, last_frame=replay_guard.last_frame)
                    print(f'[PASS] Prepared camera session activated: epoch={epoch}')
                last_verified_frame = result.metadata.frame_id
                protocol.context['last_verified_frame'] = last_verified_frame
                protocol.trace('camera_verified', frame_id=last_verified_frame)
                decode_started = time.perf_counter()
                image = decode_jpeg(result)
                if protocol.profile:
                    protocol.trace('decode_timing', elapsed_ms=(time.perf_counter() - decode_started) * 1000)
                add_camera_to_transcript(transcript, result)
                frames_received += 1

                if frames_received == 1:
                    bad_tag = bytearray(result.tag)
                    bad_tag[0] ^= 1
                    try:
                        aes_decrypt(
                            shared_secret,
                            result.nonce,
                            result.ciphertext,
                            bytes(bad_tag),
                            result.metadata_bytes,
                        )
                    except InvalidTag:
                        print("[PASS] Modified camera AES-GCM tag rejected on PC")
                    else:
                        raise ProtocolError("Modified camera tag was accepted")

                elapsed = time.monotonic() - started
                print(
                    f"[PASS] frame={result.metadata.frame_id} "
                    f"{result.metadata.width}x{result.metadata.height} "
                    f"JPEG={len(result.jpeg)} epoch={result.status.epoch} "
                    f"count={result.status.count} rekey={int(result.status.rekey)} "
                    f"average={frames_received / elapsed:.2f} FPS"
                )

                if (
                    args.memory_every
                    and frames_received % args.memory_every == 0
                ):
                    print(format_memory_status(request_memory_status(protocol)))
                    last_memory_frame = frames_received

                if args.mode == "photo":
                    output.write_bytes(result.jpeg)
                    if args.display:
                        cv2.imshow("Encrypted ESP32-S3-CAM photo", image)
                        cv2.waitKey(0)
                    break

                if view is not None:
                    view.publish(image)
                elif args.display:
                    display_started = time.perf_counter()
                    cv2.imshow(f"Encrypted {getattr(args, 'device_name', 'ESP32-S3-CAM')} stream", image)
                    quit_requested = cv2.waitKey(1) & 0xFF in (ord("q"), 27)
                    if protocol.profile:
                        protocol.trace('display_timing', elapsed_ms=(time.perf_counter() - display_started) * 1000)
                    if quit_requested:
                        break

                if result.status.rekey and pipeline is None:
                    rekey_started = time.perf_counter()
                    public_key, kem_ciphertext, shared_secret, epoch = establish_session(
                        protocol
                    )
                    if public_key == previous_public_key:
                        raise ProtocolError("ESP32 did not rotate its camera ML-KEM key")
                    if shared_secret == previous_shared_secret:
                        raise ProtocolError("Camera rekey produced the same secret")
                    replay_guard.begin_session(epoch)
                    if protocol.profile:
                        protocol.trace('rekey_timing', elapsed_ms=(time.perf_counter() - rekey_started) * 1000)
                    protocol.trace('camera_replay_session', epoch=epoch,
                                   last_frame=replay_guard.last_frame)
                    add_session_to_transcript(
                        transcript, public_key, kem_ciphertext, epoch
                    )
                    previous_public_key = public_key
                    previous_shared_secret = shared_secret
                    print(f"[PASS] Camera session automatically rekeyed to epoch {epoch}")

            protocol.trace('stream_end', frames_received=frames_received,
                           elapsed_ms=(time.monotonic() - started) * 1000)
            protocol.context = dict(stage='finalize', last_verified_frame=last_verified_frame,
                                    frames_received=frames_received, expected_epoch=epoch)
            if args.memory_every and last_memory_frame != frames_received:
                print(format_memory_status(request_memory_status(protocol)))

            test_device_signature(protocol, transcript.digest())
            protocol.send_line("RESET_SESSION")
            protocol.expect("OK")
            protocol.trace('session_reset_complete')
            final_snapshot = request_snapshot(protocol, 'end')
            if accepted_boot != final_snapshot['BOOT_INFO']['boot_id']:
                raise ProtocolError('Device rebooted during this run')
            protocol.trace('run_complete', initial_snapshot=initial_snapshot,
                           final_snapshot=final_snapshot, recoveries=recovery_count)

    except UserStop:
        record_trace(dict(event="user_stop", frames_received=frames_received))
        raise
    except BaseException:
        record_trace(dict(event='run_error', last_verified_frame=last_verified_frame,
                          frames_received=frames_received, traceback=traceback.format_exc()))
        print(f'[ERROR] Last verified frame={last_verified_frame}; diagnostic log: {trace_path.resolve()}')
        print('[ERROR] Current attempt stopped; see the diagnostic log for the failure stage.')
        raise
    finally:
        try:
            if not (args.host and args.mode == 'record' and getattr(args, 'auto_reconnect', False)):
                cv2.destroyAllWindows()
        finally:
            trace_file.close()

    if started is None or frames_received == 0:
        raise RuntimeError("No encrypted camera frames were received")
    elapsed = time.monotonic() - started
    print(
        f"\nVerified {frames_received} decrypted frame(s), "
        f"average {frames_received / elapsed:.2f} FPS"
    )
    if output is not None:
        print(f"Saved photo -> {output.resolve()}")
    print("Full ML-KEM + AES-GCM + rekey + ML-DSA camera flow passed.")
    print(f'Receive-timeout recoveries: {recovery_count}')
    return 0


def main() -> int:
    # The multi-device launcher uses CTRL_BREAK for cooperative Windows shutdown.
    import signal
    if hasattr(signal, 'SIGBREAK'):
        def interrupted(signum, frame):
            raise KeyboardInterrupt
        signal.signal(signal.SIGBREAK, interrupted)
    import cv2
    from host.wifi_recovery import supervise
    args = parse_arguments()
    # Pin bytes for the entire run, including reconnect attempts.
    args._trusted_key = load_trusted_key(getattr(args, 'trust_key', None))
    # UART and photo preserve their existing behavior.
    args.auto_reconnect = bool(args.host and args.mode == 'record')
    if not args.auto_reconnect:
        return run_once(args)
    try:
        if args.display:
            from host.live_display import run_with_display
            return run_with_display(args, lambda: supervise(args, run_once, cv2), cv2)
        return supervise(args, run_once, cv2)
    finally:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    raise SystemExit(main())
