/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : ESP32-S3 SPI Master <-> STM32H753 SPI Slave
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define SPI_PACKET_SIZE 32U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

RNG_HandleTypeDef hrng;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

static uint8_t spiTxBuffer[SPI_PACKET_SIZE];
static uint8_t spiRxBuffer[SPI_PACKET_SIZE];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_RNG_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART3_UART_Init(void);

/* USER CODE BEGIN PFP */

static void UART_SendString(const char *text);

static void UART_PrintBuffer(
		const char *title,
		const uint8_t *buffer,
		uint32_t length
);

static void PrepareTxBuffer(void);
static uint8_t CheckRxBuffer(uint8_t expectedValue);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief Send a null-terminated string through USART3.
  */
static void UART_SendString(const char *text) {
		if (text == NULL) {
				return;
		}

		HAL_UART_Transmit(
				&huart3,
				(uint8_t *)text,
				(uint16_t)strlen(text),
				HAL_MAX_DELAY
		);
}

/**
  * @brief Print a byte buffer in hexadecimal format.
  */
static void UART_PrintBuffer(const char *title, const uint8_t *buffer, uint32_t length) {
		char output[16];

		UART_SendString(title);

		for (uint32_t i = 0; i < length; i++) {
				snprintf(
						output,
						sizeof(output),
						"%02X ",
						buffer[i]
				);

				UART_SendString(output);
		}

		UART_SendString("\r\n");
}

/**
  * @brief Fill the STM32 TX buffer with 0xA5.
  */
static void PrepareTxBuffer(void) {
		for (uint32_t i = 0; i < SPI_PACKET_SIZE; i++) {
				spiTxBuffer[i] = 0xA5;
		}
}

/**
  * @brief Check whether all received bytes match the expected value.
  */
