/**
  ******************************************************************************
  * @file    wsrp_protocol.c
  * @brief   WSRP reliable windowed transport implementation
  *
  * Required matching definitions in wsrp_protocol.h:
  *   WSRP_UART_BURST_BYTES
  *   WSRP_UART_BURST_DELAY_MS
  *   WSRP_WINDOW_DEFAULT
  *   WSRP_WINDOW_MAX
  *   WSRP_ACK_TIMEOUT_MS
  *   WSRP_MAX_RETRY
  *   WSRP_INTER_FRAME_DELAY_MS
  *
  * Main fixes in this version:
  *   1. DATA frames are paced in small UART bursts.
  *   2. Incoming ACK/NACK bytes are stored in a persistent stream buffer.
  *      A timeout does not discard a half-received control frame.
  *   3. Late duplicate HELLO_ACK / START_ACK / ACK frames are ignored while
  *      waiting for the current protocol-stage response.
  ******************************************************************************
  */
#include "wsrp_protocol.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/* Internal state                                                             */
/* ========================================================================== */
static WiFi_Device_t *s_dev = NULL;
static int s_con_id = -1;
static uint8_t s_in_transparent = 0u;
static uint16_t s_window_frames = WSRP_WINDOW_DEFAULT;

#define WSRP_TX_FRAME_SIZE  \
    (WSRP_HEADER_SIZE + WSRP_PAYLOAD_MAX + WSRP_TRAILER_SIZE)

/*
 * Server-to-MCU control frames are small. 256 bytes is enough for several
 * queued ACK/NACK frames and permits recovery from delayed duplicate ACKs.
 */
#define WSRP_RX_STREAM_SIZE 256u

static uint8_t s_tx_frame[WSRP_TX_FRAME_SIZE];
static uint8_t s_ctrl_payload[WSRP_CONTROL_MAX];

static uint8_t s_rx_stream[WSRP_RX_STREAM_SIZE];
static uint16_t s_rx_stream_len = 0u;

extern UART_HandleTypeDef huart3;

/* ========================================================================== */
/* Debug                                                                      */
/* ========================================================================== */
static void WLog(const char *msg) {
    HAL_UART_Transmit(
        &huart3,
        (uint8_t *)msg,
        (uint16_t)strlen(msg),
        HAL_MAX_DELAY
    );
}

static void WLogF(const char *fmt, ...) {
    char buf[192];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    WLog(buf);
}

static void WLogParsedControlFrame(const WSRP_FrameInfo_t *info,
                                   const uint8_t *payload) {
#if WSRP_RX_DEBUG
    char line[192];
    uint16_t i;
    int used;

    if (info == NULL) {
        return;
    }

    used = snprintf(
        line,
        sizeof(line),
        "[WSRP] Parsed RX control type=%u object=%lu seq=%lu payload_len=%u payload:",
        (unsigned int)info->type,
        (unsigned long)info->object_id,
        (unsigned long)info->seq,
        (unsigned int)info->payload_len
    );

    for (i = 0u;
         i < info->payload_len && i < 16u &&
         used > 0 && used < (int)(sizeof(line) - 5u);
         i++) {
        used += snprintf(
            &line[used],
            sizeof(line) - (size_t)used,
            " %02X",
            payload[i]
        );
    }

    if (used > 0 && used < (int)(sizeof(line) - 3u)) {
        snprintf(&line[used], sizeof(line) - (size_t)used, "\r\n");
    }

    WLog(line);
#else
    (void)info;
    (void)payload;
#endif
}

/* ========================================================================== */
/* Big-endian helpers                                                         */
/* ========================================================================== */
static void put_u16_be(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xFFu);
}

static void put_u32_be(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)(value & 0xFFu);
}

static uint16_t get_u16_be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t get_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

/* ========================================================================== */
/* CRC                                                                        */
/* ========================================================================== */
uint16_t WSRP_CRC16(const uint8_t *data, uint32_t len) {
    uint16_t crc = 0x0000u;
    uint32_t i;
    int bit;

    for (i = 0u; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;

        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u)
                ? (uint16_t)((crc << 1) ^ 0x1021u)
                : (uint16_t)(crc << 1);
        }
    }

    return crc;
}

