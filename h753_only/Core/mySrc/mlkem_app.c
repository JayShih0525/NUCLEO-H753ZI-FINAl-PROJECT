#include "main.h"
#include "mlkem_app.h"
#include "ml_kem_lib.h"
#include "aes_gcm_lib.h"
#include "packet_protocol.h"
#include "command_dispatcher.h"
#include "command_opcodes.h"
#include "pqc_identity.h"
#include <string.h>

#define IO_TIMEOUT_MS HAL_MAX_DELAY

static uint8_t public_key[MLKEM_PUBLIC_KEY_SIZE];
static uint8_t secret_key[MLKEM_SECRET_KEY_SIZE];
static uint8_t kem_ciphertext[MLKEM_CIPHERTEXT_SIZE];
static uint8_t shared_secret[MLKEM_SHARED_SIZE];

static uint8_t kem_ready = 0;

/*
 * 目前設定的 KEM handshake 簽章認證模式（見 command_opcodes.h 的
 * KEM_AUTH_* 常數）。預設 KEM_AUTH_NONE，跟原本行為一樣，host 要主動
 * 呼叫 CMD_KEM_SET_AUTH_MODE 才會啟用簽章認證。
 */
static uint8_t kem_auth_mode = KEM_AUTH_NONE;

static uint8_t GenerateKeypair(void)
{
    memset(public_key, 0, sizeof(public_key));
    memset(secret_key, 0, sizeof(secret_key));
    memset(kem_ciphertext, 0, sizeof(kem_ciphertext));
    memset(shared_secret, 0, sizeof(shared_secret));

    if (MLKEM_Keypair(public_key, secret_key) != MLKEM_OK){
        kem_ready = 0;
        return ML_KEM_APP_ERR_KEYPAIR;
    }

    kem_ready = 1;
    return ML_KEM_APP_OK;
}

static void Handle_SetAuthMode(Transport_t *t)
{
    uint8_t mode_buf[1];
    uint32_t len;
    uint8_t status;

    status = Protocol_ReceivePacket(t, mode_buf, sizeof(mode_buf), &len, IO_TIMEOUT_MS);
    if (status != PROTO_OK || len != 1) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "AUTH_MODE_RX_ERROR", IO_TIMEOUT_MS);
        return;
    }

    if (mode_buf[0] > KEM_AUTH_BOTH) {
        Protocol_SendResponseMsg(t, RESP_ERR_LEN, "AUTH_MODE_INVALID", IO_TIMEOUT_MS);
        return;
    }

    kem_auth_mode = mode_buf[0];
    Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
}

static void Handle_SetHostPubkey(Transport_t *t)
{
    static uint8_t buf[PQC_IDENTITY_PUBLICKEYBYTES];
    uint32_t len;
    uint8_t status;

    status = Protocol_ReceivePacket(t, buf, sizeof(buf), &len, IO_TIMEOUT_MS);
    if (status != PROTO_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "HOST_PUBKEY_RX_ERROR", IO_TIMEOUT_MS);
        return;
    }

    if (!PQCIdentity_SetHostPublicKey(buf, len)) {
        Protocol_SendResponseMsg(t, RESP_ERR_LEN, "HOST_PUBKEY_LEN_ERROR", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
}

/*
 * KEM_AUTH_DEVICE_SIGNS / _BOTH 時，回應 payload =
 *   mlkem_public_key (MLKEM_PUBLIC_KEY_SIZE) + device_signature (PQC_IDENTITY_SIGBYTES)
 * host 端已知兩段各自的固定長度，直接切開，不需要額外的內部長度欄位。
 */
static void Handle_GetPublicKey(Transport_t *t)
{
    if (!kem_ready){
        Protocol_SendResponseMsg(t, RESP_ERR_NOT_READY, "KEM_NOT_READY", IO_TIMEOUT_MS);
        return;
    }

    uint8_t need_device_sig =
        (kem_auth_mode == KEM_AUTH_DEVICE_SIGNS || kem_auth_mode == KEM_AUTH_BOTH);

    if (!need_device_sig) {
        Protocol_SendResponse(t, RESP_OK, public_key, MLKEM_PUBLIC_KEY_SIZE, IO_TIMEOUT_MS);
        return;
    }

    if (!PQCIdentity_EnsureDeviceKeyReady()) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "DEVICE_KEY_FAIL", IO_TIMEOUT_MS);
        return;
    }

    static uint8_t sig_buf[PQC_IDENTITY_SIGBYTES];
    size_t siglen = 0;

    if (!PQCIdentity_SignWithDeviceKey(public_key, MLKEM_PUBLIC_KEY_SIZE, sig_buf, &siglen)) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEM_PUBKEY_SIGN_FAIL", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponseHeader(t, RESP_OK, MLKEM_PUBLIC_KEY_SIZE + (uint32_t)siglen, IO_TIMEOUT_MS);
    Protocol_SendRaw(t, public_key, MLKEM_PUBLIC_KEY_SIZE, IO_TIMEOUT_MS);
    Protocol_SendRaw(t, sig_buf, (uint32_t)siglen, IO_TIMEOUT_MS);
}

