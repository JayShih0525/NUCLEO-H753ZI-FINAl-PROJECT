# we need to create an unknown keypair for test

from pqcrypto.sign import ml_dsa_44
import hashlib
from pathlib import Path # store the keypair as file

public_key, secret_key = ml_dsa_44.generate_keypair()

fingerprint = hashlib.sha256(public_key).digest()

# digset make we take the original 32bytes
# public_key -> sha256 -> fingerprint(32 bytes)

output_dir = Path(__file__).resolve().parents[1] / ".test-data" / "host-b"
output_dir.mkdir(parents = True, exist_ok = False)

with ( output_dir / "public_key.bin").open("xb") as file:
    file.write(public_key)
with ( output_dir / "secret_key.bin").open("xb") as file:
    file.write(secret_key)
with ( output_dir / "fingerprint.bin").open("xb") as file:
    file.write(fingerprint)
