"""Build the isolated ESP32-S3 N16R8 benchmark. Upload only when explicitly requested."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from prepare import ROOT, digest_tree, prepare

FQBN = "esp32:esp32:esp32s3:UploadSpeed=512000,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"


def check_prepared() -> dict:
    manifest = json.loads((ROOT / "source_manifest.json").read_text(encoding="utf-8"))
    sketch = ROOT / "firmware/esp32_crypto_benchmark"
    for sub, key in (("kem", "mlkem"), ("dsa", "mldsa")):
        if digest_tree(sketch / "src" / sub)[0] != manifest[f"{key}_tree_sha256"]:
            raise ValueError(f"Generated {sub} sources changed; prepare a fresh copy before building")
    for name, expected in manifest["harness"].items():
        file = ROOT / name if name in ("prepare.py", "build.py") else sketch / name
        if hashlib.sha256(file.read_bytes()).hexdigest() != expected:
            raise ValueError("Benchmark sources changed after preparation. Regenerate generated src/ and manifest first (see README).")
    header = (sketch / "benchmark_build.h").read_text(encoding="utf-8")
    if f'#define BENCH_BUILD_ID "{manifest["build_id"]}"' not in header:
        raise ValueError("Build header does not match source manifest")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=Path(os.environ.get("LOCALAPPDATA", "")) / "Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe")
    parser.add_argument("--dsa-library", type=Path, help="Only needed on first preparation")
    parser.add_argument("--upload", action="store_true", help="Replace application firmware; does NOT erase all flash")
    parser.add_argument("--port", help="ESP32 TTL USB port, required with --upload")
    args = parser.parse_args()
    if args.upload and not args.port:
        parser.error("--upload requires --port; upload replaces the camera application")
    if args.dsa_library:
        prepare(args.dsa_library)
    elif not (ROOT / "source_manifest.json").exists():
        if not args.dsa_library:
            parser.error("First build requires --dsa-library pointing at the installed mldsa folder")
    manifest = check_prepared()
    out = ROOT / "build"
    out.mkdir(exist_ok=True)
    extra = (ROOT / "firmware/esp32_crypto_benchmark/build_opt.h").read_text(encoding="utf-8").strip()
    evidence = {"ok": False, "build_id": manifest["build_id"], "fqbn": FQBN, "extra_include_flags": extra}
    (out / "build_info.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    command = [str(args.cli), "compile", "--fqbn", FQBN, "--build-path", str(out), str(ROOT / "firmware/esp32_crypto_benchmark")]
    print("Compiling standalone benchmark...", flush=True)
    proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
    print(proc.stdout)
    (out / "compile.log").write_text(proc.stdout, encoding="utf-8")
    (out / "build_info.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    if proc.returncode:
        return proc.returncode
    evidence.update(ok=True, source_manifest=manifest, artifacts={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob("*.bin")})
    (out / "build_info.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    if args.upload:
        print("Uploading: camera application will be replaced; full flash erase is disabled.", flush=True)
        proc = subprocess.run([str(args.cli), "upload", "--fqbn", FQBN, "--port", args.port, "--input-dir", str(out), str(ROOT / "firmware/esp32_crypto_benchmark")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
        print(proc.stdout)
        (out / "upload.log").write_text(proc.stdout, encoding="utf-8")
        return proc.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
