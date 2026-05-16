/**
  ******************************************************************************
  * @file           : wifi_driver.h
  * @brief          : Ai-WB2 Wi-Fi AT Command Library Header
  ******************************************************************************
  */
#ifndef __WIFI_DRIVER_H__
#define __WIFI_DRIVER_H__

#include "main.h"

#define WIFI_RX_BUF_SIZE    8192
#define WIFI_CMD_BUF_SIZE   256

/* Wi-Fi 狀態枚舉 */
typedef enum {
    WIFI_OK = 0,
    WIFI_ERROR,
    WIFI_TIMEOUT,
    WIFI_NO_RESPONSE
} WiFi_Status_t;

/* Wi-Fi 模組結構體 */
typedef struct {
    UART_HandleTypeDef *huart;              // 指向通訊用的 UART
    uint8_t rx_buf[WIFI_RX_BUF_SIZE];       // 接收快取區
    uint16_t rx_index;                      // 快取區當前索引
    uint8_t echo_disabled;                  // 是否已關閉回顯
} WiFi_Device_t;

/* --- 核心驅動 API --- */
void          WiFi_Init(WiFi_Device_t *dev, UART_HandleTypeDef *huart);
void          WiFi_Hardware_Reset(void);
void          WiFi_Clear_RX(WiFi_Device_t *dev);
void          WiFi_Send_Cmd(WiFi_Device_t *dev, const char *cmd);
WiFi_Status_t WiFi_Wait_Response(WiFi_Device_t *dev, uint32_t timeout_ms);

/* --- Wi-Fi 聯網 API --- */
WiFi_Status_t WiFi_Basic_Check(WiFi_Device_t *dev);
WiFi_Status_t WiFi_Connect(WiFi_Device_t *dev, const char *ssid, const char *pwd);

/* --- TCP/UDP Socket API --- */
WiFi_Status_t WiFi_Socket_Create_TCPClient(WiFi_Device_t *dev, const char *ip, uint16_t port);
WiFi_Status_t WiFi_Socket_Close(WiFi_Device_t *dev, int con_id);
WiFi_Status_t WiFi_Socket_Send_Line(WiFi_Device_t *dev, int con_id, const char *data);
WiFi_Status_t WiFi_Socket_Read(WiFi_Device_t *dev, int con_id);
int          WiFi_Get_TCPClient_ConID(WiFi_Device_t *dev);

#endif /* __WIFI_DRIVER_H__ */