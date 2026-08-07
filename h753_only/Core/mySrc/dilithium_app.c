#include "main.h"
#include "dilithium_app.h"
#include "packet_protocol.h"
#include "command_dispatcher.h"
#include "command_opcodes.h"
#include "dilithium_api.h"
#include <string.h>
#include <stdio.h>

#define IO_TIMEOUT_MS HAL_MAX_DELAY

static uint8_t dilithium_pk[pqcrystals_dilithium2_ref_PUBLICKEYBYTES];
static uint8_t dilithium_sk[pqcrystals_dilithium2_ref_SECRETKEYBYTES];
static uint8_t dilithium_sig[pqcrystals_dilithium2_ref_BYTES];

static uint8_t msg_buffer[1024];
static uint8_t sig_buffer[pqcrystals_dilithium2_ref_BYTES];

static uint8_t dilithium_key_ready = 0;

/* 純粹產生 keypair，不送任何協定回應 —— 每個指令最終只能有「一個」回應，
 * 所以像 Handle_GetPublicKey / Handle_Sign 需要「還沒 ready 就自動補產生
 * key」這種內部 side-effect 時，不能讓這個 helper 自己也送一個回應，
 * 否則同一個 opcode 會回兩段東西，host 端會直接解析錯位。
 * 這是舊版（main.c 裡 DILITHIUM_SendPublicKey 呼叫 DILITHIUM_Rekey 時
 * 會多印一行文字）留下的協定瑕疵，這裡順便修掉。 */
static uint8_t EnsureKeyReady(void)
{
    int ret;

    if (dilithium_key_ready) {
        return 1;
    }

    ret = pqcrystals_dilithium2_ref_keypair(dilithium_pk, dilithium_sk);
    dilithium_key_ready = (ret == 0) ? 1 : 0;

    return dilithium_key_ready;
}

static void Handle_Rekey(Transport_t *t)
{
    int ret = pqcrystals_dilithium2_ref_keypair(dilithium_pk, dilithium_sk);

    if (ret == 0) {
        dilithium_key_ready = 1;
        Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
    } else {
        dilithium_key_ready = 0;
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "REKEY_FAIL", IO_TIMEOUT_MS);
    }
}

static void Handle_SelfTest(Transport_t *t)
{
    uint8_t msg[] = "hello dilithium stm32";
    size_t msglen = strlen((char *)msg);
    size_t siglen = 0;
    int ret;
    char summary[192];

    ret = pqcrystals_dilithium2_ref_keypair(dilithium_pk, dilithium_sk);
    if (ret != 0) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_FAIL", IO_TIMEOUT_MS);
        return;
    }
    dilithium_key_ready = 1;

    ret = pqcrystals_dilithium2_ref_signature(dilithium_sig, &siglen, msg, msglen, NULL, 0, dilithium_sk);
    if (ret != 0) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_PASS;SIGN_FAIL", IO_TIMEOUT_MS);
        return;
    }

    ret = pqcrystals_dilithium2_ref_verify(dilithium_sig, siglen, msg, msglen, NULL, 0, dilithium_pk);
    if (ret != 0) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_PASS;SIGN_PASS;VERIFY_FAIL", IO_TIMEOUT_MS);
        return;
    }

    msg[0] ^= 1;
    ret = pqcrystals_dilithium2_ref_verify(dilithium_sig, siglen, msg, msglen, NULL, 0, dilithium_pk);
    /* tamper test「應該」要 verify 失敗 (ret != 0) 才算 PASS */
    const char *tamper_result = (ret != 0) ? "TAMPER_TEST_PASS" : "TAMPER_TEST_FAIL";

    snprintf(summary, sizeof(summary),
             "KEYPAIR_PASS;SIGN_PASS siglen=%u;VERIFY_PASS;%s",
             (unsigned int)siglen, tamper_result);

    Protocol_SendResponseMsg(t, RESP_OK, summary, IO_TIMEOUT_MS);
}

static void Handle_GetPublicKey(Transport_t *t)
{
    if (!EnsureKeyReady()) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_FAIL", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, dilithium_pk,
                           pqcrystals_dilithium2_ref_PUBLICKEYBYTES, IO_TIMEOUT_MS);
}

static void Handle_Sign(Transport_t *t)
{
    uint32_t msg_len = 0;
    size_t siglen = 0;
    uint8_t status;
    int ret;

    if (!EnsureKeyReady()) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_FAIL", IO_TIMEOUT_MS);
        return;
    }

    status = Protocol_ReceivePacket(t, msg_buffer, sizeof(msg_buffer), &msg_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "SIGN_RX_FAIL", IO_TIMEOUT_MS);
        return;
    }

    ret = pqcrystals_dilithium2_ref_signature(dilithium_sig, &siglen, msg_buffer, msg_len, NULL, 0, dilithium_sk);
    if (ret != 0) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "SIGN_FAIL", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, dilithium_sig, (uint32_t)siglen, IO_TIMEOUT_MS);
}

static void Handle_Verify(Transport_t *t)
{
    uint32_t sig_len = 0;
    uint32_t msg_len = 0;
    uint8_t status;
    int ret;

    if (!dilithium_key_ready) {
        Protocol_SendResponseMsg(t, RESP_ERR_NOT_READY, "NO_KEY", IO_TIMEOUT_MS);
        return;
    }

    status = Protocol_ReceivePacket(t, sig_buffer, sizeof(sig_buffer), &sig_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "VERIFY_SIG_RX_FAIL", IO_TIMEOUT_MS);
        return;
    }

    status = Protocol_ReceivePacket(t, msg_buffer, sizeof(msg_buffer), &msg_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "VERIFY_MSG_RX_FAIL", IO_TIMEOUT_MS);
        return;
    }

    ret = pqcrystals_dilithium2_ref_verify(sig_buffer, sig_len, msg_buffer, msg_len, NULL, 0, dilithium_pk);

    if (ret == 0) {
        Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
    } else {
        /* 簽章驗證不通過是「合法結果」，不是 IO/系統錯誤，用專屬的
         * RESP_VERIFY_FAIL，讓 host 可以區分「驗證出結果=false」
         * 跟「這個請求根本沒送成功」。 */
        Protocol_SendResponse(t, RESP_VERIFY_FAIL, NULL, 0, IO_TIMEOUT_MS);
    }
}

void Dilithium_App_Init(void)
{
    Dispatcher_Register(CMD_DILITHIUM_SELFTEST, Handle_SelfTest);
    Dispatcher_Register(CMD_DILITHIUM_REKEY, Handle_Rekey);
    Dispatcher_Register(CMD_DILITHIUM_GET_PUBKEY, Handle_GetPublicKey);
    Dispatcher_Register(CMD_DILITHIUM_SIGN, Handle_Sign);
    Dispatcher_Register(CMD_DILITHIUM_VERIFY, Handle_Verify);
}
