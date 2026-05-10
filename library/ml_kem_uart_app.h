#ifndef ML_KEM_UART_APP_H
#define ML_KEM_UART_APP_H

#include "main.h"
#include <stdint.h>

#define ML_KEM_UART_OK                    					0x00
#define ML_KEM_UART_ERR_KEYPAIR           			0xA1
#define ML_KEM_UART_ERR_RX_CIPHERTEXT     	0xA2
#define ML_KEM_UART_ERR_CIPHERTEXT_LEN    	0xA3
#define ML_KEM_UART_ERR_DECAPSULATE       		0xA4
#define ML_KEM_UART_ERR_AES_KEY           			0xA5
#define ML_KEM_UART_ERR_NOT_READY         		0xA6
#define ML_KEM_UART_ERR_ENCAPSULATE       		0xA7

#define ML_KEM_CMD_BUFFER_SIZE            			256

uint8_t MLKEM_UART_Init(void);

void MLKEM_UART_SendPublicKeyTask(UART_HandleTypeDef *huart);
void MLKEM_UART_DecapsulateTask(UART_HandleTypeDef *huart);
void MLKEM_UART_EncapsulateTask(UART_HandleTypeDef *huart);
void MLKEM_UART_RekeyTask(UART_HandleTypeDef *huart);
void MLKEM_UART_CommandLoop(UART_HandleTypeDef *huart);

#endif
