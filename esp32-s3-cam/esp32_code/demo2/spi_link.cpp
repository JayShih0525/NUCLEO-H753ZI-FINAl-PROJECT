#include "spi_link.h"
#include "byte_utils.h"

#include <SPI.h>
#include <string.h>

static SPIClass h753SPI(FSPI);

struct SpiHeader
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t payloadLength;
    uint8_t command;
    uint8_t status;
    uint8_t flags;
    uint8_t reserved;
};

static uint8_t headerTx[SPI_HEADER_SIZE];
static uint8_t headerRx[SPI_HEADER_SIZE];
static uint8_t dummyBuffer[SPI_MAX_PAYLOAD_SIZE];

static uint32_t g_nextSequence = 1U;

uint8_t g_spiDebugHeader[SPI_HEADER_SIZE];
uint8_t g_spiErrorPhase = SPI_ERROR_NONE;

// =====================================================
// SPI header encode/decode
// =====================================================

static void EncodeHeader(uint8_t *buffer, const SpiHeader &header)
{
    WriteU32BE(&buffer[0], header.magic);
    WriteU32BE(&buffer[4], header.sequence);
    WriteU32BE(&buffer[8], header.payloadLength);

    buffer[12] = header.command;
    buffer[13] = header.status;
    buffer[14] = header.flags;
    buffer[15] = header.reserved;
}

static void DecodeHeader(const uint8_t *buffer, SpiHeader &header)
{
    header.magic = ReadU32BE(&buffer[0]);
    header.sequence = ReadU32BE(&buffer[4]);
    header.payloadLength = ReadU32BE(&buffer[8]);

    header.command = buffer[12];
    header.status = buffer[13];
    header.flags = buffer[14];
    header.reserved = buffer[15];
}

// =====================================================
// READY synchronization
// =====================================================

static bool WaitReadyLevel(int expectedLevel, uint32_t timeoutMs)
{
    const uint32_t start = millis();

    while (digitalRead(SPI_READY_PIN) != expectedLevel)
    {
        if ((millis() - start) >= timeoutMs)
        {
            return false;
        }

        delayMicroseconds(20);
    }

    return true;
}

static bool WaitNextReady(void)
{
    if (!WaitReadyLevel(LOW, READY_TIMEOUT_MS))
    {
        return false;
    }

    if (!WaitReadyLevel(HIGH, READY_TIMEOUT_MS))
    {
        return false;
    }

    return true;
}

// =====================================================
// SPI transfer
// =====================================================

static void SpiTransfer(
    const uint8_t *txBuffer,
    uint8_t *rxBuffer,
    size_t length)
{
    h753SPI.beginTransaction(
        SPISettings(SPI_SPEED_HZ, MSBFIRST, SPI_MODE0));

    delayMicroseconds(200);

    digitalWrite(SPI_CS_PIN, LOW);
    delayMicroseconds(10);

    h753SPI.transferBytes(
        const_cast<uint8_t *>(txBuffer),
        rxBuffer,
        length);

    delayMicroseconds(10);
    digitalWrite(SPI_CS_PIN, HIGH);

    h753SPI.endTransaction();
}

// =====================================================
// Five-phase SPI request (single attempt)
// =====================================================

