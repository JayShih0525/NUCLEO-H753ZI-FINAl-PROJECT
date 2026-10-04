"""Run Windows C++/Python wire interoperability and production protocol tests.

From esp32-only: python -B tests/run_record_native.py
Requires MinGW g++; no device or network connection is made.
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host.secure_records import RecordConnection


class Sink:
    def __init__(self): self.output = bytearray()
    def write(self, data): self.output.extend(data); return len(data)


def main():
    compiler = shutil.which('g++') or 'C:/msys64/mingw64/bin/g++.exe'
    if not Path(compiler).exists():
        raise SystemExit('MinGW g++ required for this Windows-native check')
    env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ.get('PATH', ''))
    vectors = []
    for device, payload in ((False, b'INFO\n'), (True, b'OK\n')):
        sink = Sink()
        RecordConnection(sink, bytes(range(32)), b'transcript', 1, device=device).write(payload)
        vectors.append(sink.output.hex())
    args = [hashlib.sha256(b'transcript').hexdigest(), *vectors]
    with tempfile.TemporaryDirectory(prefix='pqc-record-tests-') as temp:
        for source in ('secure-record-native/test.cpp', 'secure-record-native/protocol_test.cpp', 'text-tx-native/test.cpp'):
            executable = str(Path(temp) / (source.replace('/', '_') + '.exe'))
            subprocess.run([compiler, '-std=c++17', '-Itests/secure-record-native', '-Itests/text-tx-native',
                            'tests/' + source, '-lbcrypt', '-o', executable], cwd=ROOT, env=env, check=True)
            subprocess.run([executable, *args], cwd=ROOT, env=env, check=True)


if __name__ == '__main__':
    main()
