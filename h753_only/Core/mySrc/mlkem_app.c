#include "main.h"
#include "mlkem_app.h"
#include "ml_kem_lib.h"
#include "aes_gcm_lib.h"
#include "packet_protocol.h"
#include "command_dispatcher.h"
#include "command_opcodes.h"
#include <string.h>

#define IO_TIMEOUT_MS HAL_MAX_DELAY

static uint8_t public_key[MLKEM_PUBLIC_KEY_SIZE];
static uint8_t secret_key[MLKEM_SECRET_KEY_SIZE];
static uint8_t kem_ciphertext[MLKEM_CIPHERTEXT_SIZE];
static uint8_t shared_secret[MLKEM_SHARED_SIZE];

static uint8_t kem_ready = 0;

static uint8_t GenerateKeypair(void)
{
    memset(public_key, 0, sizeof(public_key));
    memset(secret_key, 0, sizeof(secret_key));
    memset(kem_ciphertext, 0, sizeof(kem_ciphertext));
    memset(shared_secret, 0, sizeof(shared_secret));

    if (MLKEM_Keypair(public_key, secret_key) != MLKEM_OK) {
        kem_ready = 0;
        return ML_KEM_APP_ERR_KEYPAIR;
    }

    kem_ready = 1;
    return ML_KEM_APP_OK;
}

static void Handle_GetPublicKey(Transport_t *t)
{
    if (!kem_ready) {
        Protocol_SendResponseMsg(t, RESP_ERR_NOT_READY, "KEM_NOT_READY", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, public_key, MLKEM_PUBLIC_KEY_SIZE, IO_TIMEOUT_MS);
}

/* Python 端 encapsulate 產生 ciphertext -> STM32 decapsulate -> 設成 AES key */
static void Handle_Decapsulate(Transport_t *t)
{
    uint32_t cipher_len;
    uint8_t status;

    if (!kem_ready) {
        Protocol_SendResponseMsg(t, RESP_ERR_NOT_READY, "KEM_NOT_READY", IO_TIMEOUT_MS);
        return;
    }

    status = Protocol_ReceivePacket(t, kem_ciphertext, MLKEM_CIPHERTEXT_SIZE, &cipher_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "KEM_RX_ERROR", IO_TIMEOUT_MS);
        t->flush(t);
        return;
    }

    if (cipher_len != MLKEM_CIPHERTEXT_SIZE) {
        Protocol_SendResponseMsg(t, RESP_ERR_LEN, "KEM_CIPHERTEXT_LEN_ERROR", IO_TIMEOUT_MS);
        t->flush(t);
        return;
    }

    memset(shared_secret, 0, sizeof(shared_secret));

    if (MLKEM_Decapsulate(shared_secret, kem_ciphertext, secret_key) != MLKEM_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEM_DECAPSULATE_ERROR", IO_TIMEOUT_MS);
        return;
    }

    if (AESGCM_SetKey(shared_secret, 32) != AES_GCM_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "AES_KEY_ERROR", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
}

/* STM32 自己 encapsulate（只在 Python 先給 STM32 一組 public_key 的流程才用得到） */
static void Handle_Encapsulate(Transport_t *t)
{
    if (!kem_ready) {
        Protocol_SendResponseMsg(t, RESP_ERR_NOT_READY, "KEM_NOT_READY", IO_TIMEOUT_MS);
        return;
    }

    memset(kem_ciphertext, 0, sizeof(kem_ciphertext));
    memset(shared_secret, 0, sizeof(shared_secret));

    if (MLKEM_Encapsulate(kem_ciphertext, shared_secret, public_key) != MLKEM_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEM_ENCAPSULATE_ERROR", IO_TIMEOUT_MS);
        return;
    }

    if (AESGCM_SetKey(shared_secret, 32) != AES_GCM_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "AES_KEY_ERROR", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, kem_ciphertext, MLKEM_CIPHERTEXT_SIZE, IO_TIMEOUT_MS);
}

static void Handle_Rekey(Transport_t *t)
{
    if (GenerateKeypair() != ML_KEM_APP_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEM_REKEY_ERROR", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
}

uint8_t MLKEM_App_Init(void)
{
    uint8_t status = GenerateKeypair();

    Dispatcher_Register(CMD_KEM_GET_PUBKEY, Handle_GetPublicKey);
    Dispatcher_Register(CMD_KEM_DECAPSULATE, Handle_Decapsulate);
    Dispatcher_Register(CMD_KEM_ENCAPSULATE, Handle_Encapsulate);
    Dispatcher_Register(CMD_KEM_REKEY, Handle_Rekey);

    return status;
}
