#include "randombytes.h"
#include "main.h"

RNG_HandleTypeDef hrng;

void Random_Init(void)
{
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
	// 1. 開 HSI48，RNG 常用這個 clock
	__HAL_RCC_HSI48_ENABLE();
	while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSI48RDY) == RESET){}

	// 2. 設定 RNG clock source = HSI48
	PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RNG;
	PeriphClkInitStruct.RngClockSelection = RCC_RNGCLKSOURCE_HSI48;
	if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK){
		Error_Handler();
	}

	// 3. 開 RNG peripheral clock
	__HAL_RCC_RNG_CLK_ENABLE();

	// 4. 初始化 RNG
	hrng.Instance = RNG;
	if (HAL_RNG_Init(&hrng) != HAL_OK){
		Error_Handler();
	}
}

int randombytes(uint8_t *buf, size_t len)
{
	uint32_t r;
	size_t i = 0;

	if (buf == NULL){
		Error_Handler();
		return -1;
	}

	while (i < len){
		if (HAL_RNG_GenerateRandomNumber(&hrng, &r) != HAL_OK){
			Error_Handler();
			return -1;
		}

		if (i < len) buf[i++] = (uint8_t)(r >> 0);
		if (i < len) buf[i++] = (uint8_t)(r >> 8);
		if (i < len) buf[i++] = (uint8_t)(r >> 16);
		if (i < len) buf[i++] = (uint8_t)(r >> 24);
	}

	return 0;
}
