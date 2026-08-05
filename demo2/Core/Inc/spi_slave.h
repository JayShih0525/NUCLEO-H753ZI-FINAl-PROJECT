#pragma once

#include "main.h"
#include <stdint.h>

#define SPI_MAGIC               0x53504931UL
#define SPI_HEADER_SIZE         16U
#define SPI_MAX_PAYLOAD_SIZE    16384U

#define SPI_FLAG_REQUEST        0x01U
#define SPI_FLAG_RESPONSE       0x02U

#define SPI_CMD_PING            0x01U
#define SPI_CMD_PROCESS         0x02U
#define SPI_CMD_KEM_GET_PUBKEY  0x03U
#define SPI_CMD_KEM_DECAPSULATE 0x04U

#define SPI_STATUS_NONE         0x00U
#define SPI_STATUS_HEADER_OK    0x10U
#define SPI_STATUS_OK           0x80U
#define SPI_STATUS_BAD_HEADER   0xE1U
#define SPI_STATUS_BAD_LENGTH   0xE2U
#define SPI_STATUS_BAD_COMMAND  0xE3U
#define SPI_STATUS_PROCESS_FAIL 0xE4U

// Chunk framing, packed into the header's `reserved` byte.
// PING/KEM_* are single-shot: caller (ESP32) always sends
// FIRST|LAST for those. PROCESS uses these to know when to
// (re)start an AES-GCM stream vs finalize it.
#define SPI_CHUNK_FLAG_FIRST    0x01U
#define SPI_CHUNK_FLAG_LAST     0x02U

#define SPI_TIMEOUT_MS          5000U
#define SPI_READY_LOW_US        20U

#define SPI_READY_GPIO_Port     GPIOC
#define SPI_READY_Pin           GPIO_PIN_6

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t payloadLength;
    uint8_t command;
    uint8_t status;
    uint8_t flags;
    uint8_t reserved; // chunk flags for PROCESS, 0 otherwise
} SpiHeader;

void SPI_SlaveInit(void);

// Blocks waiting for one request, dispatches it, and sends
// the response. Call this in a tight loop from main().
void SPI_HandleOneRequest(void);