uint32_t WSRP_CRC32(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    int bit;

    for (i = 0u; i < len; i++) {
        crc ^= (uint32_t)data[i];

        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1u)
                ? ((crc >> 1) ^ 0xEDB88320u)
                : (crc >> 1);
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

/*
 * Frame CRC is calculated over PREFIX + PAYLOAD.
 * HEADER_CRC16 bytes are intentionally not included.
 */
static uint32_t CRC32_TwoParts(const uint8_t *part_a, uint32_t a_len,
                               const uint8_t *part_b, uint32_t b_len) {
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    int bit;

    for (i = 0u; i < a_len; i++) {
        crc ^= (uint32_t)part_a[i];

        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1u)
                ? ((crc >> 1) ^ 0xEDB88320u)
                : (crc >> 1);
        }
    }

    for (i = 0u; i < b_len; i++) {
        crc ^= (uint32_t)part_b[i];

        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1u)
                ? ((crc >> 1) ^ 0xEDB88320u)
                : (crc >> 1);
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

/* ========================================================================== */
/* UART helpers                                                               */
/* ========================================================================== */
static void UART_ClearErrors(void) {
    if (s_dev == NULL || s_dev->huart == NULL) {
        return;
    }

    if (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_FLAG(s_dev->huart, UART_CLEAR_OREF);
    }

    if (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_FE)) {
        __HAL_UART_CLEAR_FLAG(s_dev->huart, UART_CLEAR_FEF);
    }

    if (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_NE)) {
        __HAL_UART_CLEAR_FLAG(s_dev->huart, UART_CLEAR_NEF);
    }
}

static void UART_DrainIncoming(void) {
    if (s_dev == NULL || s_dev->huart == NULL) {
        return;
    }

    UART_ClearErrors();

    while (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_RXNE)) {
        (void)(s_dev->huart->Instance->RDR & 0xFFu);
    }
}

/*
 * Send one complete WSRP frame in short UART bursts.
 * This prevents a single 4096-byte DATA payload from being pushed into the
 * Wi-Fi module as one long unpaced burst.
 */
static WSRP_Status_t TT_Write(const uint8_t *data, uint32_t len) {
    uint32_t sent = 0u;

    if (data == NULL || len == 0u || s_dev == NULL || s_dev->huart == NULL) {
        return WSRP_ERR_PARAM;
    }

    while (sent < len) {
        uint32_t remaining = len - sent;
        uint16_t burst_len = (uint16_t)(
            (remaining > WSRP_UART_BURST_BYTES)
            ? WSRP_UART_BURST_BYTES
            : remaining
        );

        HAL_StatusTypeDef hal_status = HAL_UART_Transmit(
            s_dev->huart,
            (uint8_t *)(data + sent),
            burst_len,
            HAL_MAX_DELAY
        );

        if (hal_status != HAL_OK) {
            WLogF(
                "[WSRP] UART TX failed at %lu/%lu, HAL=%d\r\n",
                (unsigned long)sent,
                (unsigned long)len,
                (int)hal_status
            );
            return WSRP_ERR_UART;
        }

        sent += burst_len;

        if ((WSRP_UART_BURST_DELAY_MS > 0u) && (sent < len)) {
            HAL_Delay(WSRP_UART_BURST_DELAY_MS);
        }
    }

    return WSRP_STATUS_OK;
}

/* ========================================================================== */
/* Persistent control RX stream parser                                       */
/* ========================================================================== */
static void RX_Discard(uint16_t count) {
    if (count == 0u) {
        return;
    }

    if (count >= s_rx_stream_len) {
        s_rx_stream_len = 0u;
        return;
    }

    memmove(
        s_rx_stream,
        &s_rx_stream[count],
        (size_t)(s_rx_stream_len - count)
    );

    s_rx_stream_len = (uint16_t)(s_rx_stream_len - count);
}

/*
 * Append all currently available bytes from UART to the persistent buffer.
 * Never discard a partial ACK merely because the current wait timed out.
 */
static void RX_PumpUART(void) {
    uint8_t byte_value;

    if (s_dev == NULL || s_dev->huart == NULL) {
        return;
    }

    UART_ClearErrors();

    /*
     * Critical rule:
     * Do not call WLog/WLogF here. UART3 debug printing blocks the CPU while
     * UART5 is still receiving the rest of a 24-byte ACK. At 921600 baud the
     * whole ACK arrives in about 0.26 ms and the UART5 FIFO can overflow.
     */
    while (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_RXNE)) {
        byte_value = (uint8_t)(s_dev->huart->Instance->RDR & 0xFFu);

        if (s_rx_stream_len >= WSRP_RX_STREAM_SIZE) {
            RX_Discard(1u);
        }

        s_rx_stream[s_rx_stream_len++] = byte_value;
    }
}

