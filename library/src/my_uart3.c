
#include "my_uart3.h"
#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart3;
uint8_t uart3_read_data[256];
static uint32_t timeout = 1000; // 1s

void UART3_GPIO_Init(void)
{
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
}

void UART3_Init(void)
{
	UART3_GPIO_Init();

	huart3.Instance = USART3;
	huart3.Init.BaudRate = 115200;
	huart3.Init.WordLength = UART_WORDLENGTH_8B;
	huart3.Init.StopBits = UART_STOPBITS_1;
	huart3.Init.Parity = UART_PARITY_NONE;
	huart3.Init.Mode = UART_MODE_TX_RX;
	huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart3.Init.OverSampling = UART_OVERSAMPLING_16;
	huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
	huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&huart3) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
	{
		Error_Handler();
	}
}

void printf_uart3(const char *format, ...)
{
	char buffer[256];
	va_list args;
	va_start(args, format);
	int len = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	if (len > 0) {
		HAL_UART_Transmit(&huart3, (uint8_t *)buffer, len, timeout);
	}
}

uint16_t read_uart3(uint32_t timeout)
{
	static uint16_t  max_len = sizeof(uart3_read_data) / sizeof(uint8_t);
	uint16_t data_len = 0;
	uint8_t rx_char;
	HAL_StatusTypeDef status;

	memset(uart3_read_data, '\0', sizeof(uart3_read_data));

	while (data_len < max_len - 1){
		status = HAL_UART_Receive(&huart3, &rx_char, 1, timeout);

		if (status == HAL_OK){

			if(rx_char == '\0') continue;

			if(rx_char == '\n' || rx_char == '\r'){
				uart3_read_data[data_len++] = '\0';
				break;
			}
			else uart3_read_data[data_len++] = rx_char;

		}

		else break;
	}

	return data_len;
}

