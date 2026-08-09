#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

/*
 * Transport 層：任何實體匯流排（UART / SPI / I2C / USB CDC / ...）都要實作這個介面。
 *
 * 設計原則：
 *   - 上層 (packet_protocol / command_dispatcher / *_app) 完全不知道底下是什麼匯流排。
 *   - read/write 對呼叫者而言是「blocking-style」API：呼叫後在 timeout 內回傳結果。
 *     實際上底層可以是：
 *       - UART: 直接呼叫 HAL_UART_Receive/Transmit（本來就是 blocking）
 *       - SPI/I2C + bare-metal: 在 IRQ callback 裡設旗標，這裡忙等旗標直到 timeout
 *       - SPI/I2C + RTOS: 在 IRQ callback 裡 give semaphore，這裡 xSemaphoreTake(timeout)
 *     只要維持這個 blocking 介面的語意，換 RTOS 或換匯流排都不需要動 packet_protocol
 *     或任何 *_app.c。
 *
 *   - flush()：清掉硬體/軟體殘留資料（對應原本的 UART3_ClearAll）。
 *
 *   - ctx：指向底層 driver-specific handle（例如 UART_HandleTypeDef*、SPI_HandleTypeDef*），
 *     由各 transport_xxx.c 自己管理，上層絕對不要直接碰它。
 */

typedef enum {
    TRANSPORT_OK = 0,
    TRANSPORT_ERR_TIMEOUT,
    TRANSPORT_ERR_IO,
    TRANSPORT_ERR_PARAM,
    TRANSPORT_ERR_NOT_READY   /* 例如 SPI slave 還沒準備好資料 */
} Transport_Status_t;

typedef struct Transport_s {
    void *ctx;

    Transport_Status_t (*read)(struct Transport_s *self,
                                uint8_t *buf,
                                uint32_t len,
                                uint32_t timeout_ms);

    Transport_Status_t (*write)(struct Transport_s *self,
                                 const uint8_t *buf,
                                 uint32_t len,
                                 uint32_t timeout_ms);

    void (*flush)(struct Transport_s *self);

    /* 可選：某些匯流排（SPI slave）需要主動告知 host「我有資料要送」。
     * UART/USB CDC 可以留 NULL。 */
    void (*notify_ready)(struct Transport_s *self);

} Transport_t;

#endif /* TRANSPORT_H */
