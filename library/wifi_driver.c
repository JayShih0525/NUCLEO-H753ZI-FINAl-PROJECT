/**
  ******************************************************************************
  * @file    wifi_driver.c
  * @brief   Ai-WB2 Wi-Fi AT Command Driver
  ******************************************************************************
  */
#include "wifi_driver.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart3;
static void Log_To_Console(const char *msg) {
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

/* ════════════════════════════════════════════════════════════════
 *  基礎驅動
 * ════════════════════════════════════════════════════════════════ */

void WiFi_Init(WiFi_Device_t *dev, UART_HandleTypeDef *huart) {
    dev->huart = huart;
    dev->rx_index = 0;
    dev->echo_disabled = 0;
    memset(dev->rx_buf, 0, WIFI_RX_BUF_SIZE);
}

void WiFi_Hardware_Reset(void) {
    Log_To_Console("\r\n[WIFI] Hardware Reset...\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);
    Log_To_Console("[WIFI] Waiting boot (8s)...\r\n");
    HAL_Delay(8000);
}

void WiFi_Clear_RX(WiFi_Device_t *dev) {
    volatile uint32_t dummy;
    __HAL_UART_CLEAR_FLAG(dev->huart, UART_CLEAR_OREF);
    while (__HAL_UART_GET_FLAG(dev->huart, UART_FLAG_RXNE)) {
        dummy = dev->huart->Instance->RDR;
        (void)dummy;
    }
    dev->rx_index = 0;
    memset(dev->rx_buf, 0, WIFI_RX_BUF_SIZE);
}

void WiFi_Send_Cmd(WiFi_Device_t *dev, const char *cmd) {
    WiFi_Clear_RX(dev);
    Log_To_Console("\r\n-> Send: ");
    Log_To_Console(cmd);
    HAL_UART_Transmit(dev->huart, (uint8_t *)cmd, strlen(cmd), HAL_MAX_DELAY);
}

WiFi_Status_t WiFi_Wait_Response(WiFi_Device_t *dev, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    uint32_t last_rx = start;
    uint8_t received_any = 0u;

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (__HAL_UART_GET_FLAG(dev->huart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_FLAG(dev->huart, UART_CLEAR_OREF);
        }

        if (__HAL_UART_GET_FLAG(dev->huart, UART_FLAG_RXNE)) {
            uint8_t ch = (uint8_t)(dev->huart->Instance->RDR & 0xFFu);

            if (dev->rx_index < (WIFI_RX_BUF_SIZE - 1u)) {
                dev->rx_buf[dev->rx_index++] = ch;
                dev->rx_buf[dev->rx_index] = '\0';
            }

            received_any = 1u;
            last_rx = HAL_GetTick();

            /*
             * AT commands used in this project end with OK/ERROR.
             * Return immediately once the terminal response arrives instead
             * of waiting the complete timeout period.
             */
            if (strstr((char *)dev->rx_buf, "\r\nOK\r\n") != NULL ||
                strstr((char *)dev->rx_buf, "\nOK\r\n") != NULL) {
                Log_To_Console("<- Recv:\r\n");
                Log_To_Console((char *)dev->rx_buf);
                Log_To_Console("\r\n");
                return WIFI_OK;
            }

            if (strstr((char *)dev->rx_buf, "ERROR") != NULL ||
                strstr((char *)dev->rx_buf, "FAIL") != NULL) {
                Log_To_Console("<- Recv:\r\n");
                Log_To_Console((char *)dev->rx_buf);
                Log_To_Console("\r\n");
                return WIFI_ERROR;
            }
        }

        /*
         * Some firmware replies may not contain final OK for asynchronous
         * events. If bytes were received and the line is idle for 100 ms,
         * expose the response to the caller without waiting several seconds.
         */
        if (received_any && ((HAL_GetTick() - last_rx) > 100u)) {
            break;
        }
    }

    dev->rx_buf[dev->rx_index] = '\0';
    Log_To_Console("<- Recv:\r\n");

    if (dev->rx_index == 0u) {
        Log_To_Console("[No response]\r\n");
        return WIFI_NO_RESPONSE;
    }

    Log_To_Console((char *)dev->rx_buf);
    Log_To_Console("\r\n");

    if (strstr((char *)dev->rx_buf, "OK") != NULL) {
        return WIFI_OK;
    }
    if (strstr((char *)dev->rx_buf, "ERROR") != NULL ||
        strstr((char *)dev->rx_buf, "FAIL") != NULL) {
        return WIFI_ERROR;
    }

    return WIFI_OK;
}

/* ════════════════════════════════════════════════════════════════
 *  WiFi_Basic_Check
 *
 *  流程：
 *    1. AT              — 確認模組存活（用目前波特率）
 *    2. ATE0            — 關閉回顯
 *    3. AT+UARTCFG=...  — 用「目前波特率」下指令，切換到高速
 *    4. STM32 這邊也切換 UART 波特率
 *    5. AT              — 用新波特率確認通訊正常
 * ════════════════════════════════════════════════════════════════ */
WiFi_Status_t WiFi_Basic_Check(WiFi_Device_t *dev) {
    Log_To_Console("\r\n===== WiFi Basic Check =====\r\n");

    WiFi_Send_Cmd(dev, "AT\r\n");
    if (WiFi_Wait_Response(dev, 3000) == WIFI_NO_RESPONSE) return WIFI_NO_RESPONSE;
    HAL_Delay(200);

    WiFi_Send_Cmd(dev, "ATE0\r\n");
    WiFi_Wait_Response(dev, 3000);
    HAL_Delay(200);

    /* 切換到高速波特率
     * 格式：AT+UARTCFG=<baud>,<data_bits>,<stop_bits>,<parity>
     * 注意：只有 4 個參數，不能加第 5 個
     */
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+UARTCFG=%lu,8,1,0\r\n",
             (unsigned long)WIFI_HIGH_BAUD);
    WiFi_Send_Cmd(dev, cmd);
    WiFi_Wait_Response(dev, 2000);
    HAL_Delay(100);

    /* STM32 跟著切換 */
    Log_To_Console("[WIFI] Switching to high speed...\r\n");
    WiFi_UART_Reinit(dev->huart, WIFI_HIGH_BAUD);
    HAL_Delay(100);

    /* 確認新波特率正常 */
    WiFi_Send_Cmd(dev, "AT\r\n");
    if (WiFi_Wait_Response(dev, 3000) == WIFI_NO_RESPONSE) {
        Log_To_Console("[WIFI] ERR: fallback to 115200\r\n");
        WiFi_UART_Reinit(dev->huart, 115200);
        return WIFI_ERROR;
    }

    Log_To_Console("[WIFI] High speed OK\r\n");
    return WIFI_OK;
}
/* ════════════════════════════════════════════════════════════════
 *  WiFi_UART_Reinit
 *  動態切換 STM32 UART 波特率（不重置模組）
 * ════════════════════════════════════════════════════════════════ */
void WiFi_UART_Reinit(UART_HandleTypeDef *huart, uint32_t baudrate) {
    HAL_UART_DeInit(huart);
    huart->Init.BaudRate        = baudrate;
    huart->Init.WordLength      = UART_WORDLENGTH_8B;
    huart->Init.StopBits        = UART_STOPBITS_1;
    huart->Init.Parity          = UART_PARITY_NONE;
    huart->Init.Mode            = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl       = UART_HWCONTROL_NONE;
    huart->Init.OverSampling    = UART_OVERSAMPLING_16;
    huart->Init.OneBitSampling  = UART_ONE_BIT_SAMPLE_DISABLE;
    huart->Init.ClockPrescaler  = UART_PRESCALER_DIV1;
    huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(huart) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_EnableFifoMode(huart) != HAL_OK) Error_Handler();
}

