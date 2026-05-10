/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "string.h"
#include "uart3_protocol.h"
#include "randombytes.h"
#include "ml_kem_uart_app.h"
#include "aes_gcm_uart_app.h"

UART_HandleTypeDef huart3;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);


static uint8_t app_cmd_buffer[256];
void APP_CommandLoop(UART_HandleTypeDef *huart)
{
	uint16_t cmd_len;
	cmd_len = UART3_ReadLine(
		huart,
		app_cmd_buffer,
		sizeof(app_cmd_buffer),
		HAL_MAX_DELAY
	);

	if (cmd_len == 0) return;

	// ML-KEM
	if (strcmp((char *)app_cmd_buffer, "GET_KEM_PUBLIC_KEY") == 0){
		MLKEM_UART_SendPublicKeyTask(huart);
	}

	else if (strcmp((char *)app_cmd_buffer, "KEM_DECAPSULATE") == 0){
		MLKEM_UART_DecapsulateTask(huart);
	}

	else if (strcmp((char *)app_cmd_buffer, "KEM_REKEY") == 0){
		MLKEM_UART_RekeyTask(huart);
	}

	else if (strcmp((char *)app_cmd_buffer, "KEM_ENCAPSULATE") == 0){
		MLKEM_UART_EncapsulateTask(huart);
	}

	// AES-GCM
	else if (strcmp((char *)app_cmd_buffer, "ENCRYPT") == 0){
		AESGCM_UART_EncryptTask(huart);
	}
	else if (strcmp((char *)app_cmd_buffer, "DECRYPT") == 0){
		AESGCM_UART_DecryptTask(huart);
	}

	// Common
	else if (strcmp((char *)app_cmd_buffer, "CLEAR") == 0){
		UART3_ClearRxBuffer();
		UART3_ClearHardwareRx(huart);
		HAL_Delay(1);
		UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");
	}

	else{
		UART3_Printf(huart, HAL_MAX_DELAY, "UNKNOWN_CMD\n");
	}

}


int main(void)
{
	HAL_Init();
	SystemClock_Config();
	MX_GPIO_Init();
	UART3_Init(&huart3, 4000000); // 4000000 is Maximum baud rate
	Random_Init();

	UART3_ClearAll(&huart3);


	while (1){
		APP_CommandLoop(&huart3);
	}
}






/**
* @brief System Clock Configuration
* @retval None
*/
//void SystemClock_Config(void)
//{
//	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
//	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
//
//	/** Supply configuration update enable
//	*/
//	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
//
//	/** Configure the main internal regulator output voltage
//	*/
//	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
//
//	while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
//
//	/** Initializes the RCC Oscillators according to the specified parameters
//	* in the RCC_OscInitTypeDef structure.
//	*/
//	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
//	RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
//	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
//	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
//	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
//	{
//		Error_Handler();
//	}
//
//	/** Initializes the CPU, AHB and APB buses clocks
//	*/
//	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
//		    |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
//		    |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
//	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
//	RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
//	RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
//	RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
//	RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
//	RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
//	RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
//
//	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
//	{
//		Error_Handler();
//	}
//}

void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	/*
	 * Supply configuration
	 */
	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

	/*
	 * Voltage scaling for high frequency
	 * VOS1 is needed for 400 MHz range.
	 */
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

	while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
	{
	}

	/*
	 * HSI = 64 MHz
	 *
	 * PLL input  = HSI / PLLM = 64 / 4 = 16 MHz
	 * PLL VCO    = 16 * PLLN = 16 * 50 = 800 MHz
	 * SYSCLK     = VCO / PLLP = 800 / 2 = 400 MHz
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = 4;
	RCC_OscInitStruct.PLL.PLLN = 50;
	RCC_OscInitStruct.PLL.PLLP = 2;
	RCC_OscInitStruct.PLL.PLLQ = 2;
	RCC_OscInitStruct.PLL.PLLR = 2;
	RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
	RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
	RCC_OscInitStruct.PLL.PLLFRACN = 0;

	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	/*
	 * CPU clock = 400 MHz
	 * AHB clock = 200 MHz
	 * APB clocks = 100 MHz
	 *
	 * This is safer for STM32H7 bus limits.
	 */
	RCC_ClkInitStruct.ClockType =
		RCC_CLOCKTYPE_HCLK  |
		RCC_CLOCKTYPE_SYSCLK |
		RCC_CLOCKTYPE_PCLK1 |
		RCC_CLOCKTYPE_PCLK2 |
		RCC_CLOCKTYPE_D3PCLK1 |
		RCC_CLOCKTYPE_D1PCLK1;

	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
	RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
* @brief GPIO Initialization Function
* @param None
* @retval None
*/
static void MX_GPIO_Init(void)
{
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
* @brief  This function is executed in case of error occurrence.
* @retval None
*/
void Error_Handler(void)
{
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1)
	{
	}
	/* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
* @brief  Reports the name of the source file and the source line number
*         where the assert_param error has occurred.
* @param  file: pointer to the source file name
* @param  line: assert_param error line source number
* @retval None
*/
void assert_failed(uint8_t *file, uint32_t line)
{
	/* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
	ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	/* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