/*
 * Attempt to parse one valid control frame from persistent RX bytes.
 *
 * Return:
 *   1u : valid frame returned in info/payload_out
 *   0u : no complete valid frame yet
 */
static uint8_t RX_TryPopFrame(WSRP_FrameInfo_t *info,
                              uint8_t *payload_out,
                              uint16_t out_capacity) {
    uint16_t pos;
    uint16_t payload_len;
    uint16_t received_hcrc;
    uint16_t expected_hcrc;
    uint32_t received_fcrc;
    uint32_t expected_fcrc;
    uint32_t frame_size;
    const uint8_t *prefix;

    if (info == NULL || payload_out == NULL) {
        return 0u;
    }

    while (1) {
        if (s_rx_stream_len < 2u) {
            return 0u;
        }

        pos = 0u;

        while ((pos + 1u) < s_rx_stream_len) {
            if (s_rx_stream[pos] == WSRP_MAGIC_0 &&
                s_rx_stream[pos + 1u] == WSRP_MAGIC_1) {
                break;
            }
            pos++;
        }

        if ((pos + 1u) >= s_rx_stream_len) {
            /*
             * Retain a trailing AB because the next received byte may be CD.
             */
            if (s_rx_stream[s_rx_stream_len - 1u] == WSRP_MAGIC_0) {
                s_rx_stream[0] = WSRP_MAGIC_0;
                s_rx_stream_len = 1u;
            } else {
                s_rx_stream_len = 0u;
            }
            return 0u;
        }

        if (pos > 0u) {
            RX_Discard(pos);
        }

        if (s_rx_stream_len < WSRP_HEADER_SIZE) {
            return 0u;
        }

        prefix = s_rx_stream;

        if (prefix[2] != WSRP_VERSION) {
            RX_Discard(1u);
            continue;
        }

        payload_len = get_u16_be(&prefix[12]);

        if (payload_len > WSRP_CONTROL_MAX || payload_len > out_capacity) {
            RX_Discard(1u);
            continue;
        }

        received_hcrc = get_u16_be(&s_rx_stream[WSRP_PREFIX_SIZE]);
        expected_hcrc = WSRP_CRC16(prefix, WSRP_PREFIX_SIZE);

        if (received_hcrc != expected_hcrc) {
            RX_Discard(1u);
            continue;
        }

        frame_size = WSRP_HEADER_SIZE +
                     (uint32_t)payload_len +
                     WSRP_TRAILER_SIZE;

        if ((uint32_t)s_rx_stream_len < frame_size) {
            /*
             * A valid-looking frame is not complete yet. Keep every byte and
             * wait for the remainder to arrive.
             */
            return 0u;
        }

        received_fcrc = get_u32_be(
            &s_rx_stream[WSRP_HEADER_SIZE + payload_len]
        );

        expected_fcrc = CRC32_TwoParts(
            prefix,
            WSRP_PREFIX_SIZE,
            &s_rx_stream[WSRP_HEADER_SIZE],
            payload_len
        );

        if (received_fcrc != expected_fcrc) {
            /*
             * Do not delete frame_size bytes: corruption may have shifted the
             * boundary and frame_size could consume the next valid ACK.
             */
            RX_Discard(1u);
            continue;
        }

        info->type = prefix[3];
        info->object_id = get_u32_be(&prefix[4]);
        info->seq = get_u32_be(&prefix[8]);
        info->payload_len = payload_len;

        if (payload_len > 0u) {
            memcpy(
                payload_out,
                &s_rx_stream[WSRP_HEADER_SIZE],
                payload_len
            );
        }

        /*
         * The complete ACK has already been removed from UART5 hardware FIFO
         * and validated. Debug output is safe only now.
         */
        WLogParsedControlFrame(info, payload_out);

        RX_Discard((uint16_t)frame_size);
        return 1u;
    }
}

