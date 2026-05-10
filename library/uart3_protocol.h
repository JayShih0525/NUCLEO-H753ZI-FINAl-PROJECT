#ifndef UART3_PROTOCOL_H
#define UART3_PROTOCOL_H

#include "main.h"
#include <stdint.h>
#include <stdarg.h>

#define UART3_MAX_BUFFER_SIZE   65535
#define UART3_CHUNK_SIZE    		4096

#define UART3_OK                			0x00
#define UART3_ERR_LEN_TOO_BIG   	0xE1
#define UART3_ERR_RX_LEN        		0xE2
#define UART3_ERR_RX_DATA       	0xE3
#define UART3_ERR_TX_DATA       	0xE4

extern uint8_t uart3_rx_buffer[UART3_MAX_BUFFER_SIZE];

void UART3_Init(UART_HandleTypeDef *huart, uint32_t baudrate);

void UART3_ClearRxBuffer(void);
void UART3_ClearHardwareRx(UART_HandleTypeDef *huart);
void UART3_ClearAll(UART_HandleTypeDef *huart);

void UART3_SendStatus(
	UART_HandleTypeDef *huart,
	uint8_t status
);

uint8_t UART3_ReceivePacket(
	UART_HandleTypeDef *huart,
	uint8_t *buffer,
	uint32_t *out_len
);

uint8_t UART3_SendPacket(
	UART_HandleTypeDef *huart,
	uint8_t *data,
	uint32_t len
);

void UART3_Printf(
	UART_HandleTypeDef *huart,
	uint32_t timeout,
	const char *format, ...
);

uint16_t UART3_ReadLine(
	UART_HandleTypeDef *huart,
	uint8_t *read_data,
	uint32_t max_len,
	uint32_t timeout
);

#endif