static uint8_t CheckRxBuffer(uint8_t expectedValue) {
		for (uint32_t i = 0; i < SPI_PACKET_SIZE; i++) {
				if (spiRxBuffer[i] != expectedValue) {
						return 0U;
				}
		}

		return 1U;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
		/* MCU Configuration------------------------------------------------------*/

		HAL_Init();

		SystemClock_Config();

		/* Initialize all configured peripherals */
		MX_GPIO_Init();
		MX_RNG_Init();
		MX_SPI1_Init();
		MX_USART3_UART_Init();

		/* USER CODE BEGIN 2 */

		PrepareTxBuffer();

		memset(spiRxBuffer, 0, sizeof(spiRxBuffer));

		UART_SendString("\r\n");
		UART_SendString("========================================\r\n");
		UART_SendString("STM32H753 SPI1 Slave started\r\n");
		UART_SendString("SPI mode: Mode 0\r\n");
		UART_SendString("SPI NSS: Hardware input PA4\r\n");
		UART_SendString("SPI SCK: PA5\r\n");
		UART_SendString("SPI MISO: PA6\r\n");
		UART_SendString("SPI MOSI: PB5\r\n");
		UART_SendString("Packet size: 32 bytes\r\n");
		UART_SendString("STM32 TX value: A5\r\n");
		UART_SendString("Expected ESP32 TX value: 55\r\n");
		UART_SendString("========================================\r\n");

		/* USER CODE END 2 */

		/* Infinite loop */
		/* USER CODE BEGIN WHILE */

		while (1) {
				memset(spiRxBuffer, 0, sizeof(spiRxBuffer));

				UART_SendString("\r\nWaiting for ESP32 SPI transaction...\r\n");

				/*
				* STM32 is SPI slave.
				*
				* The function waits until the ESP32:
				* 1. Pulls PA4/NSS LOW
				* 2. Generates 32 bytes of SPI clock
				* 3. Sends 32 bytes through MOSI
				*
				* At the same time STM32 sends 32 bytes of 0xA5
				* through PA6/MISO.
				*/
				HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
						&hspi1,
						spiTxBuffer,
						spiRxBuffer,
						SPI_PACKET_SIZE,
						HAL_MAX_DELAY
				);

				if (status == HAL_OK) {
						UART_SendString("SPI transaction completed\r\n");

						UART_PrintBuffer("STM32 received:    ", spiRxBuffer, SPI_PACKET_SIZE);

						UART_PrintBuffer("STM32 transmitted: ", spiTxBuffer, SPI_PACKET_SIZE);

						if (CheckRxBuffer(0x55U)) {
								UART_SendString("RX check: PASS, all bytes are 55\r\n");
						}
						else {
								UART_SendString("RX check: FAIL, received data is not all 55\r\n");
						}
				}
				else {
						char errorMessage[80];

						snprintf(
								errorMessage,
								sizeof(errorMessage),
								"SPI error, HAL status=%d, error code=0x%08lX\r\n",
								(int)status,
								HAL_SPI_GetError(&hspi1)
						);

						UART_SendString(errorMessage);

						HAL_SPI_Abort(&hspi1);

						__HAL_SPI_CLEAR_OVRFLAG(&hspi1);
						__HAL_SPI_CLEAR_UDRFLAG(&hspi1);

						HAL_Delay(100);
				}
		}

		/* USER CODE END WHILE */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    __HAL_PWR_VOLTAGESCALING_CONFIG(
        PWR_REGULATOR_VOLTAGE_SCALE2
    );

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    RCC_OscInitStruct.OscillatorType =
        RCC_OSCILLATORTYPE_HSI48 |
        RCC_OSCILLATORTYPE_HSI;

    RCC_OscInitStruct.HSIState =
        RCC_HSI_DIV1;

    RCC_OscInitStruct.HSICalibrationValue =
        RCC_HSICALIBRATION_DEFAULT;

    RCC_OscInitStruct.HSI48State =
        RCC_HSI48_ON;

    RCC_OscInitStruct.PLL.PLLState =
        RCC_PLL_ON;

    RCC_OscInitStruct.PLL.PLLSource =
        RCC_PLLSOURCE_HSI;

    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 15;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;

    RCC_OscInitStruct.PLL.PLLRGE =
        RCC_PLL1VCIRANGE_3;

    RCC_OscInitStruct.PLL.PLLVCOSEL =
        RCC_PLL1VCOWIDE;

    RCC_OscInitStruct.PLL.PLLFRACN = 0;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2 |
        RCC_CLOCKTYPE_D3PCLK1 |
        RCC_CLOCKTYPE_D1PCLK1;

    RCC_ClkInitStruct.SYSCLKSource =
        RCC_SYSCLKSOURCE_PLLCLK;

    RCC_ClkInitStruct.SYSCLKDivider =
        RCC_SYSCLK_DIV1;

    RCC_ClkInitStruct.AHBCLKDivider =
        RCC_HCLK_DIV1;

    RCC_ClkInitStruct.APB3CLKDivider =
        RCC_APB3_DIV1;

    RCC_ClkInitStruct.APB1CLKDivider =
        RCC_APB1_DIV2;

    RCC_ClkInitStruct.APB2CLKDivider =
        RCC_APB2_DIV1;

    RCC_ClkInitStruct.APB4CLKDivider =
        RCC_APB4_DIV1;

    if (HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct,
            FLASH_LATENCY_2
        ) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{
    hrng.Instance = RNG;

    hrng.Init.ClockErrorDetection =
        RNG_CED_ENABLE;

    if (HAL_RNG_Init(&hrng) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;

    /*
     * ESP32-S3 is Master.
     * STM32H753 must be Slave.
     */
    hspi1.Init.Mode =
        SPI_MODE_SLAVE;

    hspi1.Init.Direction =
        SPI_DIRECTION_2LINES;

    hspi1.Init.DataSize =
        SPI_DATASIZE_8BIT;

    /*
     * SPI Mode 0:
     * CPOL = 0
     * CPHA = 0
     */
    hspi1.Init.CLKPolarity =
        SPI_POLARITY_LOW;

    hspi1.Init.CLKPhase =
        SPI_PHASE_1EDGE;

    /*
     * Important:
     * PA4 is controlled by ESP32 GPIO14.
     */
    hspi1.Init.NSS =
        SPI_NSS_HARD_INPUT;

    hspi1.Init.BaudRatePrescaler =
        SPI_BAUDRATEPRESCALER_2;

    hspi1.Init.FirstBit =
        SPI_FIRSTBIT_MSB;

    hspi1.Init.TIMode =
        SPI_TIMODE_DISABLE;

    hspi1.Init.CRCCalculation =
        SPI_CRCCALCULATION_DISABLE;

    hspi1.Init.CRCPolynomial = 7;

    hspi1.Init.CRCLength =
        SPI_CRC_LENGTH_DATASIZE;

    hspi1.Init.NSSPMode =
        SPI_NSS_PULSE_DISABLE;

    hspi1.Init.NSSPolarity =
        SPI_NSS_POLARITY_LOW;

    hspi1.Init.FifoThreshold =
        SPI_FIFO_THRESHOLD_01DATA;

    hspi1.Init.TxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;

    hspi1.Init.RxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;

    hspi1.Init.MasterSSIdleness =
        SPI_MASTER_SS_IDLENESS_00CYCLE;

    hspi1.Init.MasterInterDataIdleness =
        SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;

    hspi1.Init.MasterReceiverAutoSusp =
        SPI_MASTER_RX_AUTOSUSP_DISABLE;

    hspi1.Init.MasterKeepIOState =
        SPI_MASTER_KEEP_IO_STATE_DISABLE;

    hspi1.Init.IOSwap =
        SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{
    huart3.Instance = USART3;

    huart3.Init.BaudRate =
        115200;

    huart3.Init.WordLength =
        UART_WORDLENGTH_8B;

    huart3.Init.StopBits =
        UART_STOPBITS_1;

    huart3.Init.Parity =
        UART_PARITY_NONE;

    huart3.Init.Mode =
        UART_MODE_TX_RX;

    huart3.Init.HwFlowCtl =
        UART_HWCONTROL_NONE;

    huart3.Init.OverSampling =
        UART_OVERSAMPLING_16;

    huart3.Init.OneBitSampling =
        UART_ONE_BIT_SAMPLE_DISABLE;

    huart3.Init.ClockPrescaler =
        UART_PRESCALER_DIV1;

    huart3.AdvancedInit.AdvFeatureInit =
        UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart3) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_SetTxFifoThreshold(
            &huart3,
            UART_TXFIFO_THRESHOLD_1_8
        ) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_SetRxFifoThreshold(
            &huart3,
            UART_RXFIFO_THRESHOLD_1_8
        ) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
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
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT

void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}

#endif /* USE_FULL_ASSERT */