static WSRP_Status_t ReceiveControlFrame(WSRP_FrameInfo_t *info,
                                         uint8_t *payload_out,
                                         uint16_t out_capacity,
                                         uint32_t timeout_ms) {
    uint32_t started;

    if (info == NULL || payload_out == NULL) {
        return WSRP_ERR_PARAM;
    }

    started = HAL_GetTick();

    /*
     * Busy-poll UART5 while waiting for a short ACK/NACK frame.
     *
     * Do NOT add HAL_Delay(1) here:
     *   at 921600 baud, a 24-byte HELLO_ACK needs only about 0.26 ms.
     *   Sleeping for 1 ms lets the RX FIFO fill and overflow before it is read.
     *
     * This function only runs while waiting for small control responses, so
     * short busy-polling is intentional and safe for the current test.
     */
    while ((HAL_GetTick() - started) < timeout_ms) {
        RX_PumpUART();

        if (RX_TryPopFrame(info, payload_out, out_capacity)) {
            return WSRP_STATUS_OK;
        }
    }

    /*
     * Do not clear s_rx_stream_len here. If timeout occurred after receiving
     * part of a control frame, the following retry can complete that frame.
     */
    return WSRP_ERR_TIMEOUT;
}

/*
 * Wait for a specific control response. Late responses from an earlier
 * protocol stage are discarded without forcing a retransmission.
 */
static WSRP_Status_t WaitForExpectedControlFrame(uint8_t expected_type_a,
                                                 uint8_t expected_type_b,
                                                 uint32_t expected_object_id,
                                                 WSRP_FrameInfo_t *response,
                                                 uint32_t timeout_ms) {
    uint32_t started;
    uint32_t elapsed;
    uint32_t remaining;
    WSRP_Status_t status;

    if (response == NULL) {
        return WSRP_ERR_PARAM;
    }

    started = HAL_GetTick();

    while (1) {
        elapsed = HAL_GetTick() - started;

        if (elapsed >= timeout_ms) {
            return WSRP_ERR_TIMEOUT;
        }

        remaining = timeout_ms - elapsed;

        status = ReceiveControlFrame(
            response,
            s_ctrl_payload,
            WSRP_CONTROL_MAX,
            remaining
        );

        if (status != WSRP_STATUS_OK) {
            return status;
        }

        if (response->object_id != expected_object_id) {
            WLogF(
                "[WSRP] Ignore stale control type=%u object=%lu, expect=%lu\r\n",
                (unsigned int)response->type,
                (unsigned long)response->object_id,
                (unsigned long)expected_object_id
            );
            continue;
        }

        if (response->type != expected_type_a &&
            response->type != expected_type_b) {
            WLogF(
                "[WSRP] Ignore stale control type=%u object=%lu\r\n",
                (unsigned int)response->type,
                (unsigned long)response->object_id
            );
            continue;
        }

        return WSRP_STATUS_OK;
    }
}

/*
 * During a DATA window, ignore duplicate/late ACKs that report no progress.
 * Continue waiting in the same timeout interval instead of retransmitting
 * immediately.
 */
static WSRP_Status_t WaitForDataAck(uint32_t object_id,
                                    uint32_t current_next_seq,
                                    uint32_t total_frames,
                                    WSRP_FrameInfo_t *response,
                                    uint32_t *server_next_seq_out,
                                    uint16_t *server_window_out,
                                    uint32_t timeout_ms) {
    uint32_t started;
    uint32_t elapsed;
    uint32_t remaining;
    uint32_t ack_next_seq;
    uint16_t ack_window;
    WSRP_Status_t status;

    if (response == NULL ||
        server_next_seq_out == NULL ||
        server_window_out == NULL) {
        return WSRP_ERR_PARAM;
    }

    started = HAL_GetTick();

    while (1) {
        elapsed = HAL_GetTick() - started;

        if (elapsed >= timeout_ms) {
            return WSRP_ERR_TIMEOUT;
        }

        remaining = timeout_ms - elapsed;

        status = WaitForExpectedControlFrame(
            WSRP_TYPE_ACK,
            WSRP_TYPE_NACK,
            object_id,
            response,
            remaining
        );

        if (status != WSRP_STATUS_OK) {
            return status;
        }

        if (response->payload_len < 10u) {
            WLogF(
                "[WSRP] Ignore short ACK/NACK object=%lu len=%u\r\n",
                (unsigned long)object_id,
                (unsigned int)response->payload_len
            );
            continue;
        }

        ack_next_seq = get_u32_be(&s_ctrl_payload[0]);
        ack_window = get_u16_be(&s_ctrl_payload[8]);

        if (ack_next_seq > total_frames) {
            return WSRP_ERR_PROTOCOL;
        }

        if (response->type == WSRP_TYPE_ACK &&
            ack_next_seq <= current_next_seq) {
            WLogF(
                "[WSRP] Ignore stale ACK object=%lu ack_seq=%lu current=%lu\r\n",
                (unsigned long)object_id,
                (unsigned long)ack_next_seq,
                (unsigned long)current_next_seq
            );
            continue;
        }

        *server_next_seq_out = ack_next_seq;
        *server_window_out = ack_window;
        return WSRP_STATUS_OK;
    }
}

