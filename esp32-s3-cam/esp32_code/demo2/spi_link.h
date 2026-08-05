#pragma once

#include <Arduino.h>

// =====================================================
// SPI pins
// =====================================================

#define SPI_SCK_PIN    1
#define SPI_MISO_PIN   2
#define SPI_MOSI_PIN   3
#define SPI_CS_PIN     14
#define SPI_READY_PIN  21

// =====================================================
// SPI protocol
// =====================================================

#define SPI_MAGIC               0x53504931UL
#define SPI_HEADER_SIZE         16U
#define SPI_MAX_PAYLOAD_SIZE    16384U

#define SPI_FLAG_REQUEST        0x01U
#define SPI_FLAG_RESPONSE       0x02U

#define SPI_CMD_PING            0x01U
#define SPI_CMD_PROCESS         0x02U  // AES-GCM encrypt one image chunk
#define SPI_CMD_KEM_GET_PUBKEY  0x03U  // opaque relay to/from STM32
#define SPI_CMD_KEM_DECAPSULATE 0x04U  // opaque relay to/from STM32

#define SPI_STATUS_NONE         0x00U
#define SPI_STATUS_HEADER_OK    0x10U
#define SPI_STATUS_OK           0x80U
#define SPI_STATUS_BAD_HEADER   0xE1U
#define SPI_STATUS_BAD_LENGTH   0xE2U
#define SPI_STATUS_BAD_COMMAND  0xE3U
#define SPI_STATUS_PROCESS_FAIL 0xE4U

// Packed into SpiHeader.reserved. STM32 uses these to know
// when to start a new AES-GCM stream (first chunk of a
// frame) and when to finalize + append nonce/tag (last
// chunk). For single-shot commands (PING, KEM_*) callers
// should pass FIRST|LAST - "this request is self-contained".
#define SPI_CHUNK_FLAG_FIRST    0x01U
#define SPI_CHUNK_FLAG_LAST     0x02U
#define SPI_CHUNK_FLAGS_SINGLE  (SPI_CHUNK_FLAG_FIRST | SPI_CHUNK_FLAG_LAST)

// Start with a stable speed. Raise after checking PERF output.
#define SPI_SPEED_HZ            985000U
#define READY_TIMEOUT_MS        5000U

#define SPI_REQUEST_MAX_ATTEMPTS   3U
#define SPI_RETRY_DELAY_MS         2U

// =====================================================
// SPI error phases (surfaced in ERROR packets to the PC)
// =====================================================

#define SPI_ERROR_NONE                    0U
#define SPI_ERROR_WAIT_REQUEST_HEADER     1U
#define SPI_ERROR_WAIT_HEADER_ACK         2U
#define SPI_ERROR_INVALID_HEADER_ACK      3U
#define SPI_ERROR_WAIT_REQUEST_PAYLOAD    4U
#define SPI_ERROR_WAIT_FINAL_HEADER       5U
#define SPI_ERROR_INVALID_FINAL_HEADER    6U
#define SPI_ERROR_WAIT_RESPONSE_PAYLOAD   7U
#define SPI_ERROR_WAIT_FINAL_READY_LOW    8U

extern uint8_t g_spiDebugHeader[SPI_HEADER_SIZE];
extern uint8_t g_spiErrorPhase;

bool SpiLink_Init(void);

// chunkFlags: SPI_CHUNK_FLAG_FIRST / _LAST / both / neither -
// see comment above. Retries internally up to
// SPI_REQUEST_MAX_ATTEMPTS times on transient link errors.
bool SpiLink_Request(
    uint8_t command,
    uint8_t chunkFlags,
    const uint8_t *requestData,
    uint32_t requestLength,
    uint8_t *responseData,
    uint32_t responseCapacity,
    uint32_t &responseLength);
