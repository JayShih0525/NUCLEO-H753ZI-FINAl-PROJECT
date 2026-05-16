/**
  ******************************************************************************
  * @file           : wifi_driver.c
  * @brief          : Ai-WB2 Wi-Fi AT Command Library Implementation
  ******************************************************************************
  */
#include "wifi_driver.h"
#include <string.h>
#include <stdio.h>

/* 用於將 Debug 訊息向上拋給 main 輸出的外部宣告 (huart3) */
extern UART_HandleTypeDef huart3;
static void Log_To_Console(const char *msg) {
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

/**
  * @brief 初始化 Wi-Fi 設備物件
  */
void WiFi_Init(WiFi_Device_t *dev, UART_HandleTypeDef *huart) {
    dev->huart = huart;
    dev->rx_index = 0;
    dev->echo_disabled = 0;
    memset(dev->rx_buf, 0, WIFI_RX_BUF_SIZE);
}

/**
  * @brief 硬體重置時序 (控制 PB11)
  */
void WiFi_Hardware_Reset(void) {
    Log_To_Console("\r\n[WIFI LIB] Executing Hardware Reset via PB11...\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);
    Log_To_Console("[WIFI LIB] Waiting for module boot up (8s)...\r\n");
    HAL_Delay(8000);
}

/**
  * @brief 高效底層暫存器清空，防止 Overrun 鎖死
  */
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

/**
  * @brief 發送 AT 指令
  */
void WiFi_Send_Cmd(WiFi_Device_t *dev, const char *cmd) {
    WiFi_Clear_RX(dev);
    Log_To_Console("\r\n-> Send: ");
    Log_To_Console(cmd);
    HAL_UART_Transmit(dev->huart, (uint8_t *)cmd, strlen(cmd), HAL_MAX_DELAY);
}

/**
  * @brief 底層核心：高效暫存器輪詢接收 + 動態超時重新計時
  */
WiFi_Status_t WiFi_Wait_Response(WiFi_Device_t *dev, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (__HAL_UART_GET_FLAG(dev->huart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_FLAG(dev->huart, UART_CLEAR_OREF);
        }

        if (__HAL_UART_GET_FLAG(dev->huart, UART_FLAG_RXNE)) {
            uint8_t ch = (uint8_t)(dev->huart->Instance->RDR & 0xFF);
            if (dev->rx_index < WIFI_RX_BUF_SIZE - 1) {
                dev->rx_buf[dev->rx_index++] = ch;
            }
            start = HAL_GetTick(); // 收到 Byte，刷新超時基準
        }
    }
    dev->rx_buf[dev->rx_index] = '\0';

    Log_To_Console("<- Recv:\r\n");
    if (dev->rx_index == 0) {
        Log_To_Console("[No response]\r\n");
        return WIFI_NO_RESPONSE;
    }
    Log_To_Console((char *)dev->rx_buf);
    Log_To_Console("\r\n");

    if (strstr((char *)dev->rx_buf, "OK")) return WIFI_OK;
    if (strstr((char *)dev->rx_buf, "ERROR")) return WIFI_ERROR;

    return WIFI_OK; // 部分 query 指令可能只有資料，沒帶標準結束符，由應用層自行解析
}

/**
  * @brief 通訊基礎檢查
  */
WiFi_Status_t WiFi_Basic_Check(WiFi_Device_t *dev) {
    Log_To_Console("\r\n===== WiFi Basic Check =====\r\n");
    WiFi_Send_Cmd(dev, "AT\r\n");
    if (WiFi_Wait_Response(dev, 3000) == WIFI_NO_RESPONSE) return WIFI_NO_RESPONSE;

    HAL_Delay(500);
    WiFi_Send_Cmd(dev, "ATE0\r\n"); // 關閉回顯
    WiFi_Wait_Response(dev, 3000);
    
    HAL_Delay(500);
    WiFi_Send_Cmd(dev, "AT+UARTCFG?\r\n");
    WiFi_Wait_Response(dev, 3000);
    
    return WIFI_OK;
}

/**
  * @brief 連接 Wi-Fi 基地台
  */
WiFi_Status_t WiFi_Connect(WiFi_Device_t *dev, const char *ssid, const char *pwd) {
    char cmd[WIFI_CMD_BUF_SIZE];
    Log_To_Console("\r\n===== Connecting to WiFi AP =====\r\n");
    
    WiFi_Send_Cmd(dev, "AT+WMODE=1,1\r\n");
    WiFi_Wait_Response(dev, 4000);
    HAL_Delay(500);

    snprintf(cmd, sizeof(cmd), "AT+WSCAN=%s\r\n", ssid);
    WiFi_Send_Cmd(dev, cmd);
    WiFi_Wait_Response(dev, 15000);
    HAL_Delay(500);

    snprintf(cmd, sizeof(cmd), "AT+WJAP=%s,%s\r\n", ssid, pwd);
    WiFi_Send_Cmd(dev, cmd);
    WiFi_Wait_Response(dev, 30000);
    HAL_Delay(1000);

    WiFi_Send_Cmd(dev, "AT+WJAP?\r\n");
    if (WiFi_Wait_Response(dev, 5000) != WIFI_OK) return WIFI_ERROR;

    return WIFI_OK;
}

/**
  * @brief 建立 TCP Client Socket 連線
  */
WiFi_Status_t WiFi_Socket_Create_TCPClient(WiFi_Device_t *dev, const char *ip, uint16_t port) {
    char cmd[WIFI_CMD_BUF_SIZE];
    Log_To_Console("\r\n===== Creating TCP Connection =====\r\n");
    
    snprintf(cmd, sizeof(cmd), "AT+SOCKET=4,%s,%d\r\n", ip, port);
    WiFi_Send_Cmd(dev, cmd);
    return WiFi_Wait_Response(dev, 30000);
}

/**
  * @brief 關閉指定識別碼的 Socket
  */
WiFi_Status_t WiFi_Socket_Close(WiFi_Device_t *dev, int con_id) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+SOCKETDEL=%d\r\n", con_id);
    WiFi_Send_Cmd(dev, cmd);
    return WiFi_Wait_Response(dev, 5000);
}

/**
  * @brief 單行文字發送
  */
WiFi_Status_t WiFi_Socket_Send_Line(WiFi_Device_t *dev, int con_id, const char *data) {
    char cmd[WIFI_CMD_BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "AT+SOCKETSENDLINE=%d,%d,%s\r\n", con_id, (int)strlen(data), data);
    WiFi_Send_Cmd(dev, cmd);
    return WiFi_Wait_Response(dev, 10000);
}

/**
  * @brief 讀取 Socket 接收資料
  */
WiFi_Status_t WiFi_Socket_Read(WiFi_Device_t *dev, int con_id) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+SOCKETREAD=%d\r\n", con_id);
    WiFi_Send_Cmd(dev, cmd);
    return WiFi_Wait_Response(dev, 10000);
}

/**
  * @brief 解析目前連線成功的 TCP Client Connection ID
  * @return 成功傳回 ConID (0-4), 失敗傳回 -1
  */
int WiFi_Get_TCPClient_ConID(WiFi_Device_t *dev) {
    char *p = (char *)dev->rx_buf;
    while (*p) {
        if ((p[0] >= '0' && p[0] <= '9') && p[1] == ',' && p[2] == '4' && p[3] == ',') {
            return p[0] - '0';
        }
        p++;
    }
    return -1;
}