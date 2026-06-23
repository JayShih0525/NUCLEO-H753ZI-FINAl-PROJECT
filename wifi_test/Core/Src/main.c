/**
  ******************************************************************************
  * @file    main.c
  * @brief   MCU 主程式 — WSRP Reliable Windowed WiFi Transfer
  ******************************************************************************
  */

#include "main.h"
#include "wifi_driver.h"
#include "wsrp_protocol.h"
#include <string.h>
#include <stdio.h>

/* ════════════════════════════════════════════════════════════════
 *  設定（修改這裡）
 * ════════════════════════════════════════════════════════════════ */
#define WIFI_SSID       "Jay Shih"
#define WIFI_PASSWORD   "12345678"
#define SERVER_IP       "172.20.10.3"
#define SERVER_PORT     5555

/* 傳輸 buffer 大小（根據 RAM 調整） */
#define TX_BUF_SIZE          (128 * 1024)   /* 128 KB */

/*
 * Full test: TEST_FIRST_OBJECT_ID=1, TEST_OBJECT_COUNT=10
 * Only test object 2: TEST_FIRST_OBJECT_ID=2, TEST_OBJECT_COUNT=1
 */
#define TEST_FIRST_OBJECT_ID  1u
#define TEST_OBJECT_COUNT    100u
#define OBJECT_GAP_MS        100u

static uint8_t tx_buf[TX_BUF_SIZE];

/* ─── 外設 Handle ──────────────────────────────────────────── */
RNG_HandleTypeDef  hrng;
UART_HandleTypeDef huart5;   /* Wi-Fi (Ai-WB2) */
UART_HandleTypeDef huart3;   /* Debug Console  */
WiFi_Device_t      WiFi_Module;

/* ─── 前向宣告 ─────────────────────────────────────────────── */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_RNG_Init(void);
static void MX_UART5_Init(uint32_t baudrate);
static void MX_USART3_UART_Init(void);
void Debug_Print(const char *msg);

/* ════════════════════════════════════════════════════════════════
 *  WiFi 初始化 + 波特率切換 + TCP 建立
 *
 *  流程：
 *  1. 用 WIFI_INIT_BAUD 開機（需符合模組目前設定）
 *  2. WiFi_Basic_Check 內部設定/確認 WIFI_HIGH_BAUD
 *  3. 連 AP
 *  4. 建立 TCP
 * ════════════════════════════════════════════════════════════════ */
static int App_WiFi_Connect(void) {
    /* Step 1: 用模組目前實際開機 baud 初始化 */
    WiFi_Init(&WiFi_Module, &huart5);
    WiFi_Hardware_Reset();

    /* Step 2: Basic Check 內部會切換到 WIFI_HIGH_BAUD */
    if (WiFi_Basic_Check(&WiFi_Module) != WIFI_OK) {
        Debug_Print("[APP] WiFi basic check failed\r\n");
        return -1;
    }

    /* Step 3: 連 AP（此時已是高速 baud） */
    if (WiFi_Connect(&WiFi_Module, WIFI_SSID, WIFI_PASSWORD) != WIFI_OK) {
        Debug_Print("[APP] WiFi connect failed\r\n");
        return -1;
    }

    /*
     * Step 4: 啟用 socket 下行接收。
     *
     * Ai-WB2 預設 socket receive mode 為 passive mode；在 passive mode，
     * server 回傳的 WSRP HELLO_ACK / ACK 不會作為資料交給 UART parser。
     * WSRP 是雙向協議，因此進入 transparent mode 前必須設為 active。
     */
    if (WiFi_Socket_SetReceiveMode(&WiFi_Module, 1u) != WIFI_OK) {
        Debug_Print("[APP] Cannot enable socket active receive mode\r\n");
        return -1;
    }
    Debug_Print("[APP] Socket receive mode ACTIVE\r\n");

    /* Step 5: 清舊 socket */
    for (int i = 0; i < 4; i++) {
        WiFi_Socket_Close(&WiFi_Module, i);
        HAL_Delay(200);
    }

    /* Step 6: 建立 TCP */
    if (WiFi_Socket_Create_TCPClient(&WiFi_Module,
                                      SERVER_IP, SERVER_PORT) != WIFI_OK) {
        Debug_Print("[APP] TCP connect failed\r\n");
        return -1;
    }
    HAL_Delay(1000);

    /* Step 7: 取得 ConID */
    WiFi_Send_Cmd(&WiFi_Module, "AT+SOCKET?\r\n");
    WiFi_Wait_Response(&WiFi_Module, 5000);
    int con_id = WiFi_Get_TCPClient_ConID(&WiFi_Module);
    if (con_id < 0) con_id = 1;

    char log[64];
    snprintf(log, sizeof(log), "[APP] Connected, ConID=%d, Baud=%lu\r\n",
             con_id, (unsigned long)WIFI_HIGH_BAUD);
    Debug_Print(log);
    return con_id;
}

