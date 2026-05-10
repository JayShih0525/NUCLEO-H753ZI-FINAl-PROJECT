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
#include "ml_kem_lib.h"

UART_HandleTypeDef huart3;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);

static uint8_t mlkem_pk[MLKEM_PUBLIC_KEY_SIZE];
static uint8_t mlkem_sk[MLKEM_SECRET_KEY_SIZE];
static uint8_t mlkem_ct[MLKEM_CIPHERTEXT_SIZE];
static uint8_t mlkem_ss1[MLKEM_SHARED_SIZE];
static uint8_t mlkem_ss2[MLKEM_SHARED_SIZE];
volatile uint8_t mlkem_test_result = 0;

void MLKEM_Local_Test_NoUART(void)
{
	uint8_t status;

	mlkem_test_result = 0;

	memset(mlkem_pk, 0, sizeof(mlkem_pk));
	memset(mlkem_sk, 0, sizeof(mlkem_sk));
	memset(mlkem_ct, 0, sizeof(mlkem_ct));
	memset(mlkem_ss1, 0, sizeof(mlkem_ss1));
	memset(mlkem_ss2, 0, sizeof(mlkem_ss2));

	status = MLKEM_Keypair(mlkem_pk, mlkem_sk);

	if (status != MLKEM_OK){
		mlkem_test_result = 2;
		Error_Handler();
	}

	status = MLKEM_Encapsulate(
		mlkem_ct,
		mlkem_ss1,
		mlkem_pk
	);

	if (status != MLKEM_OK){
		mlkem_test_result = 3;
		Error_Handler();
	}

	status = MLKEM_Decapsulate(
		mlkem_ss2,
		mlkem_ct,
		mlkem_sk
	);

	if (status != MLKEM_OK){
		mlkem_test_result = 4;
		Error_Handler();
	}

	if (memcmp(mlkem_ss1, mlkem_ss2, MLKEM_SHARED_SIZE) == 0){
		mlkem_test_result = 1;   // PASS
	}

	else{
		mlkem_test_result = 5;   // FAIL
		Error_Handler();
	}

	UART3_Printf(&huart3, 100, "%d\n", mlkem_test_result);
}

int main(void)
{
	HAL_Init();
	SystemClock_Config();
	MX_GPIO_Init();
	UART3_Init(&huart3, 1000000);
	Random_Init();

	UART3_ClearAll(&huart3);

	while(1){
		MLKEM_Local_Test_NoUART();
	}

}






/**
* @brief System Clock Configuration
* @retval None
*/
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	/** Supply configuration update enable
	*/
	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

	/** Configure the main internal regulator output voltage
	*/
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

	while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

	/** Initializes the RCC Oscillators according to the specified parameters
	* in the RCC_OscInitTypeDef structure.
	*/
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	*/
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
		    |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
		    |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
	RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
	RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
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