/* ========================================================================== */
/* Frame transmit                                                             */
/* ========================================================================== */
static WSRP_Status_t SendFrame(uint8_t type, uint32_t object_id, uint32_t seq,
                               const uint8_t *payload, uint16_t payload_len) {
    uint16_t header_crc;
    uint32_t frame_crc;
    uint32_t total_len;
    WSRP_Status_t status;

    if (s_dev == NULL ||
        !s_in_transparent ||
        payload_len > WSRP_PAYLOAD_MAX) {
        return WSRP_ERR_PARAM;
    }

    if (payload_len > 0u && payload == NULL) {
        return WSRP_ERR_PARAM;
    }

    s_tx_frame[0] = WSRP_MAGIC_0;
    s_tx_frame[1] = WSRP_MAGIC_1;
    s_tx_frame[2] = WSRP_VERSION;
    s_tx_frame[3] = type;

    put_u32_be(&s_tx_frame[4], object_id);
    put_u32_be(&s_tx_frame[8], seq);
    put_u16_be(&s_tx_frame[12], payload_len);

    header_crc = WSRP_CRC16(s_tx_frame, WSRP_PREFIX_SIZE);
    put_u16_be(&s_tx_frame[WSRP_PREFIX_SIZE], header_crc);

    if (payload_len > 0u) {
        memcpy(
            &s_tx_frame[WSRP_HEADER_SIZE],
            payload,
            payload_len
        );
    }

    frame_crc = CRC32_TwoParts(
        s_tx_frame,
        WSRP_PREFIX_SIZE,
        payload,
        payload_len
    );

    put_u32_be(
        &s_tx_frame[WSRP_HEADER_SIZE + payload_len],
        frame_crc
    );

    total_len = WSRP_HEADER_SIZE +
                (uint32_t)payload_len +
                WSRP_TRAILER_SIZE;

    status = TT_Write(s_tx_frame, total_len);

    if (status != WSRP_STATUS_OK) {
        return status;
    }

    return WSRP_STATUS_OK;
}

/* ========================================================================== */
/* Public lifecycle                                                           */
/* ========================================================================== */
void WSRP_Init(WiFi_Device_t *dev, int con_id) {
    s_dev = dev;
    s_con_id = con_id;
    s_in_transparent = 0u;
    s_window_frames = WSRP_WINDOW_DEFAULT;
    s_rx_stream_len = 0u;

    memset(s_rx_stream, 0, sizeof(s_rx_stream));

    WLogF("[WSRP] Init con_id=%d\r\n", s_con_id);
}

WSRP_Status_t WSRP_EnterTransparent(void) {
    uint32_t started;

    if (s_dev == NULL || s_dev->huart == NULL) {
        return WSRP_ERR_PARAM;
    }

    WLog("[WSRP] Entering transparent mode...\r\n");

    WiFi_Send_Cmd(s_dev, "AT+SOCKETTT\r\n");

    started = HAL_GetTick();

    while ((HAL_GetTick() - started) < 5000u) {
        UART_ClearErrors();

        if (__HAL_UART_GET_FLAG(s_dev->huart, UART_FLAG_RXNE)) {
            uint8_t ch = (uint8_t)(s_dev->huart->Instance->RDR & 0xFFu);

            if (ch == '>') {
                s_in_transparent = 1u;

                HAL_Delay(50u);
                UART_DrainIncoming();
                s_rx_stream_len = 0u;

                WLog("[WSRP] Transparent mode ACTIVE\r\n");
                return WSRP_STATUS_OK;
            }
        }
    }

    return WSRP_ERR_TRANSPARENT;
}

void WSRP_ExitTransparent(void) {
    if (!s_in_transparent ||
        s_dev == NULL ||
        s_dev->huart == NULL) {
        return;
    }

    WLog("[WSRP] Exiting transparent mode...\r\n");

    HAL_Delay(1100u);
    HAL_UART_Transmit(
        s_dev->huart,
        (uint8_t *)"+++",
        3u,
        HAL_MAX_DELAY
    );
    HAL_Delay(1100u);

    s_in_transparent = 0u;
    s_rx_stream_len = 0u;

    WiFi_Wait_Response(s_dev, 3000u);
}