static bool SpiRequestAttempt(
    uint8_t command,
    uint8_t chunkFlags,
    const uint8_t *requestData,
    uint32_t requestLength,
    uint8_t *responseData,
    uint32_t responseCapacity,
    uint32_t &responseLength)
{
    responseLength = 0U;
    g_spiErrorPhase = SPI_ERROR_NONE;
    memset(g_spiDebugHeader, 0, sizeof(g_spiDebugHeader));

    if (requestLength > SPI_MAX_PAYLOAD_SIZE)
    {
        g_spiErrorPhase = SPI_ERROR_INVALID_HEADER_ACK;
        return false;
    }

    if ((requestLength > 0U) && (requestData == nullptr))
    {
        g_spiErrorPhase = SPI_ERROR_INVALID_HEADER_ACK;
        return false;
    }

    const uint32_t sequence = g_nextSequence++;

    SpiHeader requestHeader = {};
    requestHeader.magic = SPI_MAGIC;
    requestHeader.sequence = sequence;
    requestHeader.payloadLength = requestLength;
    requestHeader.command = command;
    requestHeader.status = SPI_STATUS_NONE;
    requestHeader.flags = SPI_FLAG_REQUEST;
    requestHeader.reserved = chunkFlags;

    EncodeHeader(headerTx, requestHeader);
    memset(headerRx, 0, sizeof(headerRx));

    if (!WaitReadyLevel(HIGH, READY_TIMEOUT_MS))
    {
        g_spiErrorPhase = SPI_ERROR_WAIT_REQUEST_HEADER;
        return false;
    }

    SpiTransfer(headerTx, headerRx, SPI_HEADER_SIZE);

    if (!WaitNextReady())
    {
        g_spiErrorPhase = SPI_ERROR_WAIT_HEADER_ACK;
        return false;
    }

    memset(headerTx, 0, sizeof(headerTx));
    memset(headerRx, 0, sizeof(headerRx));

    SpiTransfer(headerTx, headerRx, SPI_HEADER_SIZE);

    memcpy(g_spiDebugHeader, headerRx, SPI_HEADER_SIZE);

    SpiHeader ackHeader = {};
    DecodeHeader(headerRx, ackHeader);

    if ((ackHeader.magic != SPI_MAGIC) ||
        (ackHeader.sequence != sequence) ||
        (ackHeader.command != command) ||
        (ackHeader.flags != SPI_FLAG_RESPONSE) ||
        (ackHeader.payloadLength != 0U) ||
        (ackHeader.status != SPI_STATUS_HEADER_OK))
    {
        g_spiErrorPhase = SPI_ERROR_INVALID_HEADER_ACK;
        WaitReadyLevel(LOW, READY_TIMEOUT_MS);
        return false;
    }

    if (requestLength > 0U)
    {
        if (!WaitNextReady())
        {
            g_spiErrorPhase = SPI_ERROR_WAIT_REQUEST_PAYLOAD;
            return false;
        }

        SpiTransfer(requestData, dummyBuffer, requestLength);
    }

    if (!WaitNextReady())
    {
        g_spiErrorPhase = SPI_ERROR_WAIT_FINAL_HEADER;
        return false;
    }

    memset(headerTx, 0, sizeof(headerTx));
    memset(headerRx, 0, sizeof(headerRx));

    SpiTransfer(headerTx, headerRx, SPI_HEADER_SIZE);

    memcpy(g_spiDebugHeader, headerRx, SPI_HEADER_SIZE);

    SpiHeader responseHeader = {};
    DecodeHeader(headerRx, responseHeader);

    if ((responseHeader.magic != SPI_MAGIC) ||
        (responseHeader.sequence != sequence) ||
        (responseHeader.command != command) ||
        (responseHeader.flags != SPI_FLAG_RESPONSE) ||
        (responseHeader.payloadLength > SPI_MAX_PAYLOAD_SIZE) ||
        (responseHeader.payloadLength > responseCapacity))
    {
        g_spiErrorPhase = SPI_ERROR_INVALID_FINAL_HEADER;
        return false;
    }

    if (responseHeader.payloadLength > 0U)
    {
        if (responseData == nullptr)
        {
            g_spiErrorPhase = SPI_ERROR_INVALID_FINAL_HEADER;
            return false;
        }

        if (!WaitNextReady())
        {
            g_spiErrorPhase = SPI_ERROR_WAIT_RESPONSE_PAYLOAD;
            return false;
        }

        SpiTransfer(
            dummyBuffer,
            responseData,
            responseHeader.payloadLength);
    }

    responseLength = responseHeader.payloadLength;

    if (!WaitReadyLevel(LOW, READY_TIMEOUT_MS))
    {
        g_spiErrorPhase = SPI_ERROR_WAIT_FINAL_READY_LOW;
        return false;
    }

    if (responseHeader.status != SPI_STATUS_OK)
    {
        g_spiErrorPhase = SPI_ERROR_INVALID_FINAL_HEADER;
        return false;
    }

    return true;
}

// =====================================================
// Public entry point (with retry)
// =====================================================

bool SpiLink_Init(void)
{
    pinMode(SPI_CS_PIN, OUTPUT);
    digitalWrite(SPI_CS_PIN, HIGH);

    pinMode(SPI_READY_PIN, INPUT_PULLDOWN);

    memset(dummyBuffer, 0, sizeof(dummyBuffer));

    h753SPI.begin(
        SPI_SCK_PIN,
        SPI_MISO_PIN,
        SPI_MOSI_PIN,
        SPI_CS_PIN);

    return true;
}

bool SpiLink_Request(
    uint8_t command,
    uint8_t chunkFlags,
    const uint8_t *requestData,
    uint32_t requestLength,
    uint8_t *responseData,
    uint32_t responseCapacity,
    uint32_t &responseLength)
{
    for (uint32_t attempt = 1U;
         attempt <= SPI_REQUEST_MAX_ATTEMPTS;
         attempt++)
    {
        const bool ok = SpiRequestAttempt(
            command,
            chunkFlags,
            requestData,
            requestLength,
            responseData,
            responseCapacity,
            responseLength);

        if (ok)
        {
            return true;
        }

        if (attempt == SPI_REQUEST_MAX_ATTEMPTS)
        {
            return false;
        }

        // NOTE: never Serial.printf() here - see
        // transport_uart.cpp. Serial IS the protocol
        // channel; debug text would corrupt it.

        WaitReadyLevel(LOW, READY_TIMEOUT_MS);
        delay(SPI_RETRY_DELAY_MS);
    }

    return false;
}
