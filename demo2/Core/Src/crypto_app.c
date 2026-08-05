#include "crypto_app.h"

#include <string.h>

// TODO: confirm these two includes against your actual
// project - I have not seen ml_kem_api.h or
// aes_gcm_uart_app.h, only that your old main.c included
// them. Function names below are placeholders.
//
// #include "ml_kem_api.h"
// #include "randombytes.h"

#define KEM_PUBLICKEYBYTES   1184U  // TODO: confirm vs your ML-KEM parameter set
#define KEM_SECRETKEYBYTES   2400U  // TODO: confirm
#define KEM_CIPHERTEXTBYTES  1088U  // TODO: confirm
#define KEM_SHAREDSECRETBYTES  32U

#define AES_GCM_NONCE_BYTES  12U
#define AES_GCM_TAG_BYTES    16U

static uint8_t g_kemPublicKey[KEM_PUBLICKEYBYTES];
static uint8_t g_kemSecretKey[KEM_SECRETKEYBYTES];

static uint8_t g_aesKey[KEM_SHAREDSECRETBYTES];
static uint8_t g_aesKeyReady = 0U;

// Streaming AES-GCM state for the frame currently in
// progress (spans multiple PROCESS calls, FIRST..LAST).
static uint8_t g_streamNonce[AES_GCM_NONCE_BYTES];
static uint8_t g_streamActive = 0U;

void CryptoApp_Init(void)
{
    g_aesKeyReady = 0U;
    g_streamActive = 0U;
}

// =====================================================
// PROCESS - AES-GCM encrypt one chunk
// =====================================================
// TODO: this is the piece I could not write with
// confidence - I don't know whether your AES-GCM library
// supports incremental encrypt (init/update/finalize) or
// only one-shot (whole buffer in, whole buffer out).
//
// If it's one-shot only, the safe approach is: accumulate
// plaintext chunks into a per-frame buffer here, and only
// call the real encrypt function once, on the LAST chunk,
// over the whole accumulated frame. That still satisfies
// "nonce+tag attached on the last chunk" - it just means
// the buffering happens in this file instead of inside the
// crypto library. Swap the body below for whichever your
// library actually supports.
// =====================================================

#define MAX_FRAME_PLAINTEXT (256U * 1024U) // size to your JPEG output; adjust for available SRAM

static uint8_t g_frameBuffer[MAX_FRAME_PLAINTEXT];
static uint32_t g_frameLength = 0U;

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

        // TODO: replace with your real RNG call, e.g.
        // randombytes(g_streamNonce, AES_GCM_NONCE_BYTES);
        memset(g_streamNonce, 0, sizeof(g_streamNonce));
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
        // Not the final chunk yet - nothing to send back
        // other than an ack. Mirror plaintext length so the
        // ESP32-side length check (non-final chunks) passes;
        // real ciphertext only exists once we finalize below.
        if (inputLength > outputCapacity)
        {
            *outputLength = 0U;
            return SPI_STATUS_PROCESS_FAIL;
        }

        memset(output, 0, inputLength);
        *outputLength = inputLength;
        return SPI_STATUS_OK;
    }

    // Final chunk: encrypt the whole accumulated frame in
    // one shot and append nonce + tag after the ciphertext.
    if ((g_frameLength + AES_GCM_NONCE_BYTES + AES_GCM_TAG_BYTES) > outputCapacity)
    {
        g_streamActive = 0U;
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    // TODO: replace with your real one-shot AES-GCM encrypt,
    // something like:
    // AESGCM_Encrypt(g_aesKey, g_streamNonce,
    //                g_frameBuffer, g_frameLength,
    //                output, &tag);
    memcpy(output, g_frameBuffer, g_frameLength);

    uint8_t *nonceOut = output + g_frameLength;
    uint8_t *tagOut = nonceOut + AES_GCM_NONCE_BYTES;

    memcpy(nonceOut, g_streamNonce, AES_GCM_NONCE_BYTES);
    memset(tagOut, 0, AES_GCM_TAG_BYTES); // TODO: real tag from the encrypt call above

    *outputLength = g_frameLength + AES_GCM_NONCE_BYTES + AES_GCM_TAG_BYTES;

    g_streamActive = 0U;

    return SPI_STATUS_OK;
}

// =====================================================
// KEM_GET_PUBKEY
// =====================================================

uint8_t CryptoApp_KemGetPublicKey(
    uint8_t *output, uint32_t outputCapacity, uint32_t *outputLength)
{
    if (outputCapacity < KEM_PUBLICKEYBYTES)
    {
        *outputLength = 0U;
        return SPI_STATUS_PROCESS_FAIL;
    }

    // TODO: replace with your real ML-KEM keypair call, e.g.
    // if (pqcrystals_kyber768_ref_keypair(g_kemPublicKey, g_kemSecretKey) != 0)
    // {
    //     *outputLength = 0U;
    //     return SPI_STATUS_PROCESS_FAIL;
    // }
    (void)g_kemSecretKey;

    memcpy(output, g_kemPublicKey, KEM_PUBLICKEYBYTES);
    *outputLength = KEM_PUBLICKEYBYTES;

    return SPI_STATUS_OK;
}

// =====================================================
// KEM_DECAPSULATE
// =====================================================

uint8_t CryptoApp_KemDecapsulate(
    const uint8_t *ciphertext, uint32_t ciphertextLength)
{
    if (ciphertextLength != KEM_CIPHERTEXTBYTES)
    {
        return SPI_STATUS_PROCESS_FAIL;
    }

    // TODO: replace with your real ML-KEM decapsulate call, e.g.
    // if (pqcrystals_kyber768_ref_dec(g_aesKey, ciphertext, g_kemSecretKey) != 0)
    // {
    //     g_aesKeyReady = 0U;
    //     return SPI_STATUS_PROCESS_FAIL;
    // }
    (void)ciphertext;
    memset(g_aesKey, 0, sizeof(g_aesKey));

    g_aesKeyReady = 1U;

    return SPI_STATUS_OK;
}