/* ========================================================================== */
/* HELLO handshake                                                            */
/* ========================================================================== */
WSRP_Status_t WSRP_Handshake(void) {
    uint8_t hello_payload[8];
    WSRP_FrameInfo_t response;
    WSRP_Status_t status;
    uint8_t attempt;

    put_u16_be(&hello_payload[0], WSRP_PAYLOAD_MAX);
    put_u16_be(&hello_payload[2], WSRP_WINDOW_DEFAULT);
    put_u16_be(&hello_payload[4], WSRP_WINDOW_MAX);
    put_u16_be(&hello_payload[6], 0x0001u);

    for (attempt = 0u; attempt <= WSRP_MAX_RETRY; attempt++) {
        status = SendFrame(
            WSRP_TYPE_HELLO,
            0u,
            0u,
            hello_payload,
            (uint16_t)sizeof(hello_payload)
        );

        if (status != WSRP_STATUS_OK) {
            return status;
        }

        status = WaitForExpectedControlFrame(
            WSRP_TYPE_HELLO_ACK,
            WSRP_TYPE_HELLO_ACK,
            0u,
            &response,
            WSRP_ACK_TIMEOUT_MS
        );

        if (status == WSRP_STATUS_OK &&
            response.payload_len >= 4u) {
            uint16_t accepted_payload = get_u16_be(&s_ctrl_payload[0]);
            uint16_t accepted_window = get_u16_be(&s_ctrl_payload[2]);

            if (accepted_payload != WSRP_PAYLOAD_MAX ||
                accepted_window == 0u) {
                return WSRP_ERR_PROTOCOL;
            }

            s_window_frames =
                (accepted_window > WSRP_WINDOW_MAX)
                ? WSRP_WINDOW_MAX
                : accepted_window;

            WLogF(
                "[WSRP] HELLO OK, window=%u payload=%u\r\n",
                s_window_frames,
                accepted_payload
            );

            return WSRP_STATUS_OK;
        }

        WLogF(
            "[WSRP] HELLO retry %u/%u, status=%d\r\n",
            (unsigned int)(attempt + 1u),
            (unsigned int)(WSRP_MAX_RETRY + 1u),
            (int)status
        );
    }

    return WSRP_ERR_RETRY_EXCEEDED;
}

