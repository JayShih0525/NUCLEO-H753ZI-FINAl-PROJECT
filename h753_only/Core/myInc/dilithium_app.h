#ifndef DILITHIUM_APP_H
#define DILITHIUM_APP_H

#include "transport.h"
#include <stdint.h>

/* 註冊 CMD_DILITHIUM_* 到 dispatcher。key 延遲到第一次用到時才產生（跟原本行為一致）。*/
void Dilithium_App_Init(void);

#endif /* DILITHIUM_APP_H */
