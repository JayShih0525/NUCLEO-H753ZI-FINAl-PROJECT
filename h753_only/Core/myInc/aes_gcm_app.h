#ifndef AESGCM_APP_H
#define AESGCM_APP_H

#include "transport.h"
#include <stdint.h>

#define AES_GCM_APP_MAX_SIZE 131072

/* 註冊 CMD_AES_ENCRYPT / CMD_AES_DECRYPT 到 dispatcher，並設定初始 key */
uint8_t AESGCM_App_Init(void);

#endif /* AESGCM_APP_H */