/* ========================================================================== */
/* Send one complete logical object                                           */
/* ========================================================================== */
WSRP_Status_t WSRP_SendObject(uint8_t data_type,
                              uint8_t mode,
                              uint32_t object_id,
                              const uint8_t *data,
                              uint32_t len) {
    uint8_t start_payload[16];
    uint8_t end_payload[8];

    uint32_t total_frames;
    uint32_t object_crc;

    uint32_t next_seq;
    uint32_t window_end;
    uint32_t seq;
    uint32_t offset;
    uint32_t server_next_seq;

    uint16_t chunk_len;
    uint16_t window;
    uint16_t server_window;

    uint8_t retries;

    WSRP_FrameInfo_t response;
    WSRP_Status_t status;

    if (data == NULL || len == 0u || !s_in_transparent) {
        return WSRP_ERR_PARAM;
    }

    total_frames =
        (len + WSRP_PAYLOAD_MAX - 1u) / WSRP_PAYLOAD_MAX;

    object_crc = WSRP_CRC32(data, len);
    window = s_window_frames;

    /*
     * START payload:
     *   total_len(4), total_frames(4), object_crc32(4),
     *   data_type(1), mode(1), requested_window(2)
     */
    put_u32_be(&start_payload[0], len);
    put_u32_be(&start_payload[4], total_frames);
    put_u32_be(&start_payload[8], object_crc);
    start_payload[12] = data_type;
    start_payload[13] = mode;
    put_u16_be(&start_payload[14], window);

    /* ---------------------------------------------------------------------- */
    /* START / START_ACK                                                      */
    /* ---------------------------------------------------------------------- */
    for (retries = 0u; retries <= WSRP_MAX_RETRY; retries++) {
        status = SendFrame(
            WSRP_TYPE_START,
            object_id,
            0u,
            start_payload,
            (uint16_t)sizeof(start_payload)
        );

        if (status != WSRP_STATUS_OK) {
            return status;
        }

        status = WaitForExpectedControlFrame(
            WSRP_TYPE_START_ACK,
            WSRP_TYPE_START_ACK,
            object_id,
            &response,
            WSRP_ACK_TIMEOUT_MS
        );

        if (status == WSRP_STATUS_OK &&
            response.payload_len >= 3u &&
            s_ctrl_payload[0] == 0u) {
            window = get_u16_be(&s_ctrl_payload[1]);

            if (window == 0u || window > WSRP_WINDOW_MAX) {
                window = WSRP_WINDOW_DEFAULT;
            }

            break;
        }

        WLogF(
            "[WSRP] START object=%lu retry=%u status=%d\r\n",
            (unsigned long)object_id,
            (unsigned int)(retries + 1u),
            (int)status
        );
    }

    if (retries > WSRP_MAX_RETRY) {
        return WSRP_ERR_RETRY_EXCEEDED;
    }

    /* ---------------------------------------------------------------------- */
    /* DATA windows / ACK-NACK                                                */
    /* ---------------------------------------------------------------------- */
    next_seq = 0u;
    retries = 0u;

    while (next_seq < total_frames) {
        window_end = next_seq + (uint32_t)window;

        if (window_end > total_frames) {
            window_end = total_frames;
        }

        for (seq = next_seq; seq < window_end; seq++) {
            offset = seq * WSRP_PAYLOAD_MAX;

            chunk_len = (uint16_t)(
                ((len - offset) > WSRP_PAYLOAD_MAX)
                ? WSRP_PAYLOAD_MAX
                : (len - offset)
            );

            status = SendFrame(
                WSRP_TYPE_DATA,
                object_id,
                seq,
                data + offset,
                chunk_len
            );

            if (status != WSRP_STATUS_OK) {
                return status;
            }
        }

        status = WaitForDataAck(
            object_id,
            next_seq,
            total_frames,
            &response,
            &server_next_seq,
            &server_window,
            WSRP_ACK_TIMEOUT_MS
        );

        if (status == WSRP_STATUS_OK) {
            if (server_window > 0u &&
                server_window <= WSRP_WINDOW_MAX) {
                window = server_window;
            }

            if (response.type == WSRP_TYPE_ACK) {
                next_seq = server_next_seq;
                retries = 0u;
            } else {
                next_seq = server_next_seq;
                retries++;

                WLogF(
                    "[WSRP] NACK object=%lu restart_seq=%lu retry=%u\r\n",
                    (unsigned long)object_id,
                    (unsigned long)next_seq,
                    (unsigned int)retries
                );
            }
        } else {
            retries++;

            WLogF(
                "[WSRP] ACK timeout/invalid object=%lu seq=%lu retry=%u status=%d\r\n",
                (unsigned long)object_id,
                (unsigned long)next_seq,
                (unsigned int)retries,
                (int)status
            );
        }

        if (retries > WSRP_MAX_RETRY) {
            return WSRP_ERR_RETRY_EXCEEDED;
        }
    }

    /* ---------------------------------------------------------------------- */
    /* END / END_ACK                                                          */
    /* ---------------------------------------------------------------------- */
    put_u32_be(&end_payload[0], len);
    put_u32_be(&end_payload[4], object_crc);

    for (retries = 0u; retries <= WSRP_MAX_RETRY; retries++) {
        status = SendFrame(
            WSRP_TYPE_END,
            object_id,
            total_frames,
            end_payload,
            (uint16_t)sizeof(end_payload)
        );

        if (status != WSRP_STATUS_OK) {
            return status;
        }

        status = WaitForExpectedControlFrame(
            WSRP_TYPE_END_ACK,
            WSRP_TYPE_END_ACK,
            object_id,
            &response,
            WSRP_ACK_TIMEOUT_MS
        );

        if (status == WSRP_STATUS_OK &&
            response.payload_len >= 1u &&
            s_ctrl_payload[0] == 0u) {
            WLogF(
                "[WSRP] Object %lu complete: %lu bytes, %lu frames\r\n",
                (unsigned long)object_id,
                (unsigned long)len,
                (unsigned long)total_frames
            );

            return WSRP_STATUS_OK;
        }

        WLogF(
            "[WSRP] END object=%lu retry=%u status=%d\r\n",
            (unsigned long)object_id,
            (unsigned int)(retries + 1u),
            (int)status
        );
    }

    return WSRP_ERR_RETRY_EXCEEDED;
}