/* ════════════════════════════════════════════════════════════════
 *  Wi-Fi 連線
 * ════════════════════════════════════════════════════════════════ */
WiFi_Status_t WiFi_Connect(WiFi_Device_t *dev, const char *ssid, const char *pwd) {
    char cmd[WIFI_CMD_BUF_SIZE];
    Log_To_Console("\r\n===== Connecting to WiFi AP =====\r\n");

    WiFi_Send_Cmd(dev, "AT+WMODE=0,0\r\n");
    WiFi_Wait_Response(dev, 3000);
    HAL_Delay(1000);

    WiFi_Send_Cmd(dev, "AT+WMODE=1,1\r\n");
    WiFi_Wait_Response(dev, 4000);
    HAL_Delay(2000);

    snprintf(cmd, sizeof(cmd), "AT+WSCAN=%s\r\n", ssid);
    WiFi_Send_Cmd(dev, cmd);
    WiFi_Wait_Response(dev, 15000);
    HAL_Delay(500);

    snprintf(cmd, sizeof(cmd), "AT+WJAP=%s,%s\r\n", ssid, pwd);
    WiFi_Send_Cmd(dev, cmd);
    WiFi_Wait_Response(dev, 30000);
    HAL_Delay(1000);

    WiFi_Send_Cmd(dev, "AT+WJAP?\r\n");
    WiFi_Wait_Response(dev, 5000);

    if (strstr((char *)dev->rx_buf, ":3,") == NULL) {
        HAL_Delay(3000);
        WiFi_Send_Cmd(dev, "AT+WJAP?\r\n");
        if (WiFi_Wait_Response(dev, 5000) != WIFI_OK) return WIFI_ERROR;
        if (strstr((char *)dev->rx_buf, ":3,") == NULL) return WIFI_ERROR;
    }
    return WIFI_OK;
}

