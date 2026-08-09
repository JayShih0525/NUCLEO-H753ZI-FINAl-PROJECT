#include "main.h"
#include "packet_protocol.h"
#include "command_dispatcher.h"
#include "command_opcodes.h"
#include "pqc_identity.h"
#include "ml_dsa_app.h"
#include <string.h>
#include <stdio.h>

/*
 * 檔名/opcode/函式名稱都用 MLDSA，不是 Dilithium —— 底層是 ML-DSA-44
 * （FIPS 204 最終標準），跟原版 CRYSTALS-Dilithium round 3 不是同一份
 * 規格（雖然 key/簽章大小剛好一樣，但雜湊/domain separation不同，
 * 兩者簽出來的東西不能互相驗證，這也是這個專案migrate 過程中踩過的
 * 一個真實的 bug）。用「Dilithium」這個舊稱會誤導成以為在用 round-3
 * 版本，所以全面改成 MLDSA 對齊實際演算法版本。
 */

#define IO_TIMEOUT_MS HAL_MAX_DELAY

static uint8_t msg_buffer[1024];
static uint8_t sig_buffer[PQC_IDENTITY_SIGBYTES];

/*
 * 注意：這份改用 pqc_identity.c（ML-DSA-44），不再直接呼叫
 * pqcrystals_dilithium2_ref_*。裝置的 keypair 現在是跟 mlkem_app.c
 * 的 "device signs the KEM public key" 模式共用同一份（見
 * pqc_identity.c 開頭的說明），不要再各自維護一份 key 狀態。
 */

static void Handle_Rekey(Transport_t *t)
{
    if (PQCIdentity_RekeyDevice()) {
        Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
    } else {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "REKEY_FAIL", IO_TIMEOUT_MS);
    }
}

static void Handle_SelfTest(Transport_t *t)
{
    uint8_t msg[] = "hello mldsa stm32";
    size_t msglen = strlen((char *)msg);
    uint8_t sig[PQC_IDENTITY_SIGBYTES];
    size_t siglen = 0;
    char summary[192];

    if (!PQCIdentity_RekeyDevice()) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_FAIL", IO_TIMEOUT_MS);
        return;
    }

    if (!PQCIdentity_SignWithDeviceKey(msg, msglen, sig, &siglen)) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_PASS;SIGN_FAIL", IO_TIMEOUT_MS);
        return;
    }

    if (!PQCIdentity_VerifyWithDeviceKey(msg, msglen, sig, siglen)) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_PASS;SIGN_PASS;VERIFY_FAIL", IO_TIMEOUT_MS);
        return;
    }

    msg[0] ^= 1;
    /* tamper test「應該」要 verify 失敗才算 PASS */
    const char *tamper_result =
        PQCIdentity_VerifyWithDeviceKey(msg, msglen, sig, siglen)
            ? "TAMPER_TEST_FAIL"
            : "TAMPER_TEST_PASS";

    snprintf(summary, sizeof(summary),
             "KEYPAIR_PASS;SIGN_PASS siglen=%u;VERIFY_PASS;%s",
             (unsigned int)siglen, tamper_result);

    Protocol_SendResponseMsg(t, RESP_OK, summary, IO_TIMEOUT_MS);
}

static void Handle_GetPublicKey(Transport_t *t)
{
    if (!PQCIdentity_EnsureDeviceKeyReady()) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_FAIL", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, PQCIdentity_DevicePublicKey(),
                           PQC_IDENTITY_PUBLICKEYBYTES, IO_TIMEOUT_MS);
}

static void Handle_Sign(Transport_t *t)
{
    uint32_t msg_len = 0;
    size_t siglen = 0;
    uint8_t status;

    if (!PQCIdentity_EnsureDeviceKeyReady()) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "KEYPAIR_FAIL", IO_TIMEOUT_MS);
        return;
    }

    status = Protocol_ReceivePacket(t, msg_buffer, sizeof(msg_buffer), &msg_len, IO_TIMEOUT_MS);
    if (status != PROTO_OK) {
        Protocol_SendResponseMsg(t, RESP_ERR_RX, "SIGN_RX_FAIL", IO_TIMEOUT_MS);
        return;
    }

    if (!PQCIdentity_SignWithDeviceKey(msg_buffer, msg_len, sig_buffer, &siglen)) {
        Protocol_SendResponseMsg(t, RESP_ERR_CRYPTO, "SIGN_FAIL", IO_TIMEOUT_MS);
        return;
    }

    Protocol_SendResponse(t, RESP_OK, sig_buffer, (uint32_t)siglen, IO_TIMEOUT_MS);
}

static void Handle_Verify(Transport_t *t)
{
    uint32_t sig_len = 0;
    uint32_t msg_len = 0;
    uint8_t status;

    if (!PQCIdentity_DeviceKeyIsReady()) {
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

    if (PQCIdentity_VerifyWithDeviceKey(msg_buffer, msg_len, sig_buffer, sig_len)) {
        Protocol_SendResponse(t, RESP_OK, NULL, 0, IO_TIMEOUT_MS);
    } else {
        /* 簽章驗證不通過是「合法結果」，不是 IO/系統錯誤 */
        Protocol_SendResponse(t, RESP_VERIFY_FAIL, NULL, 0, IO_TIMEOUT_MS);
    }
}

void MLDSA_App_Init(void)
{
    Dispatcher_Register(CMD_MLDSA_SELFTEST, Handle_SelfTest);
    Dispatcher_Register(CMD_MLDSA_REKEY, Handle_Rekey);
    Dispatcher_Register(CMD_MLDSA_GET_PUBKEY, Handle_GetPublicKey);
    Dispatcher_Register(CMD_MLDSA_SIGN, Handle_Sign);
    Dispatcher_Register(CMD_MLDSA_VERIFY, Handle_Verify);
}
