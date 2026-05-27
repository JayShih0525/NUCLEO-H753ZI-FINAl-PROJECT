/**
  ******************************************************************************
  * @file    wifi_bulk_transfer.c
  * @brief   Streaming Bulk Transfer — 純串流，無 ACK，最低延遲
  ******************************************************************************
  */

#include "wifi_bulk_transfer.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ─── 內部狀態 ──────────────────────────────────────────────── */
static WiFi_Device_t *s_dev    = NULL;
static int            s_con_id = -1;
static uint8_t        s_in_tt  = 0;

/* ─── Debug Log ─────────────────────────────────────────────── */
extern UART_HandleTypeDef huart3;
static void SLog(const char *msg) {
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}
static void SLogF(const char *fmt, ...) {
    char buf[128];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    SLog(buf);
}

/* ════════════════════════════════════════════════════════════════
 *  CRC16-CCITT (xmodem)
 * ════════════════════════════════════════════════════════════════ */
uint16_t Stream_CRC16(const uint8_t *data, uint32_t len) {
    uint16_t crc = 0x0000;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

/* ════════════════════════════════════════════════════════════════
 *  透傳模式底層 I/O
 * ════════════════════════════════════════════════════════════════ */

static void TT_Write(const uint8_t *data, uint32_t len) {
    /* 分批送，避免單次 HAL_UART_Transmit 太大 */
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t chunk = (len - sent > 4096) ? 4096 : (len - sent);
        HAL_UART_Transmit(s_dev->huart, (uint8_t *)(data + sent),
                          (uint16_t)chunk, HAL_MAX_DELAY);
        sent += chunk;
    }
}

static uint8_t TT_Read(uint8_t *buf, uint32_t need, uint32_t timeout_ms) {
    uint32_t got = 0;
    uint32_t t   = HAL_GetTick();
    while (got < need) {
        if ((HAL_GetTick() - t) >= timeout_ms) return 0;
        if (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_ORE))
            __HAL_UART_CLEAR_FLAG(s_dev->huart, UART_CLEAR_OREF);
        if (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_RXNE)) {
            buf[got++] = (uint8_t)(s_dev->huart->Instance->RDR & 0xFF);
            t = HAL_GetTick();
        }
    }
    return 1;
}

/* ════════════════════════════════════════════════════════════════
 *  Frame 組裝
 *
 *  Frame layout:
 *  [0]  MAGIC_0  0xAB
 *  [1]  MAGIC_1  0xCD
 *  [2]  LEN_HI
 *  [3]  LEN_LO
 *  [4..4+N-1]  PAYLOAD
 *  [4+N]   CRC_HI
 *  [4+N+1] CRC_LO
 * ════════════════════════════════════════════════════════════════ */
#define FRAME_BUF_SIZE  (STREAM_HDR_SIZE + STREAM_PAYLOAD_MAX + STREAM_FTR_SIZE)
static uint8_t s_frame_buf[FRAME_BUF_SIZE];

static void TT_Send_Frame(const uint8_t *payload, uint16_t plen) {
    s_frame_buf[0] = STREAM_MAGIC_0;
    s_frame_buf[1] = STREAM_MAGIC_1;
    s_frame_buf[2] = (uint8_t)(plen >> 8);
    s_frame_buf[3] = (uint8_t)(plen & 0xFF);
    if (plen > 0 && payload != NULL)
        memcpy(s_frame_buf + STREAM_HDR_SIZE, payload, plen);
    uint16_t crc = Stream_CRC16(s_frame_buf, STREAM_HDR_SIZE + plen);
    s_frame_buf[STREAM_HDR_SIZE + plen]     = (uint8_t)(crc >> 8);
    s_frame_buf[STREAM_HDR_SIZE + plen + 1] = (uint8_t)(crc & 0xFF);
    TT_Write(s_frame_buf, STREAM_HDR_SIZE + plen + STREAM_FTR_SIZE);
}

/* ════════════════════════════════════════════════════════════════
 *  公開 API
 * ════════════════════════════════════════════════════════════════ */

void Stream_Init(WiFi_Device_t *dev, int con_id) {
    s_dev    = dev;
    s_con_id = con_id;
    s_in_tt  = 0;
    SLog("[STREAM] Init OK\r\n");
}

