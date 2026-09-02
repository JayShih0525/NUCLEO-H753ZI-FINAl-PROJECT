# ESP32-S3-CAM + PC post-quantum crypto demo

This project exercises three algorithms both locally and across the USB TTL
serial link:

- ML-KEM-768 (FIPS 203) establishes a 32-byte shared secret.
- AES-256-GCM encrypts and authenticates messages in both directions.
- ML-DSA-44 (FIPS 204) signs the handshake transcript on the ESP32 and verifies
  it on the PC.
- The ML-KEM/AES session automatically rotates after a configurable number of
  successfully authenticated AES messages (10 by default).

The Arduino firmware also exposes `SELFTEST`, which runs local ML-KEM, AES-GCM,
and ML-DSA round trips entirely on the ESP32.

## Layout

```text
esp32-only/
├── firmware/esp32_pqc_demo/   Arduino sketch and vendored ML-KEM-768 C source
├── host/                      PC serial interoperability test
├── tests/                     PC-only cryptographic self-tests
├── camera-tests/              Standalone OV2640 photo and video tests
└── requirements.txt           Python dependencies
```

## Camera baseline

The standalone camera tests under `camera-tests` deliberately do not use
encryption yet. On 2026-08-28 the N16R8 pin map was verified on the real board:
one 800x600 JPEG photo was captured successfully, and the QVGA UART stream
delivered 70 valid frames in 5.05 seconds (13.87 FPS) at 460800 baud. See
`camera-tests/README.md` for the photo and recording commands.

## Encrypted camera flow

The main `esp32_pqc_demo` firmware now includes the OV2640 camera. JPEG data is
encrypted on the ESP32 before crossing UART. The PC verifies the AES-GCM tag,
decrypts the JPEG, and saves a photo or records a video. Each successful frame
counts toward the configured automatic rekey interval.

Encrypted photo:

```powershell
..\.venv\Scripts\python.exe -m host.pqc_camera_demo --port COM5 --mode photo --output encrypted_photo.jpg --rekey-every 10
```

Encrypted 10-second recording:

```powershell
..\.venv\Scripts\python.exe -m host.pqc_camera_demo --port COM5 --mode record --seconds 10 --output encrypted_camera_recording.avi --output-fps 10 --rekey-every 10 --memory-every 10
```

Add `--display` to show decrypted frames. Photo mode uses 800x600 JPEG and
recording mode uses 320x240 JPEG. Frame metadata is authenticated as AES-GCM
AAD and contains the session epoch, frame ID, dimensions, and JPEG length. The
final ML-DSA signature covers the KEM sessions and every encrypted frame.
The host safely queries `MEMORY_INFO` only between complete encrypted frames.
`--memory-every 10` prints internal heap, PSRAM, largest allocatable blocks, and
the PQC task stack low-water mark every ten frames. Use `--memory-every 1` for
every frame or `--memory-every 0` to disable monitoring.

## Required Arduino software

1. Install `esp32 by Espressif Systems` 3.3.11 in Boards Manager.
2. Install `mldsa-esp32` from https://github.com/NeuraiProject/mldsa-esp32
   with **Sketch > Include Library > Add .ZIP Library**.
3. Open `firmware/esp32_pqc_demo/esp32_pqc_demo.ino`.

No separate AES or ML-KEM Arduino library is required. AES-GCM comes from the
ESP32 core's mbedTLS, and the pinned PQClean ML-KEM source is included under the
sketch's `src` directory.

Use these Arduino settings for the N16R8 board:

```text
Board: ESP32S3 Dev Module
USB cable: TTL port
USB CDC On Boot: Disabled
Upload Mode: UART0 / Hardware CDC
Flash Size: 16MB
Flash Mode: QIO 80MHz
PSRAM: OPI PSRAM
Upload Speed: 460800 or 512000
Serial baud: 460800
```

Upload the firmware. Its serial output should finish with:

```text
PQC-DEMO READY
```

You may type `SELFTEST` followed by Enter in Serial Monitor. Expected result:

```text
SELFTEST MLKEM=PASS AES=PASS MLDSA=PASS
```

Close Serial Monitor before starting the PC program because only one program
can own `COM5` at a time.

## PC setup and test

