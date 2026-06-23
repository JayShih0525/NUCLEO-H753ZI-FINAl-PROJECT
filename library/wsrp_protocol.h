/**
  ******************************************************************************
  * @file    wsrp_protocol.h
  * @brief   WSRP reliable windowed transport over Ai-WB2 TCP transparent mode
  *
  * Link:
  *   STM32H753ZI -> UART5 -> Ai-WB2 Transparent TCP -> Python Server
  *
  * Frame format, big-endian:
  *   MAGIC(2) VERSION(1) TYPE(1) OBJECT_ID(4) SEQ(4) PAYLOAD_LEN(2)
  *   HEADER_CRC16(2) PAYLOAD(N) FRAME_CRC32(4)
  *
  * Reliability:
  *   HELLO -> HELLO_ACK
  *   START -> START_ACK
  *   DATA window -> ACK / NACK
  *   END -> END_ACK
  ******************************************************************************
  */
#ifndef WSRP_PROTOCOL_H
#define WSRP_PROTOCOL_H

#include "main.h"
#include "wifi_driver.h"
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Wire protocol                                                              */
/* -------------------------------------------------------------------------- */
#define WSRP_MAGIC_0                    0xABu
#define WSRP_MAGIC_1                    0xCDu
#define WSRP_VERSION                    0x02u

#define WSRP_PREFIX_SIZE                14u  /* MAGIC through PAYLOAD_LEN */
#define WSRP_HEADER_SIZE                16u  /* PREFIX + HEADER_CRC16 */
#define WSRP_TRAILER_SIZE                4u  /* FRAME_CRC32 */
#define WSRP_FRAME_OVERHEAD             20u

#define WSRP_PAYLOAD_MAX              4096u
#define WSRP_CONTROL_MAX                64u

/* -------------------------------------------------------------------------- */
/* Performance / reliability baseline                                        */
/* -------------------------------------------------------------------------- */
/*
 * Start with these conservative settings. When object 1~10 are all stable,
 * increase UART_BURST_BYTES to 512 or 1024 before increasing WINDOW_DEFAULT.
 */
#define WSRP_WINDOW_DEFAULT              	4u  /* 4 x 4096 = 16 KiB per ACK */
#define WSRP_WINDOW_MAX                 		16u
#define WSRP_ACK_TIMEOUT_MS           		3000u
#define WSRP_MAX_RETRY					5u
#define WSRP_INTER_FRAME_DELAY_MS	0u /* must listen for ACK immediately */

/*
 * Critical fix: throttle inside one 4096-byte DATA frame.
 * Without this, a full frame is pushed continuously into Ai-WB2 and byte
 * loss can occur before the next window ACK is reached.
 */
#define WSRP_UART_BURST_BYTES		512u
#define WSRP_UART_BURST_DELAY_MS	1u
#define WSRP_RX_DEBUG                    		0u

/* -------------------------------------------------------------------------- */
/* Frame types and payload modes                                              */
/* -------------------------------------------------------------------------- */
typedef enum {
    WSRP_TYPE_HELLO       = 0x01,
    WSRP_TYPE_HELLO_ACK   = 0x02,
    WSRP_TYPE_START       = 0x10,
    WSRP_TYPE_START_ACK   = 0x11,
    WSRP_TYPE_DATA        = 0x12,
    WSRP_TYPE_ACK         = 0x13,
    WSRP_TYPE_NACK        = 0x14,
    WSRP_TYPE_END         = 0x15,
    WSRP_TYPE_END_ACK     = 0x16,
    WSRP_TYPE_PING        = 0x20,
    WSRP_TYPE_PONG        = 0x21,
    WSRP_TYPE_ABORT       = 0xFF
} WSRP_FrameType_t;

typedef enum {
    WSRP_DATA_RAW         = 0x00,
    WSRP_DATA_JPEG        = 0x01,
    WSRP_DATA_CIPHERTEXT  = 0x02
} WSRP_DataType_t;

typedef enum {
    WSRP_MODE_RELIABLE    = 0x00,
    WSRP_MODE_LIVE        = 0x01
} WSRP_Mode_t;

typedef enum {
    WSRP_STATUS_OK = 0,
    WSRP_ERR_PARAM,
    WSRP_ERR_TIMEOUT,
    WSRP_ERR_PROTOCOL,
    WSRP_ERR_HEADER_CRC,
    WSRP_ERR_FRAME_CRC,
    WSRP_ERR_TRANSPARENT,
    WSRP_ERR_RETRY_EXCEEDED,
    WSRP_ERR_UART
} WSRP_Status_t;

typedef struct {
    uint8_t  type;
    uint32_t object_id;
    uint32_t seq;
    uint16_t payload_len;
} WSRP_FrameInfo_t;

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
void WSRP_Init(WiFi_Device_t *dev, int con_id);
WSRP_Status_t WSRP_EnterTransparent(void);
void WSRP_ExitTransparent(void);
WSRP_Status_t WSRP_Handshake(void);

WSRP_Status_t WSRP_SendObject(uint8_t data_type,
                              uint8_t mode,
                              uint32_t object_id,
                              const uint8_t *data,
                              uint32_t len);

uint16_t WSRP_CRC16(const uint8_t *data, uint32_t len);
uint32_t WSRP_CRC32(const uint8_t *data, uint32_t len);

#endif /* WSRP_PROTOCOL_H */
