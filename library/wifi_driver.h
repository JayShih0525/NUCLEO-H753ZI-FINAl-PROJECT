/**
  ******************************************************************************
  * @file    wifi_driver.h
  * @brief   Ai-WB2 Wi-Fi AT Command Driver Header
  ******************************************************************************
  */
#ifndef __WIFI_DRIVER_H__
#define __WIFI_DRIVER_H__

#include "main.h"

/* ─── 波特率設定 ─────────────────────────────────────────────── */
// 115200
// 460800
// 921600
// 1000000
#define WIFI_INIT_BAUD		921600
#define WIFI_HIGH_BAUD		921600    /* 切換後的高速波特率        */

/* ─── Buffer 大小 ────────────────────────────────────────────── */
#define WIFI_RX_BUF_SIZE		8192
#define WIFI_CMD_BUF_SIZE		256

/* ─── 狀態碼 ─────────────────────────────────────────────────── */
typedef enum {
    WIFI_OK = 0,
    WIFI_ERROR,
    WIFI_TIMEOUT,
    WIFI_NO_RESPONSE
} WiFi_Status_t;

/* ─── 設備結構體 ─────────────────────────────────────────────── */
typedef struct {
    UART_HandleTypeDef *huart;
    uint8_t  rx_buf[WIFI_RX_BUF_SIZE];
    uint16_t rx_index;
    uint8_t  echo_disabled;
} WiFi_Device_t;

/* ─── 核心 API ───────────────────────────────────────────────── */
void          WiFi_Init(WiFi_Device_t *dev, UART_HandleTypeDef *huart);
void          WiFi_Hardware_Reset(void);
void          WiFi_Clear_RX(WiFi_Device_t *dev);
void          WiFi_Send_Cmd(WiFi_Device_t *dev, const char *cmd);
WiFi_Status_t WiFi_Wait_Response(WiFi_Device_t *dev, uint32_t timeout_ms);
void          WiFi_UART_Reinit(UART_HandleTypeDef *huart, uint32_t baudrate);

/* ─── 聯網 API ───────────────────────────────────────────────── */
WiFi_Status_t WiFi_Basic_Check(WiFi_Device_t *dev);
WiFi_Status_t WiFi_Connect(WiFi_Device_t *dev,
                            const char *ssid, const char *pwd);

/* ─── Socket API ─────────────────────────────────────────────── */
WiFi_Status_t WiFi_Socket_SetReceiveMode(WiFi_Device_t *dev, uint8_t active_mode);
WiFi_Status_t WiFi_Socket_Create_TCPClient(WiFi_Device_t *dev,
                                            const char *ip, uint16_t port);
WiFi_Status_t WiFi_Socket_Close(WiFi_Device_t *dev, int con_id);
WiFi_Status_t WiFi_Socket_Send_Line(WiFi_Device_t *dev,
                                     int con_id, const char *data);
WiFi_Status_t WiFi_Socket_Read(WiFi_Device_t *dev, int con_id);
int           WiFi_Get_TCPClient_ConID(WiFi_Device_t *dev);

#endif /* __WIFI_DRIVER_H__ */