/* ════════════════════════════════════════════════════════════════
 *  main
 * ════════════════════════════════════════════════════════════════ */
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_RNG_Init();
    MX_USART3_UART_Init();
    MX_UART5_Init(WIFI_INIT_BAUD);  /* 必須等於 Ai-WB2 目前實際開機 baud */

    Debug_Print("\r\n=== WSRP Reliable Windowed WiFi Transfer ===\r\n");

    /* Step 1: 連 Wi-Fi 並建立一條長時間保持的 TCP connection */
    int con_id = App_WiFi_Connect();
    if (con_id < 0) {
        Debug_Print("[FATAL] Connect failed\r\n");
        while (1) {}
    }

    /* Step 2: 進入 transparent mode 後，以 binary WSRP 協議通訊 */
    WSRP_Init(&WiFi_Module, con_id);
    if (WSRP_EnterTransparent() != WSRP_STATUS_OK) {
        Debug_Print("[FATAL] Cannot enter transparent mode\r\n");
        while (1) {}
    }

    if (WSRP_Handshake() != WSRP_STATUS_OK) {
        Debug_Print("[FATAL] Protocol handshake failed\r\n");
        while (1) {}
    }

    /* 測試資料：每筆 object 為 128 KiB；未來可換成 JPEG/ciphertext buffer */
    for (uint32_t i = 0; i < TX_BUF_SIZE; i++) {
        tx_buf[i] = (uint8_t)(i & 0xFFu);
    }

    /* Step 3: 傳 10 筆獨立 object；server 依 START.total_len 自動存成 10 個檔案 */
    char log[120];
    for (uint32_t object_id = TEST_FIRST_OBJECT_ID;
         object_id < (TEST_FIRST_OBJECT_ID + TEST_OBJECT_COUNT);
         object_id++) {
        uint32_t t0 = HAL_GetTick();
        WSRP_Status_t st = WSRP_SendObject(
            WSRP_DATA_RAW,
            WSRP_MODE_RELIABLE,
            object_id,
            tx_buf,
            TX_BUF_SIZE
        );
        uint32_t elapsed = HAL_GetTick() - t0;

        if (st != WSRP_STATUS_OK) {
            snprintf(log, sizeof(log),
                     "[APP] Object %lu failed, status=%d\r\n",
                     (unsigned long)object_id, (int)st);
            Debug_Print(log);
            break;
        }

        snprintf(log, sizeof(log),
                 "[APP] Object %lu done: %lu bytes, %lu ms, ~%lu KiB/s\r\n",
                 (unsigned long)object_id,
                 (unsigned long)TX_BUF_SIZE,
                 (unsigned long)elapsed,
                 (unsigned long)((TX_BUF_SIZE / 1024u) * 1000u /
                                  (elapsed > 0u ? elapsed : 1u)));
        Debug_Print(log);

        /*
         * 讓 Ai-WB2/TCP transmission queue 在下一筆 object 前穩定下來。
         * 十筆都穩定完成後，再測試 50 ms、10 ms、0 ms。
         */
        HAL_Delay(OBJECT_GAP_MS);
    }

    /* 測試完成後退出；長時間攝影串流應維持連線並持續呼叫 WSRP_SendObject() */
    WSRP_ExitTransparent();
    WiFi_Socket_Close(&WiFi_Module, con_id);
    Debug_Print("[APP] Done.\r\n");

    while (1) { HAL_Delay(10000); }
}

/* ─── Debug 輸出 ────────────────────────────────────────────── */
void Debug_Print(const char *msg) {
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

/* ─── 週邊初始化 ────────────────────────────────────────────── */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|
                                  RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2|
                                  RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource    = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.SYSCLKDivider   = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider   = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider  = RCC_APB3_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider  = RCC_APB1_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider  = RCC_APB2_DIV1;
    RCC_ClkInitStruct.APB4CLKDivider  = RCC_APB4_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) Error_Handler();
}
static void MX_UART5_Init(uint32_t baudrate) {
    HAL_UART_DeInit(&huart5);
    huart5.Instance = UART5;
    huart5.Init.BaudRate        = baudrate;
    huart5.Init.WordLength      = UART_WORDLENGTH_8B;
    huart5.Init.StopBits        = UART_STOPBITS_1;
    huart5.Init.Parity          = UART_PARITY_NONE;
    huart5.Init.Mode            = UART_MODE_TX_RX;
    huart5.Init.HwFlowCtl       = UART_HWCONTROL_NONE;
    huart5.Init.OverSampling    = UART_OVERSAMPLING_16;
    huart5.Init.OneBitSampling  = UART_ONE_BIT_SAMPLE_DISABLE;
    huart5.Init.ClockPrescaler  = UART_PRESCALER_DIV1;
    huart5.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart5) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_SetTxFifoThreshold(&huart5, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_SetRxFifoThreshold(&huart5, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_EnableFifoMode(&huart5) != HAL_OK) Error_Handler();
}
static void MX_USART3_UART_Init(void) {
    huart3.Instance = USART3;
    huart3.Init.BaudRate        = 115200;
    huart3.Init.WordLength      = UART_WORDLENGTH_8B;
    huart3.Init.StopBits        = UART_STOPBITS_1;
    huart3.Init.Parity          = UART_PARITY_NONE;
    huart3.Init.Mode            = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl       = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling    = UART_OVERSAMPLING_16;
    huart3.Init.OneBitSampling  = UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.Init.ClockPrescaler  = UART_PRESCALER_DIV1;
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart3) != HAL_OK) Error_Handler();
}
static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = GPIO_PIN_11;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
static void MX_RNG_Init(void) {
    hrng.Instance = RNG;
    hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
    if (HAL_RNG_Init(&hrng) != HAL_OK) Error_Handler();
}
void Error_Handler(void) { __disable_irq(); while (1) {} }
