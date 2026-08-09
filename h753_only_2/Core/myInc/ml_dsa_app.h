#ifndef MLDSA_APP_H
#define MLDSA_APP_H

#include "transport.h"
#include <stdint.h>

/*
 * 註冊 CMD_MLDSA_* 到 dispatcher。key 延遲到第一次用到時才產生（跟原本行為一致）。
 *
 * 底層演算法是 ML-DSA-44（FIPS 204 最終標準，NIST 對 CRYSTALS-Dilithium
 * 正式標準化後的名稱），透過 pqc_identity.c 呼叫。這個模組本身不直接碰
 * mldsa-native，只透過 pqc_identity.c 的介面。
 */
void MLDSA_App_Init(void);

#endif /* MLDSA_APP_H */