/*
 * KEM_AUTH_HOST_SIGNS / _BOTH 時，host 在 kem_ciphertext packet 之後
 * 要「再多送一個」簽章 packet（對 kem_ciphertext 簽的章）。STM32 驗證
 * 通過才會繼續 decapsulate，驗證失敗就直接拒絕、不設定 AES key。
 */
static void Handle_Decapsulate(Transport_t *t)
{
    uint32_t cipher_len;
    uint8_t status;

    if (!kem_ready){
        Protocol_SendResponseMsg(t, RESP_ERR_NOT_READY, "KEM_NOT_READY", IO_TIMEOUT_MS);
        return;
    }

    status = Protocol_ReceivePacket(t, kem_ciphertext, MLKEM_CIPHERTEXT_SIZE, &cipher_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK){
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "KEM_RX_ERROR", IO_TIMEOUT_MS);
        t->flush(t);
        return;
    }

    if (cipher_len != MLKEM_CIPHERTEXT_SIZE){
        Protocol_SendResponseMsg(t, RESP_ERR_LEN, "KEM_CIPHERTEXT_LEN_ERROR", IO_TIMEOUT_MS);
        t->flush(t);
        return;
    }

    uint8_t need_host_sig =
        (kem_auth_mode == KEM_AUTH_HOST_SIGNS || kem_auth_mode == KEM_AUTH_BOTH);

    if (need_host_sig) {
        static uint8_t host_sig[PQC_IDENTITY_SIGBYTES];
        uint32_t sig_len;

        status = Protocol_ReceivePacket(t, host_sig, sizeof(host_sig), &sig_len, IO_TIMEOUT_MS);
        if (status != PROTO_OK) {
            Protocol_SendResponseMsg(t, RESP_ERR_RX, "KEM_HOST_SIG_RX_ERROR", IO_TIMEOUT_MS);
            t->flush(t);
            return;
        }

        if (!PQCIdentity_HostPublicKeyIsSet()) {
            Protocol_SendResponseMsg(t, RESP_ERR_NOT_READY, "HOST_PUBKEY_NOT_SET", IO_TIMEOUT_MS);
            return;
        }

        if (!PQCIdentity_VerifyWithHostKey(kem_ciphertext, cipher_len, host_sig, sig_len)) {
            /* 簽章驗證不通過：拒絕這次 handshake，不設定新的 AES key，
             * 避免有人偽造 ciphertext 硬塞一把 key 進來。 */
            Protocol_SendResponse(t, RESP_VERIFY_FAIL, NULL, 0, IO_TIMEOUT_MS);
            return;
        }
    }

    memset(shared_secret, 0, sizeof(shared_secret));

    if (MLKEM_Decapsulate(shared_secret, kem_ciphertext, secret_key) != MLKEM_OK){
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEM_DECAPSULATE_ERROR", IO_TIMEOUT_MS);
        return;
    }

    if (AESGCM_SetKey(shared_secret, 32) != AES_GCM_OK){
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "AES_KEY_ERROR", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
}

static void Handle_Encapsulate(Transport_t *t)
{
    if (!kem_ready){
        Protocol_SendResponseMsg(t, RESP_ERR_NOT_READY, "KEM_NOT_READY", IO_TIMEOUT_MS);
        return;
    }

    memset(kem_ciphertext, 0, sizeof(kem_ciphertext));
    memset(shared_secret, 0, sizeof(shared_secret));

    if (MLKEM_Encapsulate(kem_ciphertext, shared_secret, public_key) != MLKEM_OK){
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEM_ENCAPSULATE_ERROR", IO_TIMEOUT_MS);
        return;
    }

    if (AESGCM_SetKey(shared_secret, 32) != AES_GCM_OK){
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "AES_KEY_ERROR", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, kem_ciphertext, MLKEM_CIPHERTEXT_SIZE, IO_TIMEOUT_MS);
}

static void Handle_Rekey(Transport_t *t)
{
    if (GenerateKeypair() != ML_KEM_APP_OK){
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
    Dispatcher_Register(CMD_KEM_SET_AUTH_MODE, Handle_SetAuthMode);
    Dispatcher_Register(CMD_SET_HOST_MLDSA_PUBKEY, Handle_SetHostPubkey);

    return status;
}