StreamStatus_t Stream_Enter(void) {
    SLog("[STREAM] Entering transparent mode...\r\n");
    WiFi_Send_Cmd(s_dev, "AT+SOCKETTT\r\n");
    uint32_t t = HAL_GetTick();
    while ((HAL_GetTick() - t) < 5000) {
        if (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_ORE))
            __HAL_UART_CLEAR_FLAG(s_dev->huart, UART_CLEAR_OREF);
        if (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_RXNE)) {
            uint8_t ch = (uint8_t)(s_dev->huart->Instance->RDR & 0xFF);
            if (ch == '>') {
                s_in_tt = 1;
                HAL_Delay(50);
                SLog("[STREAM] Transparent mode ACTIVE\r\n");
                return STREAM_OK;
            }
        }
    }
    SLog("[STREAM] ERR: Cannot enter transparent mode\r\n");
    return STREAM_ERR_TT;
}

void Stream_Exit(void) {
    if (!s_in_tt) return;
    SLog("[STREAM] Exiting...\r\n");
    HAL_Delay(1100);
    HAL_UART_Transmit(s_dev->huart, (uint8_t *)"+++", 3, HAL_MAX_DELAY);
    HAL_Delay(1100);
    s_in_tt = 0;
    WiFi_Wait_Response(s_dev, 3000);
    SLog("[STREAM] EXIT\r\n");
}

/* ════════════════════════════════════════════════════════════════
 *  Stream_Send  —  純串流，不等 ACK
 *
 *  把 data 切成 STREAM_PAYLOAD_MAX 大小的 frame 連續送出，
 *  完全不等對方回應，速度接近 UART 波特率上限。
 * ════════════════════════════════════════════════════════════════ */
StreamStatus_t Stream_Send(const uint8_t *data, uint32_t len) {
    if (!s_dev || !s_in_tt || !data || len == 0) return STREAM_ERR_PARAM;

    uint32_t offset = 0;
    uint32_t frame_count = 0;

    while (offset < len) {
        uint16_t this_len = (len - offset > STREAM_PAYLOAD_MAX) ?
                             STREAM_PAYLOAD_MAX : (uint16_t)(len - offset);
        TT_Send_Frame(data + offset, this_len);
        offset += this_len;
        frame_count++;
    }

    SLogF("[STREAM] Sent %lu bytes in %lu frames\r\n", len, frame_count);
    return STREAM_OK;
}

/* ════════════════════════════════════════════════════════════════
 *  Stream_Recv  —  接收一個 frame
 *
 *  掃描 MAGIC → 讀 LEN → 讀 PAYLOAD → 驗 CRC
 * ════════════════════════════════════════════════════════════════ */
StreamStatus_t Stream_Recv(uint8_t *out_buf, uint32_t buf_size,
                            uint32_t *out_len) {
    if (!s_dev || !s_in_tt || !out_buf || !out_len) return STREAM_ERR_PARAM;
    *out_len = 0;

    /* 掃描 MAGIC */
    uint32_t t    = HAL_GetTick();
    uint8_t  sync = 0;
    uint8_t  hdr[STREAM_HDR_SIZE];

    while ((HAL_GetTick() - t) < 5000) {
        uint8_t b;
        if (!TT_Read(&b, 1, 200)) continue;
        if (sync == 0 && b == STREAM_MAGIC_0) { sync = 1; hdr[0] = b; continue; }
        if (sync == 1 && b == STREAM_MAGIC_1) { sync = 2; hdr[1] = b; break; }
        sync = 0;
    }
    if (sync < 2) return STREAM_ERR_TIMEOUT;

    /* 讀 LEN（2 bytes） */
    if (!TT_Read(hdr + 2, 2, 1000)) return STREAM_ERR_TIMEOUT;
    uint16_t plen = ((uint16_t)hdr[2] << 8) | hdr[3];

    if (plen > buf_size || plen > STREAM_PAYLOAD_MAX) return STREAM_ERR_PARAM;

    /* 讀 PAYLOAD + CRC */
    static uint8_t tmp[STREAM_PAYLOAD_MAX + STREAM_FTR_SIZE];
    if (!TT_Read(tmp, plen + STREAM_FTR_SIZE, 5000)) return STREAM_ERR_TIMEOUT;

    /* 驗 CRC */
    uint8_t full[STREAM_HDR_SIZE + STREAM_PAYLOAD_MAX];
    memcpy(full, hdr, STREAM_HDR_SIZE);
    if (plen > 0) memcpy(full + STREAM_HDR_SIZE, tmp, plen);
    uint16_t exp_crc = Stream_CRC16(full, STREAM_HDR_SIZE + plen);
    uint16_t got_crc = ((uint16_t)tmp[plen] << 8) | tmp[plen + 1];

    if (exp_crc != got_crc) {
        SLogF("[STREAM] CRC fail: exp=%04X got=%04X\r\n", exp_crc, got_crc);
        return STREAM_ERR_CRC;
    }

    memcpy(out_buf, tmp, plen);
    *out_len = plen;
    return STREAM_OK;
}
