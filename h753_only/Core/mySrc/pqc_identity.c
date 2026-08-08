#include "pqc_identity.h"
#include "ml_dsa_native.h"
#include <string.h>

/* 對照 mldsa_native.h 實際的 public API（已經用試編譯確認過名字跟大小）：
 *   int PQCP_MLDSA_NATIVE_MLDSA44_keypair(uint8_t pk[1312], uint8_t sk[2560]);
 *   int PQCP_MLDSA_NATIVE_MLDSA44_signature(uint8_t sig[2420], const uint8_t *m,
 *       size_t mlen, const uint8_t *ctx, size_t ctxlen, const uint8_t sk[2560]);
 *   int PQCP_MLDSA_NATIVE_MLDSA44_verify(const uint8_t sig[2420], const uint8_t *m,
 *       size_t mlen, const uint8_t *ctx, size_t ctxlen, const uint8_t pk[1312]);
 *
 * ctx/ctxlen 兩邊統一用 NULL, 0（空 context string），跟 Python
 * pqcrypto.sign.ml_dsa_44 的 sign()/verify()（不開放 context 參數）對齊。
 */

static uint8_t device_pk[PQC_IDENTITY_PUBLICKEYBYTES];
static uint8_t device_sk[PQC_IDENTITY_SECRETKEYBYTES];
static uint8_t device_key_ready = 0;

static uint8_t host_pk[PQC_IDENTITY_PUBLICKEYBYTES];
static uint8_t host_key_set = 0;

uint8_t PQCIdentity_RekeyDevice(void)
{
    int ret = PQCP_MLDSA_NATIVE_MLDSA44_keypair(device_pk, device_sk);
    device_key_ready = (ret == 0) ? 1 : 0;
    return device_key_ready;
}

uint8_t PQCIdentity_EnsureDeviceKeyReady(void)
{
    if (device_key_ready) {
        return 1;
    }
    return PQCIdentity_RekeyDevice();
}

uint8_t PQCIdentity_DeviceKeyIsReady(void)
{
    return device_key_ready;
}

const uint8_t *PQCIdentity_DevicePublicKey(void)
{
    return device_pk;
}

uint8_t PQCIdentity_SignWithDeviceKey(const uint8_t *msg, size_t msg_len,
                                      uint8_t *sig_out, size_t *siglen_out)
{
    int ret;

    if (!device_key_ready) {
        return 0;
    }

    ret = PQCP_MLDSA_NATIVE_MLDSA44_signature(sig_out, msg, msg_len, NULL, 0,
                                               device_sk);

    if (ret != 0) {
        return 0;
    }

    if (siglen_out != NULL) {
        *siglen_out = PQC_IDENTITY_SIGBYTES;
    }

    return 1;
}

uint8_t PQCIdentity_VerifyWithDeviceKey(const uint8_t *msg, size_t msg_len,
                                        const uint8_t *sig, size_t siglen)
{
    int ret;

    if (!device_key_ready) {
        return 0;
    }

    if (siglen != PQC_IDENTITY_SIGBYTES) {
        return 0;
    }

    ret = PQCP_MLDSA_NATIVE_MLDSA44_verify(sig, msg, msg_len, NULL, 0,
                                            device_pk);

    return (ret == 0) ? 1 : 0;
}

uint8_t PQCIdentity_SetHostPublicKey(const uint8_t *pk, uint32_t len)
{
    if (len != PQC_IDENTITY_PUBLICKEYBYTES) {
        return 0;
    }

    memcpy(host_pk, pk, len);
    host_key_set = 1;

    return 1;
}

uint8_t PQCIdentity_HostPublicKeyIsSet(void)
{
    return host_key_set;
}

uint8_t PQCIdentity_VerifyWithHostKey(const uint8_t *msg, size_t msg_len,
                                      const uint8_t *sig, size_t siglen)
{
    int ret;

    if (!host_key_set) {
        return 0;
    }

    if (siglen != PQC_IDENTITY_SIGBYTES) {
        return 0;
    }

    ret = PQCP_MLDSA_NATIVE_MLDSA44_verify(sig, msg, msg_len, NULL, 0,
                                            host_pk);

    return (ret == 0) ? 1 : 0;
}
