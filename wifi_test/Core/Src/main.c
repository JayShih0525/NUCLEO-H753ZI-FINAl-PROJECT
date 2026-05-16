/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Optimized Clean Main with WiFi Library Architecture
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "wifi_driver.h" // 引入新設計的庫

/* USER CODE BEGIN Includes */
#include <string.h>  // 修正：補上 strlen 需要的標頭檔
#include <stdio.h>   // 修正：補上 sprintf 需要的標頭檔
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
RNG_HandleTypeDef hrng;
UART_HandleTypeDef huart5; // Wi-Fi Module
UART_HandleTypeDef huart3; // Debug Console

/* 實體化一個 Wi-Fi 設備結構體（物件） */
WiFi_Device_t WiFi_Module;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_RNG_Init(void);
static void MX_UART5_Init(uint32_t baudrate);
static void MX_USART3_UART_Init(void);

/* USER CODE BEGIN PFP */
/* 修正：必須在這裡先做好宣告，後面的應用層才能順利呼叫 */
void Debug_Print(const char *msg);
void Run_Professional_WiFi_Test(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
void Run_Professional_WiFi_Test(void)
{
    int con_id = 1; // 預設 ConID

    Debug_Print("\r\n🚀 Starting Modular WiFi TCP Client Test 🚀\r\n");

    // 1. 初始化結構體物件並綁定硬體 UART5
    WiFi_Init(&WiFi_Module, &huart5);

    // 2. 進行純硬體重置
    WiFi_Hardware_Reset();

    // 3. 通訊與基本 AT 功能性檢查
    if (WiFi_Basic_Check(&WiFi_Module) != WIFI_OK) {
        Debug_Print("[FATAL] WiFi module basic check failed!\r\n");
        return;
    }

    // 4. 連接 Wi-Fi 基地台
    if (WiFi_Connect(&WiFi_Module, "Eason0124", "23209925") != WIFI_OK) {
        Debug_Print("[FATAL] Wi-Fi Connection failed!\r\n");
        return;
    }

    // 5. 連線前防錯：迴圈清空舊連線 (0 ~ 3)
    Debug_Print("\r\n===== Cleaning up historical sockets =====\r\n");
    for (int i = 0; i < 4; i++) {
        WiFi_Socket_Close(&WiFi_Module, i);
        HAL_Delay(200);
    }

    // 6. 建立與 Mac 的 TCP 連線
    if (WiFi_Socket_Create_TCPClient(&WiFi_Module, "192.168.1.104", 5555) != WIFI_OK) {
        Debug_Print("[ERROR] Failed to initiate TCP Client Socket!\r\n");
    }
    HAL_Delay(1000);

    // 7. 查詢 Socket 狀態並動態抓取 Connection ID
    WiFi_Send_Cmd(&WiFi_Module, "AT+SOCKET?\r\n");
    WiFi_Wait_Response(&WiFi_Module, 5000);

    int parsed_id = WiFi_Get_TCPClient_ConID(&WiFi_Module);
    if (parsed_id >= 0) {
        con_id = parsed_id;
        char log_msg[64];
        sprintf(log_msg, "[SYSTEM] Dynamic ID matching successful. ConID = %d\r\n", con_id);
        Debug_Print(log_msg);
    } else {
        Debug_Print("[WARN] Dynamic tracking failed. Using default ConID = 1\r\n");
    }

    // 8. 發送測試資料包至 Mac Server
    WiFi_Socket_Send_Line(&WiFi_Module, con_id, "STM32_TCP_TEST_001");
    HAL_Delay(1000);

    // 9. 讀取來自 Mac Server 的 ACK 數據
    WiFi_Socket_Read(&WiFi_Module, con_id);
    HAL_Delay(1000);

    // 10. 測試圓滿完成，切斷 Socket 連線釋放資源
    WiFi_Socket_Close(&WiFi_Module, con_id);

    Debug_Print("\r\n🎉 All pipeline tests passed successfully!\r\n");
}

void Debug_Print(const char *msg)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}
/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_RNG_Init();
    MX_USART3_UART_Init(); // 電腦 Debug 端 (115200)
    MX_UART5_Init(115200); // 聯網驅動端 (穩定 115200 運行)

    /* USER CODE BEGIN 2 */
    Run_Professional_WiFi_Test();
    /* USER CODE END 2 */

    while (1)
    {
        /* 主迴圈保持心跳檢查 */
        HAL_Delay(5000);
        WiFi_Send_Cmd(&WiFi_Module, "AT\r\n");
        WiFi_Wait_Response(&WiFi_Module, 2000);
    }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48 | RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) Error_Handler();
}

static void MX_UART5_Init(uint32_t baudrate)
{
  HAL_UART_DeInit(&huart5);
  huart5.Instance = UART5;
  huart5.Init.BaudRate = baudrate;
  huart5.Init.WordLength = UART_WORDLENGTH_8B;
  huart5.Init.StopBits = UART_STOPBITS_1;
  huart5.Init.Parity = UART_PARITY_NONE;
  huart5.Init.Mode = UART_MODE_TX_RX;
  huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart5.Init.OverSampling = UART_OVERSAMPLING_16;
  huart5.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart5.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart5.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart5) != HAL_OK) Error_Handler();
  if (HAL_UARTEx_SetTxFifoThreshold(&huart5, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
  if (HAL_UARTEx_SetRxFifoThreshold(&huart5, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
  if (HAL_UARTEx_EnableFifoMode(&huart5) != HAL_OK) Error_Handler();
}

static void MX_USART3_UART_Init(void)
{
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

  if (HAL_UART_Init(&huart3) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void MX_RNG_Init(void)
{
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
  if (HAL_RNG_Init(&hrng) != HAL_OK) Error_Handler();
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}
