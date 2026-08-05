#pragma once

#include <stdint.h>
#include "spi_slave.h"

void CryptoApp_Init(void);

// PROCESS: one chunk of image plaintext in, ciphertext out.
// chunkFlags: SPI_CHUNK_FLAG_FIRST starts a new AES-GCM
// stream (fresh nonce) for this frame; SPI_CHUNK_FLAG_LAST
// finalizes it and appends [nonce(12)][tag(16)] after the
// ciphertext in the output buffer. Returns SPI_STATUS_OK or
// SPI_STATUS_PROCESS_FAIL.
uint8_t CryptoApp_EncryptChunk(
    uint8_t chunkFlags,
    const uint8_t *input, uint32_t inputLength,
    uint8_t *output, uint32_t outputCapacity, uint32_t *outputLength);

// KEM_GET_PUBKEY: generates a fresh ML-KEM keypair for this
// session and returns the public key bytes.
uint8_t CryptoApp_KemGetPublicKey(
    uint8_t *output, uint32_t outputCapacity, uint32_t *outputLength);

// KEM_DECAPSULATE: ciphertext in (from the PC), derives the
// shared secret and installs it as the active AES-GCM key
// for subsequent PROCESS calls. No payload is returned.
uint8_t CryptoApp_KemDecapsulate(
    const uint8_t *ciphertext, uint32_t ciphertextLength);
