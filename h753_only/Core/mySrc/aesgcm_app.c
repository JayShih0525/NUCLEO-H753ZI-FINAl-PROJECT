#include "main.h"
#include "aesgcm_app.h"
#include "aes_gcm_lib.h"
#include "packet_protocol.h"
#include "command_dispatcher.h"
#include "command_opcodes.h"
#include <string.h>

#define IO_TIMEOUT_MS HAL_MAX_DELAY

static uint8_t key[AES_GCM_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

static uint8_t nonce[AES_GCM_NONCE_SIZE] = {
    'S', 'T', 'M', '3',
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01
};

static uint8_t plaintext[AES_GCM_APP_MAX_SIZE];
static uint8_t ciphertext[AES_GCM_APP_MAX_SIZE];
static uint8_t decrypted[AES_GCM_APP_MAX_SIZE];
static uint8_t tag[AES_GCM_TAG_SIZE];

static void AESGCM_IncrementNonce(void)
{
    for (int i = 11; i >= 4; i--) {
        nonce[i]++;
        if (nonce[i] != 0) break;
    }
}

/*
 * 回應 payload = nonce(12) + ciphertext(len) + tag(16) 接在一起。
 * host 收到後自己用固定的 12 / 16 byte 邊界切開，不需要三次獨立的
 * SendPacket/RecvPacket 來回，也不用另外開一份跟 payload 一樣大的
 * 暫存 buffer 湊連續記憶體 —— 用 Protocol_SendResponseHeader +
 * Protocol_SendRaw 分段送。
 */
static void Handle_Encrypt(Transport_t *t)
{
    uint32_t len;
    uint8_t status;

    status = Protocol_ReceivePacket(t, plaintext, AES_GCM_APP_MAX_SIZE, &len, IO_TIMEOUT_MS);
    if (status != PROTO_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "RX_ERROR", IO_TIMEOUT_MS);
        t->flush(t);
        return;
    }

    status = AESGCM_Encrypt(nonce, plaintext, len, ciphertext, tag);
    if (status != AES_GCM_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "ENC_ERROR", IO_TIMEOUT_MS);
        return;
    }

    uint32_t total_len = AES_GCM_NONCE_SIZE + len + AES_GCM_TAG_SIZE;

    Protocol_SendResponseHeader(t, RESP_OK, total_len, IO_TIMEOUT_MS);
    Protocol_SendRaw(t, nonce, AES_GCM_NONCE_SIZE, IO_TIMEOUT_MS);
    Protocol_SendRaw(t, ciphertext, len, IO_TIMEOUT_MS);
    Protocol_SendRaw(t, tag, AES_GCM_TAG_SIZE, IO_TIMEOUT_MS);

    AESGCM_IncrementNonce();
}

static void Handle_Decrypt(Transport_t *t)
{
    uint32_t nonce_len, cipher_len, tag_len;
    uint8_t status;
    uint8_t received_nonce[AES_GCM_NONCE_SIZE];

    status = Protocol_ReceivePacket(t, received_nonce, AES_GCM_NONCE_SIZE, &nonce_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK || nonce_len != AES_GCM_NONCE_SIZE) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "NONCE_ERROR", IO_TIMEOUT_MS);
        t->flush(t);
        return;
    }

    status = Protocol_ReceivePacket(t, ciphertext, AES_GCM_APP_MAX_SIZE, &cipher_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "CIPHER_ERROR", IO_TIMEOUT_MS);
        t->flush(t);
        return;
    }

    status = Protocol_ReceivePacket(t, tag, AES_GCM_TAG_SIZE, &tag_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK || tag_len != AES_GCM_TAG_SIZE) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "TAG_ERROR", IO_TIMEOUT_MS);
        t->flush(t);
        return;
    }

    status = AESGCM_Decrypt(received_nonce, ciphertext, cipher_len, tag, decrypted);
    if (status != AES_GCM_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "DEC_ERROR", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, decrypted, cipher_len, IO_TIMEOUT_MS);
}

uint8_t AESGCM_App_Init(void)
{
    if (AESGCM_SetKey(key, AES_GCM_KEY_SIZE) != AES_GCM_OK) {
        return 0;
    }

    Dispatcher_Register(CMD_AES_ENCRYPT, Handle_Encrypt);
    Dispatcher_Register(CMD_AES_DECRYPT, Handle_Decrypt);

    return 1;
}
