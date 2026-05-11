#include "uart3_protocol.h"
#include <stdio.h>
#include <string.h>

uint8_t uart3_rx_buffer[UART3_MAX_BUFFER_SIZE];


void UART3_Init(UART_HandleTypeDef *huart, uint32_t baudrate)
{
	huart->Instance = USART3;
	huart->Init.BaudRate = baudrate;
	huart->Init.WordLength = UART_WORDLENGTH_8B;
	huart->Init.StopBits = UART_STOPBITS_1;
	huart->Init.Parity = UART_PARITY_NONE;
	huart->Init.Mode = UART_MODE_TX_RX;
	huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart->Init.OverSampling = UART_OVERSAMPLING_16;
	huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
	huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

	if (HAL_UART_Init(huart) != HAL_OK){
		Error_Handler();
	}

	if (HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK){
		Error_Handler();
	}

	if (HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK){
		Error_Handler();
	}

	if (HAL_UARTEx_DisableFifoMode(huart) != HAL_OK){
		Error_Handler();
	}
}

void UART3_ClearRxBuffer(void)
{
	memset(uart3_rx_buffer, 0, UART3_MAX_BUFFER_SIZE);
}

void UART3_ClearHardwareRx(UART_HandleTypeDef *huart)
{
	volatile uint8_t dummy;

	if (huart == NULL)
	{
		return;
	}

	HAL_UART_AbortReceive(huart);

#if defined(UART_FLAG_RXFNE)
	while (__HAL_UART_GET_FLAG(huart, UART_FLAG_RXFNE))
	{
		dummy = (uint8_t)(huart->Instance->RDR & 0xFF);
		(void)dummy;
	}
#else
	while (__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE))
	{
		dummy = (uint8_t)(huart->Instance->RDR & 0xFF);
		(void)dummy;
	}
#endif

	__HAL_UART_CLEAR_OREFLAG(huart);
	__HAL_UART_CLEAR_FEFLAG(huart);
	__HAL_UART_CLEAR_NEFLAG(huart);
	__HAL_UART_CLEAR_PEFLAG(huart);
}

void UART3_ClearAll(UART_HandleTypeDef *huart)
{
	UART3_ClearRxBuffer();
	UART3_ClearHardwareRx(huart);
}

void UART3_SendStatus(UART_HandleTypeDef *huart, uint8_t status)
{
	HAL_UART_Transmit(huart, &status, 1, HAL_MAX_DELAY);
}

uint8_t UART3_ReceivePacket(UART_HandleTypeDef *huart, uint8_t *buffer, uint32_t MAX_DATA_SIZE, uint32_t *out_len)
{
	if (huart == NULL || buffer == NULL || out_len == NULL || MAX_DATA_SIZE == 0){
		return UART3_ERR_NULL_PTR;
	}

	uint8_t len_bytes[4];
	uint32_t len;
	uint32_t received = 0;

	// 1. 先收 4 bytes 總長度
	if (HAL_UART_Receive(huart, len_bytes, 4, HAL_MAX_DELAY) != HAL_OK){
		UART3_SendStatus(huart, UART3_ERR_RX_LEN);
		return UART3_ERR_RX_LEN;
	}

	// 2. big-endian length
	len =
	    ((uint32_t)len_bytes[0] << 24) |
	    ((uint32_t)len_bytes[1] << 16) |
	    ((uint32_t)len_bytes[2] << 8)  |
	    ((uint32_t)len_bytes[3]);

	// 3. 檢查長度
	if (len > MAX_DATA_SIZE){
		UART3_SendStatus(huart, UART3_ERR_LEN_TOO_BIG);
		return UART3_ERR_LEN_TOO_BIG;
	}

	// 4. 分次收 data
	while (received < len){
		uint32_t remain = len - received;
		uint16_t chunk_len;

		if (remain > UART3_CHUNK_SIZE) chunk_len = UART3_CHUNK_SIZE;
		else chunk_len = (uint16_t)remain;

		if (HAL_UART_Receive(huart, buffer + received, chunk_len, HAL_MAX_DELAY) != HAL_OK){
			UART3_SendStatus(huart, UART3_ERR_RX_DATA);
			return UART3_ERR_RX_DATA;
		}

		received += (uint32_t)chunk_len;
	}

	*out_len = len;

	// 5. 收完後回 OK
	UART3_SendStatus(huart, UART3_OK);

	return UART3_OK;
}

uint8_t UART3_SendPacket(UART_HandleTypeDef *huart, uint8_t *data, uint32_t MAX_DATA_SIZE, uint32_t len)
{
	uint8_t len_bytes[4];
	uint32_t sent = 0;

	if (len > MAX_DATA_SIZE){
		UART3_SendStatus(huart, UART3_ERR_LEN_TOO_BIG);
		return UART3_ERR_LEN_TOO_BIG;
	}

	len_bytes[0] = (uint8_t)((len >> 24) & 0xFF);
	len_bytes[1] = (uint8_t)((len >> 16) & 0xFF);
	len_bytes[2] = (uint8_t)((len >> 8) & 0xFF);
	len_bytes[3] = (uint8_t)(len & 0xFF);

	// 1. 先送 4 bytes 總長度
	if (HAL_UART_Transmit(huart, len_bytes, 4, HAL_MAX_DELAY) != HAL_OK){
		return UART3_ERR_TX_DATA;
	}

	// 2. 分次送 data
	while (sent < len){
		uint32_t remain = len - sent;
		uint16_t chunk_len;

		if (remain > UART3_CHUNK_SIZE) chunk_len = UART3_CHUNK_SIZE;
		else chunk_len = (uint16_t)remain;

		if (HAL_UART_Transmit(huart, data + sent, chunk_len, HAL_MAX_DELAY) != HAL_OK){
			return UART3_ERR_TX_DATA;
		}

		sent += (uint32_t)chunk_len;
	}

	return UART3_OK;
}


void UART3_Printf(UART_HandleTypeDef *huart, uint32_t timeout, const char *format, ...)
{
	char buffer[256];
	va_list args;
	uint16_t len;

	if (huart == NULL || format == NULL) return;

	va_start(args, format);
	len = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	if (len <= 0) return;

	if (len >= (int)sizeof(buffer)) len = sizeof(buffer) - 1;
	HAL_UART_Transmit(huart, (uint8_t *)buffer, (uint16_t)len, timeout);
}


uint16_t UART3_ReadLine(UART_HandleTypeDef *huart, uint8_t *read_data, uint32_t max_len, uint32_t timeout)
{
	uint16_t data_len = 0;
	uint8_t rx_char;
	HAL_StatusTypeDef status;

	if (huart == NULL || read_data == NULL || max_len == 0) return 0;

	memset(read_data, '\0', max_len);

	while (data_len < max_len - 1){
		status = HAL_UART_Receive(huart, &rx_char, 1, timeout);

		if (status == HAL_OK){
			if (rx_char == '\0') break;
			if (rx_char == '\n' || rx_char == '\r') break;

			read_data[data_len++] = rx_char;
		}
		else break;
	}

	read_data[data_len] = '\0';
	return data_len;
}
