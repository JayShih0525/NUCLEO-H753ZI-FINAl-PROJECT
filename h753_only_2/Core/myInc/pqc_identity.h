#ifndef PQC_IDENTITY_H
#define PQC_IDENTITY_H

#include <stdint.h>
#include <stddef.h>

/*
 * 這一層把底層 ML-DSA-44 函式庫（mldsa-native）包起來，管理兩件事：
 *
 * 1. 裝置自己的 keypair —— mldsa_app.c 的一般簽章指令
 *    （GET_PUBKEY/SIGN/VERIFY/REKEY）跟 mlkem_app.c 的
 *    "device signs the KEM public key" 模式共用同一把 key，
 *    不要各自維護一份、以免兩邊 key 對不上。
 *
 * 2. Host（Python）的 public key —— 只有 mlkem_app.c 的
 *    "host signs the ciphertext" 模式會用到。這是 TOFU
 *    （trust-on-first-use）模型：host 開機時透過
 *    CMD_SET_HOST_MLDSA_PUBKEY 送一次，STM32 直接存進 RAM，
 *    沒有額外的憑證鏈驗證。這是 demo/教學專案的簡化，正式產品
 *    要換成有根憑證的機制。
 *
 * 底層演算法：ML-DSA-44（FIPS 204 最終標準），已用實際編譯 + 雙向
 * 簽章互驗證實跟 Python 的 pqcrypto.sign.ml_dsa_44 完全相容。
 */

#define PQC_IDENTITY_PUBLICKEYBYTES 1312
#define PQC_IDENTITY_SECRETKEYBYTES 2560
#define PQC_IDENTITY_SIGBYTES       2420

/* ---------------- 裝置自己的 keypair ---------------- */

/* 如果還沒有 key 就產生一把；已經有的話直接回傳成功，不重新產生。 */
uint8_t PQCIdentity_EnsureDeviceKeyReady(void);

/* 強制重新產生一把新的裝置 keypair（不管原本有沒有）。 */
uint8_t PQCIdentity_RekeyDevice(void);

uint8_t PQCIdentity_DeviceKeyIsReady(void);

/* 回傳裝置目前 public key 的指標（固定 PQC_IDENTITY_PUBLICKEYBYTES bytes）。
 * 呼叫前應先確認 PQCIdentity_DeviceKeyIsReady() 或呼叫過 EnsureDeviceKeyReady。 */
const uint8_t *PQCIdentity_DevicePublicKey(void);

/* 用裝置自己的 secret key 簽章。siglen_out 固定會被設成 PQC_IDENTITY_SIGBYTES。
 * 回傳 1 成功、0 失敗。 */
uint8_t PQCIdentity_SignWithDeviceKey(const uint8_t *msg, size_t msg_len,
                                      uint8_t *sig_out, size_t *siglen_out);

/* 用裝置自己的 public key 驗證（例如 MLDSA_VERIFY 指令、
 * 或自我測試）。回傳 1 = 驗證通過，0 = 驗證失敗或發生錯誤。 */
uint8_t PQCIdentity_VerifyWithDeviceKey(const uint8_t *msg, size_t msg_len,
                                        const uint8_t *sig, size_t siglen);

/* ---------------- Host 的 public key（TOFU） ---------------- */

uint8_t PQCIdentity_SetHostPublicKey(const uint8_t *pk, uint32_t len);
uint8_t PQCIdentity_HostPublicKeyIsSet(void);

/* 用先前存好的 host public key 驗證（KEM_AUTH_HOST_SIGNS 模式用）。
 * 回傳 1 = 驗證通過，0 = 驗證失敗、發生錯誤、或 host key 還沒設定。 */
uint8_t PQCIdentity_VerifyWithHostKey(const uint8_t *msg, size_t msg_len,
                                      const uint8_t *sig, size_t siglen);

#endif /* PQC_IDENTITY_H */