From PowerShell in this directory:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
.\.venv\Scripts\python.exe -m host.pqc_host_demo --port COM5 --rekey-every 10 --exchange-count 12
```

Use `--rekey-every 100 --exchange-count 105` to demonstrate a rotation after
100 successful messages instead. `--exchange-count` controls the total number
of AES messages, alternating PC-to-ESP32 and ESP32-to-PC.

The final command performs this sequence:

1. Configure the per-session successful-message limit.
2. Read the ESP32 ML-KEM-768 public key.
3. Encapsulate on the PC and decapsulate on the ESP32.
4. Confirm the ESP32 rejects a modified AES-GCM tag without incrementing the
   successful-message counter.
5. Exchange AES-256-GCM messages in both directions.
6. At the limit, clear the old shared secret and rotate the ESP32 ML-KEM keypair.
7. Automatically fetch the new public key and establish a fresh AES session.
8. Sign the final multi-session SHA-256 transcript digest on the ESP32 with
   ML-DSA-44, then verify it and reject a modified digest on the PC.

Successful output ends with:

```text
All ESP32 <-> PC interoperability tests passed.
```

## Verified hardware result

Verified on 2026-08-18 with an ESP32-S3 N16R8 board (16 MB Flash, 8 MB OPI
PSRAM), Arduino ESP32 Core 3.3.11, mldsa 0.2.0, Python 3.12, pqcrypto 0.4.0,
and cryptography 49.0.0:

```text
[PASS] Device local test: SELFTEST MLKEM=PASS AES=PASS MLDSA=PASS
[PASS] ML-KEM-768 host encapsulation: 1.690 ms
[PASS] ML-KEM-768 ESP32 decapsulation: 10 ms
[PASS] AES-256-GCM PC encrypt -> ESP32 decrypt
[PASS] AES-256-GCM modified tag rejected by ESP32
[PASS] AES-256-GCM ESP32 encrypt -> PC decrypt
[PASS] ML-DSA-44 ESP32 sign -> PC verify: 142 ms
[PASS] ML-DSA-44 modified message rejected by PC
```

Firmware build size in that configuration was 329,799 bytes of application
Flash and 41,748 bytes of global internal RAM.

Count-based automatic rekey was verified on 2026-08-20 with
`--rekey-every 10 --exchange-count 12`. Message 10 returned
`epoch=1 count=10 rekey=1`; the ESP32 generated a different ML-KEM public key,
the PC established epoch 2, and messages 11 and 12 plus the final ML-DSA
transcript signature all passed. The updated firmware build uses 330,375 bytes
of application Flash and 41,756 bytes of global internal RAM.

The integrated encrypted camera flow was verified on 2026-08-28. One encrypted
800x600 photo was decrypted successfully. A five-second encrypted recording
received 46 valid 320x240 frames at 8.98 FPS while rekeying every three frames;
it crossed epochs 1 through 16 and the final multi-session ML-DSA transcript
signature passed. The AVI contains 46 decodable frames. The integrated firmware
uses 410,827 bytes of application Flash and 52,976 bytes of global internal RAM
after adding command-driven runtime memory monitoring.

## Protocol

Commands are newline-terminated ASCII. Binary values use a four-byte
big-endian length followed by exactly that many bytes. The shared secret and
private keys are never sent over serial.

Maximum generic AES message size is 4096 bytes and maximum generic AAD size is
256 bytes. Encrypted camera JPEG frames may be up to 1 MiB and use a separate
PSRAM ciphertext allocation.

The rekey counter is shared by both directions and counts only successfully
processed AES messages. Authentication failures and protocol errors do not
count. When the threshold is reached, the device first finishes sending the
current authenticated response, then zeroes the old 32-byte shared secret,
generates a new ML-KEM-768 keypair, and requires a new encapsulation. The
ML-DSA identity key does not rotate as part of this session rekey.

The relevant protocol status lines are:

```text
SET_REKEY_INTERVAL 10
OK rekey_every=10
KEM_OK epoch=1 limit=10 elapsed_ms=...
OK epoch=1 count=10 rekey=1
MEM internal_free=... internal_min=... internal_largest=... psram_free=... psram_min=... psram_largest=... pqc_stack_min=...
```

## Security scope

This is an interoperability and benchmarking demo, not a production protocol.
Keys are regenerated at boot, the device public key is trusted on first use,
and there is no certificate chain, replay database, persistent nonce state, or
secure-element storage. The configurable count-based rekey is a demo policy;
its negotiation is not yet authenticated. A production design should persist
and authenticate a provisioned device identity, apply an explicit KDF/key
schedule, authenticate rekey policy and epoch values, define replay rules,
enable Secure Boot and Flash Encryption, and receive independent review.