/* ════════════════════════════════════════════════════════════════
 *  Socket API
 * ════════════════════════════════════════════════════════════════ */
/*
 * Ai-WB2 socket receive mode:
 *   0 = passive mode (default): only a SocketDown event is printed.
 *   1 = active mode: received socket data is forwarded on UART.
 *
 * This must be enabled before entering AT+SOCKETTT in this project because
 * WSRP relies on server -> MCU ACK/NACK frames.
 */
WiFi_Status_t WiFi_Socket_SetReceiveMode(WiFi_Device_t *dev, uint8_t active_mode) {
    char cmd[48];

    snprintf(cmd, sizeof(cmd), "AT+SOCKETRECVCFG=%u\r\n",
             active_mode ? 1u : 0u);

    Log_To_Console("\r\n===== Setting Socket Receive Mode =====\r\n");
    WiFi_Send_Cmd(dev, cmd);

    return WiFi_Wait_Response(dev, 3000u);
}

WiFi_Status_t WiFi_Socket_Create_TCPClient(WiFi_Device_t *dev,
                                            const char *ip, uint16_t port) {
    char cmd[WIFI_CMD_BUF_SIZE];
    Log_To_Console("\r\n===== Creating TCP Connection =====\r\n");
    snprintf(cmd, sizeof(cmd), "AT+SOCKET=4,%s,%d\r\n", ip, port);
    WiFi_Send_Cmd(dev, cmd);
    return WiFi_Wait_Response(dev, 30000);
}

WiFi_Status_t WiFi_Socket_Close(WiFi_Device_t *dev, int con_id) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+SOCKETDEL=%d\r\n", con_id);
    WiFi_Send_Cmd(dev, cmd);
    return WiFi_Wait_Response(dev, 5000);
}

WiFi_Status_t WiFi_Socket_Send_Line(WiFi_Device_t *dev,
                                     int con_id, const char *data) {
    char cmd[WIFI_CMD_BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "AT+SOCKETSENDLINE=%d,%d,%s\r\n",
             con_id, (int)strlen(data), data);
    WiFi_Send_Cmd(dev, cmd);
    return WiFi_Wait_Response(dev, 10000);
}

WiFi_Status_t WiFi_Socket_Read(WiFi_Device_t *dev, int con_id) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+SOCKETREAD=%d\r\n", con_id);
    WiFi_Send_Cmd(dev, cmd);
    return WiFi_Wait_Response(dev, 10000);
}

int WiFi_Get_TCPClient_ConID(WiFi_Device_t *dev) {
    char *p = (char *)dev->rx_buf;
    while (*p) {
        if ((p[0] >= '0' && p[0] <= '9') &&
             p[1] == ',' && p[2] == '4' && p[3] == ',') {
            return p[0] - '0';
        }
        p++;
    }
    return -1;
}
