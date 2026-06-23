/**
  ******************************************************************************
  * @file    wifi_bulk_transfer.h
  * @brief   Streaming Bulk Transfer over Ai-WB2 Transparent Mode
  *
  * ═══════════════════════════════════════════════════════════════
  *  純串流模式（無 ACK），最低延遲設計
  * ═══════════════════════════════════════════════════════════════
  *
  *  Frame 格式（極簡，只保護資料邊界）：
  *
  *  ┌──────────────────────────────────────────┐
  *  │  MAGIC    2 bytes  0xAB 0xCD             │
  *  │  LEN      2 bytes  payload 長度 big-endian│
  *  │  PAYLOAD  N bytes  資料本體              │
  *  │  CRC16    2 bytes  MAGIC+LEN+PAYLOAD     │
  *  └──────────────────────────────────────────┘
  *
  *  總 overhead = 6 bytes/frame
  *
  *  傳輸流程：
  *    STM32 連續送 frame → Server 連續收
  *    完全不等 ACK，速度接近 UART 波特率上限
  *
  *  為什麼不需要 ACK？
  *    透傳模式下 UART = TCP，TCP 本身保證送達，
  *    CRC 只用來驗邊界同步，不是重送機制
  * ═══════════════════════════════════════════════════════════════
  */

#ifndef WIFI_BULK_TRANSFER_H
#define WIFI_BULK_TRANSFER_H

#include "main.h"
#include "wifi_driver.h"
#include <stdint.h>

/* ─── 協議常數 ──────────────────────────────────────────────── */
#define STREAM_MAGIC_0      0xAB
#define STREAM_MAGIC_1      0xCD
#define STREAM_HDR_SIZE     4       /* MAGIC(2) + LEN(2) */
#define STREAM_FTR_SIZE     2       /* CRC16(2) */
#define STREAM_OVERHEAD     6       /* 總 overhead */

/* 每個 frame 最大 payload
 * 921600 baud 下建議 4096，可以大幅減少 frame 數量
 * 最大不超過 8192（STM32 stack 限制）                          */
#define STREAM_PAYLOAD_MAX  4096

/* ─── 回傳碼 ────────────────────────────────────────────────── */
typedef enum {
    STREAM_OK = 0,
    STREAM_ERR_PARAM,
    STREAM_ERR_TT,       /* 透傳進入/退出失敗 */
    STREAM_ERR_TIMEOUT,
    STREAM_ERR_CRC,
} StreamStatus_t;

/* ─── 公開 API ──────────────────────────────────────────────── */

/**
 * @brief 初始化（需已完成 WiFi_Connect 並建立 TCP Socket）
 */
void Stream_Init(WiFi_Device_t *dev, int con_id);

/**
 * @brief 進入透傳模式（傳輸前呼叫）
 */
StreamStatus_t Stream_Enter(void);

/**
 * @brief 退出透傳模式（傳輸結束後呼叫）
 */
void Stream_Exit(void);

/**
 * @brief 連續傳送資料（無 ACK，最低延遲）
 * @param data  資料指標
 * @param len   資料長度
 */
StreamStatus_t Stream_Send(const uint8_t *data, uint32_t len);

/**
 * @brief 接收一個 frame
 * @param out_buf   接收緩衝
 * @param buf_size  緩衝大小
 * @param out_len   實際接收長度
 */
StreamStatus_t Stream_Recv(uint8_t *out_buf, uint32_t buf_size,
                            uint32_t *out_len);

/* ─── 工具 ──────────────────────────────────────────────────── */
uint16_t Stream_CRC16(const uint8_t *data, uint32_t len);

#endif /* WIFI_BULK_TRANSFER_H */
