/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Ai-WB2 WiFi Test at UART5 4Mbps + USART3 Debug
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
RNG_HandleTypeDef hrng;

/*
 * huart5：STM32 和 Ai-WB2 Wi-Fi 模組溝通用
 * huart3：STM32 印 debug 訊息到電腦 terminal 用
 */
UART_HandleTypeDef huart5;
UART_HandleTypeDef huart3;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_RNG_Init(void);
static void MX_UART5_Init(void);
static void MX_USART3_UART_Init(void);

/* USER CODE BEGIN PFP */
void Debug_Print(const char *msg);

void WiFi_Clear_RX(void);
void WiFi_Send(const char *cmd);
void WiFi_Read(uint32_t total_timeout_ms);

void WiFi_Basic_Test(void);
void WiFi_Connect_Test(void);
void WiFi_DNS_Test(void);
void WiFi_Socket_Command_Test(void);
void WiFi_Full_Test(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/*
 * Wi-Fi 模組回覆暫存 buffer
 *
 * 注意：
 * 目前這版還是 polling receive。
 * 4 Mbps 下短 AT 指令可以測，
 * 但如果之後要收大量資料，建議改 UART5 RX DMA circular buffer。
 */
uint8_t wifi_rx[8192];

/*
 * 印 debug 訊息到電腦 terminal
 *
 * USART3 通常接 ST-LINK Virtual COM Port。
 */
void Debug_Print(const char *msg)
{
  HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

/*
 * 清除 UART5 裡面殘留的舊資料
 *
 * 為什麼要清？
 * 因為 AT 指令回覆可能有 event 或舊資料殘留。
 * 送新指令前清一下，可以避免把舊 response 誤判成新 response。
 */
void WiFi_Clear_RX(void)
{
  uint8_t dummy;

  while (HAL_UART_Receive(&huart5, &dummy, 1, 1) == HAL_OK)
  {
    // discard old data
  }
}

/*
 * 送 AT 指令給 Ai-WB2
 *
 * 注意：
 * AT 指令結尾要有 \r\n
 * 例如：
 * WiFi_Send("AT\r\n");
 */
void WiFi_Send(const char *cmd)
{
  WiFi_Clear_RX();

  Debug_Print("\r\nSend to WiFi: ");
  Debug_Print(cmd);

  HAL_UART_Transmit(&huart5, (uint8_t *)cmd, strlen(cmd), HAL_MAX_DELAY);
}

/*
 * 讀取 Ai-WB2 回覆
 *
 * total_timeout_ms：
 * 表示最多等多久。
 *
 * 目前是 polling receive：
 * HAL_UART_Receive(&huart5, &ch, 1, 1)
 *
 * 優點：
 * 簡單。
 *
 * 缺點：
 * 4 Mbps 下長資料可能掉 byte。
 * 後面要傳大量資料時，建議改 DMA。
 */
void WiFi_Read(uint32_t total_timeout_ms)
{
  memset(wifi_rx, 0, sizeof(wifi_rx));

  uint32_t start = HAL_GetTick();
  uint16_t index = 0;
  uint8_t ch;

  /*
   * 先把 UART5 收到的資料存進 buffer。
   * 不要邊收邊用 USART3 印出來，
   * 否則 debug print 會拖慢速度，導致 UART5 掉字。
   */
  while ((HAL_GetTick() - start) < total_timeout_ms)
  {
    if (HAL_UART_Receive(&huart5, &ch, 1, 1) == HAL_OK)
    {
      if (index < sizeof(wifi_rx) - 1)
      {
        wifi_rx[index++] = ch;
      }
    }
  }

  wifi_rx[index] = '\0';

  Debug_Print("WiFi response:\r\n");

  if (index == 0)
  {
    Debug_Print("[No response]\r\n");
  }
  else
  {
    Debug_Print((char *)wifi_rx);
    Debug_Print("\r\n");
  }
}

/*
 * 基本 UART / AT 測試
 *
 * 這裡確認：
 * 1. UART5 4 Mbps 能通
 * 2. AT 指令有 OK
 * 3. UARTCFG 確認目前是 4000000
 * 4. GMR 查 firmware version
 */
void WiFi_Basic_Test(void)
{
  Debug_Print("\r\n===== Basic AT Test =====\r\n");

  /*
   * 最基本 AT 測試
   */
  WiFi_Send("AT\r\n");
  WiFi_Read(3000);

  HAL_Delay(500);

  /*
   * 關閉 echo
   *
   * ATE0 = 關閉 echo，回覆比較乾淨
   * ATE1 = 打開 echo，會把你送的指令也回傳
   */
  WiFi_Send("ATE0\r\n");
  WiFi_Read(3000);

  HAL_Delay(500);

  /*
   * 確認 UART baud rate
   *
   * 預期：
   * +UARTCFG:4000000,8,1,0
   */
  WiFi_Send("AT+UARTCFG?\r\n");
  WiFi_Read(5000);

  HAL_Delay(500);

  /*
   * 查 firmware 版本
   */
  WiFi_Send("AT+GMR\r\n");
  WiFi_Read(5000);
}

/*
 * Wi-Fi 連線測試
 *
 * 這裡確認：
 * 1. Wi-Fi mode
 * 2. 掃描指定 Wi-Fi
 * 3. 連上 Wi-Fi
 * 4. 取得 IP
 */
void WiFi_Connect_Test(void)
{
  Debug_Print("\r\n===== WiFi Connect Test =====\r\n");

  /*
   * 查 Wi-Fi mode
   *
   * +WMODE:1 通常代表 Station mode
   */
  WiFi_Send("AT+WMODE?\r\n");
  WiFi_Read(5000);

  HAL_Delay(500);

  /*
   * 掃描指定 Wi-Fi
   */
  WiFi_Send("AT+WSCAN=Eason0124\r\n");
  WiFi_Read(15000);

  HAL_Delay(500);

  /*
   * 連接 Wi-Fi
   *
   * 你的 Ai-WB2 firmware 用：
   * AT+WJAP=SSID,password
   *
   * 注意：
   * 這版不需要雙引號。
   */
  WiFi_Send("AT+WJAP=Eason0124,23209925\r\n");
  WiFi_Read(30000);

  HAL_Delay(1000);

  /*
   * 查目前 Wi-Fi 狀態與 IP
   *
   * 成功會看到：
   * +EVENT:WIFI_GOT_IP
   * 或 +WJAP 裡有 192.168.x.x
   */
  WiFi_Send("AT+WJAP?\r\n");
  WiFi_Read(10000);
}

/*
 * DNS 測試
 *
 * 這裡確認：
 * Ai-WB2 已經不只是連上 Wi-Fi，
 * 而且可以解析網域名稱。
 */
void WiFi_DNS_Test(void)
{
  Debug_Print("\r\n===== DNS Test =====\r\n");

  /*
   * 查 google.com 的 IP
   */
  WiFi_Send("AT+WDOMAIN=google.com\r\n");
  WiFi_Read(10000);

  HAL_Delay(500);

  /*
   * 查 example.com 的 IP
   */
  WiFi_Send("AT+WDOMAIN=example.com\r\n");
  WiFi_Read(10000);
}

/*
 * 下一步：Socket 指令格式探索
 *
 * 目的：
 * 你的 AT+HELP 裡有：
 * AT+SOCKET
 * AT+SOCKETSEND
 * AT+SOCKETREAD
 * AT+SOCKETDEL
 *
 * 但是不同 firmware 的 socket command 格式可能不一樣。
 * 所以先查 usage，不要直接猜。
 */
void WiFi_Socket_Command_Test(void)
{
  Debug_Print("\r\n===== Socket Command Test =====\r\n");

  /*
   * 查 SOCKET 指令是否支援 query usage
   */
  WiFi_Send("AT+SOCKET?\r\n");
  WiFi_Read(5000);

  HAL_Delay(500);

  /*
   * 有些 firmware 直接輸入 AT+SOCKET 會回 usage
   */
  WiFi_Send("AT+SOCKET\r\n");
  WiFi_Read(5000);

  HAL_Delay(500);

  /*
   * 查 SOCKETSEND 格式
   */
  WiFi_Send("AT+SOCKETSEND\r\n");
  WiFi_Read(5000);

  HAL_Delay(500);

  /*
   * 查 SOCKETREAD 格式
   */
  WiFi_Send("AT+SOCKETREAD\r\n");
  WiFi_Read(5000);

  HAL_Delay(500);

  /*
   * 查 SOCKETDEL 格式
   */
  WiFi_Send("AT+SOCKETDEL\r\n");
  WiFi_Read(5000);
}

/*
 * 完整測試流程
 */
void WiFi_Full_Test(void)
{
  Debug_Print("\r\n====================================\r\n");
  Debug_Print("Ai-WB2 Full WiFi Test\r\n");
  Debug_Print("UART5  = WiFi module\r\n");
  Debug_Print("USART3 = Debug console\r\n");
  Debug_Print("PB11   = ESP_EN\r\n");
  Debug_Print("UART5 baud = 4000000\r\n");
  Debug_Print("====================================\r\n");

  /*
   * Reset / Enable Wi-Fi module
   */
  Debug_Print("Reset / Enable WiFi module...\r\n");

  /*
   * PB11 = ESP_EN
   * LOW  = disable / reset Wi-Fi module
   * HIGH = enable Wi-Fi module
   */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
  HAL_Delay(1000);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);
  HAL_Delay(8000);

  /*
   * Step 1: 基本 AT 測試
   */
  WiFi_Basic_Test();

  HAL_Delay(1000);

  /*
   * Step 2: Wi-Fi 連線測試
   */
  WiFi_Connect_Test();

  HAL_Delay(1000);

  /*
   * Step 3: DNS 測試
   */
  WiFi_DNS_Test();

  HAL_Delay(1000);

  /*
   * Step 4: 下一步 socket 指令格式測試
   */
  WiFi_Socket_Command_Test();

  Debug_Print("\r\nFull WiFi test finished.\r\n");
}

/* USER CODE END 0 */

int main(void)
{
  HAL_Init();

  SystemClock_Config();

  MX_GPIO_Init();
  MX_RNG_Init();
  MX_UART5_Init();
  MX_USART3_UART_Init();

  /* USER CODE BEGIN 2 */

  Debug_Print("\r\nSystem start...\r\n");

  /*
   * 執行完整 Wi-Fi 測試
   */
  WiFi_Full_Test();

  /* USER CODE END 2 */

  while (1)
  {
    HAL_Delay(5000);

    /*
     * 每 5 秒送一次 AT，
     * 確認 Ai-WB2 還活著。
     */
    WiFi_Send("AT\r\n");
    WiFi_Read(3000);
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

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48 |
                                     RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK  |
                                RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 |
                                RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 |
                                RCC_CLOCKTYPE_D1PCLK1;

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
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;

  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief UART5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART5_Init(void)
{
  huart5.Instance = UART5;

  /*
   * Ai-WB2 目前已經設定成 4 Mbps。
   * 所以 STM32 UART5 開機也必須是 4 Mbps。
   */
  huart5.Init.BaudRate = 4000000;

  huart5.Init.WordLength = UART_WORDLENGTH_8B;
  huart5.Init.StopBits = UART_STOPBITS_1;
  huart5.Init.Parity = UART_PARITY_NONE;
  huart5.Init.Mode = UART_MODE_TX_RX;
  huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart5.Init.OverSampling = UART_OVERSAMPLING_16;
  huart5.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart5.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart5.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart5) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetTxFifoThreshold(&huart5, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetRxFifoThreshold(&huart5, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }

  /*
   * 4 Mbps 下建議開 FIFO。
   */
  if (HAL_UARTEx_EnableFifoMode(&huart5) != HAL_OK)
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

  /*
   * Debug console 給電腦看，不需要高速。
   */
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

  /*
   * USART3 只是 debug，用 disable FIFO 也可以。
   */
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
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /*
   * Enable GPIO clock
   */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*
   * PB11 = ESP_EN
   * 預設先拉低，避免模組還沒初始化就啟動。
   */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

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
}
#endif
