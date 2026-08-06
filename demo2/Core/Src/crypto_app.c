#include "crypto_app.h"

#include <string.h>

#include "ml_kem_lib.h"
#include "aes_gcm_lib.h"
#include "randombytes.h"

// Real sizes now, taken from ml_kem_lib.h / aes_gcm_lib.h -
// nothing guessed. MLKEM_SHARED_SIZE and AES_GCM_KEY_SIZE
// should both be 32 (ML-KEM's shared secret feeds directly
// into AES-256-GCM's key) - this is asserted below.
#define AES_GCM_NONCE_BYTES  AES_GCM_NONCE_SIZE
#define AES_GCM_TAG_BYTES    AES_GCM_TAG_SIZE

static uint8_t g_kemPublicKey[MLKEM_PUBLIC_KEY_SIZE];
static uint8_t g_kemSecretKey[MLKEM_SECRET_KEY_SIZE];

static uint8_t g_aesKey[MLKEM_SHARED_SIZE];
static uint8_t g_aesKeyReady = 0U;

// Streaming state for the frame currently in progress
// (spans multiple PROCESS calls, FIRST..LAST).
static uint8_t g_streamNonce[AES_GCM_NONCE_BYTES];
static uint8_t g_streamActive = 0U;

#define MAX_FRAME_PLAINTEXT (256U * 1024U) // size to your JPEG output; adjust for available SRAM

static uint8_t g_frameBuffer[MAX_FRAME_PLAINTEXT];
static uint32_t g_frameLength = 0U;

void CryptoApp_Init(void)
{
    g_aesKeyReady = 0U;
    g_streamActive = 0U;

    // Compile-time-ish sanity check: if your ML-KEM parameter
    // set ever changes and the shared secret size stops
    // matching AES-256's key size, fail loudly at boot instead
    // of silently truncating/overrunning g_aesKey later.
    if (MLKEM_SHARED_SIZE != AES_GCM_KEY_SIZE)
    {
        Error_Handler();
    }
}

uint8_t CryptoApp_EncryptChunk(
    uint8_t chunkFlags,
    const uint8_t *input, uint32_t inputLength,
    uint8_t *output, uint32_t outputCapacity, uint32_t *outputLength)
{
    if (!g_aesKeyReady)
    {
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    if (chunkFlags & SPI_CHUNK_FLAG_FIRST)
    {
        g_frameLength = 0U;
        g_streamActive = 1U;

        randombytes(g_streamNonce, AES_GCM_NONCE_BYTES);
    }

    if (!g_streamActive)
    {
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    if ((g_frameLength + inputLength) > MAX_FRAME_PLAINTEXT)
    {
        g_streamActive = 0U;
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    memcpy(&g_frameBuffer[g_frameLength], input, inputLength);
    g_frameLength += inputLength;

    if (!(chunkFlags & SPI_CHUNK_FLAG_LAST))
    {
        // Not the final chunk - nothing to encrypt yet, mirror
        // plaintext length so the ESP32-side length check
        // (non-final chunks) passes.
        if (inputLength > outputCapacity)
        {
            *outputLength = 0U;
            return SPI_STATUS_PROCESS_FAIL;
        }

        memset(output, 0, inputLength);
        *outputLength = inputLength;
        return SPI_STATUS_OK;
    }

    // Final chunk: real one-shot AES-256-GCM over the whole
    // accumulated frame, then append nonce + tag.
    if ((g_frameLength + AES_GCM_NONCE_BYTES + AES_GCM_TAG_BYTES) > outputCapacity)
    {
        g_streamActive = 0U;
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    uint8_t tag[AES_GCM_TAG_BYTES];

    const uint8_t encryptStatus = AESGCM_Encrypt(
        g_streamNonce, g_frameBuffer, g_frameLength, output, tag);

    g_streamActive = 0U;

    if (encryptStatus != AES_GCM_OK)
    {
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    uint8_t *nonceOut = output + g_frameLength;
    uint8_t *tagOut = nonceOut + AES_GCM_NONCE_BYTES;

    memcpy(nonceOut, g_streamNonce, AES_GCM_NONCE_BYTES);
    memcpy(tagOut, tag, AES_GCM_TAG_BYTES);

    *outputLength = g_frameLength + AES_GCM_NONCE_BYTES + AES_GCM_TAG_BYTES;

    return SPI_STATUS_OK;
}

uint8_t CryptoApp_KemGetPublicKey(
    uint8_t *output, uint32_t outputCapacity, uint32_t *outputLength)
{
    if (outputCapacity < MLKEM_PUBLIC_KEY_SIZE)
    {
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    if (MLKEM_Keypair(g_kemPublicKey, g_kemSecretKey) != MLKEM_OK)
    {
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    memcpy(output, g_kemPublicKey, MLKEM_PUBLIC_KEY_SIZE);
    *outputLength = MLKEM_PUBLIC_KEY_SIZE;

    return SPI_STATUS_OK;
}

uint8_t CryptoApp_KemDecapsulate(
    const uint8_t *ciphertext, uint32_t ciphertextLength)
{
    if (ciphertextLength != MLKEM_CIPHERTEXT_SIZE)
    {
        return SPI_STATUS_PROCESS_FAIL;
    }

    if (MLKEM_Decapsulate(g_aesKey, ciphertext, g_kemSecretKey) != MLKEM_OK)
    {
        g_aesKeyReady = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    if (AESGCM_SetKey(g_aesKey, AES_GCM_KEY_SIZE) != AES_GCM_OK)
    {
        g_aesKeyReady = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    g_aesKeyReady = 1U;

    return SPI_STATUS_OK;
}
