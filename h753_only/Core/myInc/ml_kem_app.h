#ifndef MLKEM_APP_H
#define MLKEM_APP_H

#include "transport.h"
#include <stdint.h>

#define ML_KEM_APP_OK           0
#define ML_KEM_APP_ERR_KEYPAIR  1

/* 產生第一組 keypair，並註冊 CMD_KEM_* 到 dispatcher */
uint8_t MLKEM_App_Init(void);

#endif /* MLKEM_APP_H */
