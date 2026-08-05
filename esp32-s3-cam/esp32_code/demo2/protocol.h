#pragma once

#include <Arduino.h>

// =====================================================
// ESP32 <-> Computer packet protocol ("CAM1")
// =====================================================
//
// 0..3   magic              "CAM1"
// 4      version            1
// 5      packetType
//        1 = DATA               ESP32 -> PC, image chunk
//        2 = FRAME_END          ESP32 -> PC
//        3 = ERROR              ESP32 -> PC
//        4 = PERFORMANCE        ESP32 -> PC
//        5 = CONTROL_REQUEST    PC -> ESP32, relay to STM32
//        6 = CONTROL_RESPONSE   ESP32 -> PC, STM32's reply
// 6      status  (DATA/ERROR/etc.)
//        SPI command to forward (CONTROL_REQUEST only)
// 7      reserved
// 8..11  frameId (image packets) / requestId (control packets)
// 12..15 totalFrameLength (unused for control packets)
// 16..17 chunkIndex / SPI error phase
// 18..19 chunkCount
// 20..23 payloadLength
// 24..   payload
//
// CONTROL_REQUEST/RESPONSE payloads are opaque to the
// ESP32 - it only relays them to/from STM32 over SPI via
// spi_link.h. It never parses KEM/crypto content.
//
// All multi-byte integers are Big Endian.
// =====================================================

#define COMPUTER_MAGIC_TEXT       "CAM1"
#define COMPUTER_MAGIC_SIZE       4U
#define COMPUTER_PROTOCOL_VERSION 1U
#define COMPUTER_HEADER_SIZE      24U

#define COMPUTER_PACKET_DATA              0x01U
#define COMPUTER_PACKET_FRAME_END         0x02U
#define COMPUTER_PACKET_ERROR             0x03U
#define COMPUTER_PACKET_PERFORMANCE       0x04U
#define COMPUTER_PACKET_CONTROL_REQUEST   0x05U
#define COMPUTER_PACKET_CONTROL_RESPONSE  0x06U

#define COMPUTER_STATUS_OK             0x00U
#define COMPUTER_STATUS_CAMERA_FAILED  0x01U
#define COMPUTER_STATUS_FRAME_INVALID  0x02U
#define COMPUTER_STATUS_SPI_FAILED     0x03U
#define COMPUTER_STATUS_LENGTH_ERROR   0x04U
#define COMPUTER_STATUS_OUTPUT_FAILED  0x05U

#define CONTROL_MAX_PAYLOAD 4096U

bool Protocol_Init(void);

// ---- Image path (unchanged behaviour from the original
// ComputerSend* functions, just routed through Transport_*
// instead of touching Serial directly) ----

bool Protocol_SendDataChunk(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkIndex,
    uint16_t chunkCount,
    const uint8_t *data,
    uint32_t length);

bool Protocol_SendFrameEnd(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkCount);

bool Protocol_SendError(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t errorPhase,
    uint16_t chunkCount,
    uint8_t status,
    const uint8_t *debugPayload,
    uint32_t debugLength);

bool Protocol_SendPerformance(
    uint32_t frameId,
    uint32_t captureUs,
    uint32_t spiUs,
    uint32_t usbUs,
    uint32_t totalUs,
    uint32_t jpegSize);

// ---- Control path (KEM handshake relay, and later
// Dilithium) ----

struct ControlRequest
{
    uint32_t requestId;
    uint8_t spiCommand;
    uint32_t payloadLength;
    uint8_t *payload; // points into an internal static buffer,
                       // valid until the next Protocol_Poll call
};

// Call once per loop() iteration. Returns true and fills
// `out` only if a complete CONTROL_REQUEST packet was
// available and successfully parsed right now. Does not
// block if nothing is waiting.
bool Protocol_PollControlRequest(ControlRequest &out);

bool Protocol_SendControlResponse(
    uint32_t requestId,
    uint8_t status,
    const uint8_t *payload,
    uint32_t payloadLength);
