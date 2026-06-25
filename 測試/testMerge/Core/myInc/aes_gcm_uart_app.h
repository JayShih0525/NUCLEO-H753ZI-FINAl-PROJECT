#ifndef AES_GCM_UART_APP_H
#define AES_GCM_UART_APP_H

#include "main.h"
#include "aes_gcm_lib.h"
#include <stdint.h>

// #define AES_GCM_APP_MAX_SIZE 32768
#define AES_GCM_APP_MAX_SIZE 65536
// #define AES_GCM_APP_MAX_SIZE 131072

uint8_t AESGCM_UART_Init(void);

void AESGCM_UART_EncryptTask(UART_HandleTypeDef *huart);
void AESGCM_UART_DecryptTask(UART_HandleTypeDef *huart);
void AESGCM_UART_CommandLoop(UART_HandleTypeDef *huart);

#endif
