# Vendored ML-KEM-768 source

The C implementation in this directory is copied from:

- Project: PQClean
- Repository: https://github.com/PQClean/PQClean
- Commit: `0586a824fc0d49df0b6b6e9179d8d15d06d0974f`
- Path: `crypto_kem/ml-kem-768/clean`

`fips202.c`, `fips202.h`, and `compat.h` are copied from PQClean's `common`
directory at the same commit. `randombytes.c` is the ESP32 adapter and calls
Espressif's `esp_fill_random()`.

The upstream implementation is public domain/CC0; see `LICENSE`.
