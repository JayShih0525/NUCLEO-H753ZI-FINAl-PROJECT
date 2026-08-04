#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include "esp_camera.h"

// =====================================================
// Computer transport: UART0
// =====================================================

#define COMPUTER_SERIAL Serial
#define COMPUTER_UART_BAUD_RATE 4000000U

// UART packet, 24-byte header, all multi-byte integers are Big Endian.
//
// 0..3   magic          "CAM1"
// 4      version        1
// 5      packetType     1=DATA, 2=FRAME_END, 3=ERROR
// 6      status         0=OK, otherwise error code
// 7      reserved       0
// 8..11  frameId
// 12..15 totalFrameLength
// 16..17 chunkIndex
// 18..19 chunkCount
// 20..23 payloadLength
// 24..   payload

#define COMPUTER_MAGIC_TEXT       "CAM1"
#define COMPUTER_MAGIC_SIZE       4U
#define COMPUTER_PROTOCOL_VERSION 1U
#define COMPUTER_HEADER_SIZE      24U

#define COMPUTER_PACKET_DATA      0x01U
#define COMPUTER_PACKET_FRAME_END 0x02U
#define COMPUTER_PACKET_ERROR     0x03U

#define COMPUTER_STATUS_OK             0x00U
#define COMPUTER_STATUS_CAMERA_FAILED  0x01U
#define COMPUTER_STATUS_FRAME_INVALID  0x02U
#define COMPUTER_STATUS_SPI_FAILED     0x03U
#define COMPUTER_STATUS_LENGTH_ERROR   0x04U
#define COMPUTER_STATUS_UART_FAILED    0x05U

#define MAX_JPEG_SIZE (1024U * 1024U)
#define FRAME_INTERVAL_MS 0U

// =====================================================
// ESP32-S3-CAM + OV3660 pins
// =====================================================

#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// =====================================================
// SPI pins
// =====================================================

#define SPI_SCK_PIN   1
#define SPI_MISO_PIN  2
#define SPI_MOSI_PIN  3
#define SPI_CS_PIN    14
#define SPI_READY_PIN 21

// =====================================================
// Original SPI protocol: unchanged
// =====================================================

#define SPI_MAGIC               0x53504931UL
#define SPI_HEADER_SIZE         16U
#define SPI_MAX_PAYLOAD_SIZE    16384U

#define SPI_FLAG_REQUEST        0x01U
#define SPI_FLAG_RESPONSE       0x02U

#define SPI_CMD_PING            0x01U
#define SPI_CMD_PROCESS         0x02U

#define SPI_STATUS_NONE         0x00U
#define SPI_STATUS_OK           0x80U
#define SPI_STATUS_BAD_HEADER   0xE1U
#define SPI_STATUS_BAD_LENGTH   0xE2U
#define SPI_STATUS_BAD_COMMAND  0xE3U
#define SPI_STATUS_PROCESS_FAIL 0xE4U
#define SPI_STATUS_HEADER_OK    0x10U

#define SPI_SPEED_HZ     800000U
#define READY_TIMEOUT_MS 5000U

// =====================================================
// Global state
// =====================================================

static uint32_t g_frameId = 0U;
static uint32_t g_lastFrameTime = 0U;

SPIClass h753SPI(FSPI);

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
static uint8_t responsePayload[SPI_MAX_PAYLOAD_SIZE];
static uint8_t dummyBuffer[SPI_MAX_PAYLOAD_SIZE];
static uint32_t nextSequence = 1U;

// =====================================================
// Big-endian helpers
// =====================================================

static void WriteU16BE(uint8_t *destination, uint16_t value)
{
    destination[0] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    destination[1] = static_cast<uint8_t>(value & 0xFFU);
}

static void WriteU32BE(uint8_t *destination, uint32_t value)
{
    destination[0] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
    destination[1] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    destination[2] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    destination[3] = static_cast<uint8_t>(value & 0xFFU);
}

static uint32_t ReadU32BE(const uint8_t *source)
{
    return
        (static_cast<uint32_t>(source[0]) << 24U) |
        (static_cast<uint32_t>(source[1]) << 16U) |
        (static_cast<uint32_t>(source[2]) << 8U) |
        static_cast<uint32_t>(source[3]);
}

// =====================================================
// Computer output functions
// Only this section needs replacement when UART is changed
// to TCP, UDP, USB CDC, etc.
// =====================================================

static void ComputerTransportBegin(void)
{
    // USB CDC On Boot = Disabled; UART0 through CH340.
    COMPUTER_SERIAL.begin(COMPUTER_UART_BAUD_RATE);
}

static bool ComputerWriteAll(
    const uint8_t *data,
    size_t length)
{
    if ((data == nullptr) && (length > 0U))
    {
        return false;
    }

    size_t totalWritten = 0U;
    uint32_t lastProgressTime = millis();

    while (totalWritten < length)
    {
        const size_t remaining =
            length - totalWritten;

        const size_t blockSize =
            (remaining > 4096U)
                ? 4096U
                : remaining;

        const size_t written =
            COMPUTER_SERIAL.write(
                data + totalWritten,
                blockSize);

        if (written == 0U)
        {
            if ((millis() - lastProgressTime) >= 5000U)
            {
                return false;
            }

            delay(1);
            continue;
        }

        totalWritten += written;
        lastProgressTime = millis();
    }

    return true;
}

static bool ComputerSendPacket(
    uint8_t packetType,
    uint8_t status,
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkIndex,
    uint16_t chunkCount,
    const uint8_t *payload,
    uint32_t payloadLength)
{
    uint8_t packetHeader[COMPUTER_HEADER_SIZE] = {0};

    memcpy(&packetHeader[0], COMPUTER_MAGIC_TEXT, COMPUTER_MAGIC_SIZE);
    packetHeader[4] = COMPUTER_PROTOCOL_VERSION;
    packetHeader[5] = packetType;
    packetHeader[6] = status;
    packetHeader[7] = 0U;

    WriteU32BE(&packetHeader[8], frameId);
    WriteU32BE(&packetHeader[12], totalFrameLength);
    WriteU16BE(&packetHeader[16], chunkIndex);
    WriteU16BE(&packetHeader[18], chunkCount);
    WriteU32BE(&packetHeader[20], payloadLength);

    if (!ComputerWriteAll(packetHeader, sizeof(packetHeader)))
    {
        return false;
    }

    if ((payloadLength > 0U) &&
        !ComputerWriteAll(payload, payloadLength))
    {
        return false;
    }

    return true;
}

static bool ComputerSendDataChunk(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkIndex,
    uint16_t chunkCount,
    const uint8_t *data,
    uint32_t length)
{
    return ComputerSendPacket(
        COMPUTER_PACKET_DATA,
        COMPUTER_STATUS_OK,
        frameId,
        totalFrameLength,
        chunkIndex,
        chunkCount,
        data,
        length);
}

static bool ComputerSendFrameEnd(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkCount)
{
    return ComputerSendPacket(
        COMPUTER_PACKET_FRAME_END,
        COMPUTER_STATUS_OK,
        frameId,
        totalFrameLength,
        chunkCount,
        chunkCount,
        nullptr,
        0U);
}

static bool ComputerSendError(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkIndex,
    uint16_t chunkCount,
    uint8_t status)
{
    return ComputerSendPacket(
        COMPUTER_PACKET_ERROR,
        status,
        frameId,
        totalFrameLength,
        chunkIndex,
        chunkCount,
        nullptr,
        0U);
}

static void ComputerFlush(void)
{
    COMPUTER_SERIAL.flush();
}

// =====================================================
// Camera
// =====================================================

static bool InitCamera(void)
{
    camera_config_t config = {};

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 5;

    if (psramFound())
    {
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }

    return esp_camera_init(&config) == ESP_OK;
}

static bool IsFrameBasicValid(const camera_fb_t *frame)
{
    return
        (frame != nullptr) &&
        (frame->buf != nullptr) &&
        (frame->len > 0U) &&
        (frame->len <= MAX_JPEG_SIZE);
}

// =====================================================
// Original SPI header encode/decode
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
// Original READY synchronization
// =====================================================

static bool WaitReadyLevel(
    int expectedLevel,
    uint32_t timeoutMs)
{
    const uint32_t startTime = millis();

    while (digitalRead(SPI_READY_PIN) != expectedLevel)
    {
        if ((millis() - startTime) >= timeoutMs)
        {
            return false;
        }

        delayMicroseconds(5);
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
// Original SPI transaction
// =====================================================

static void SpiTransfer(
    const uint8_t *txBuffer,
    uint8_t *rxBuffer,
    size_t length)
{
    h753SPI.beginTransaction(
        SPISettings(
            SPI_SPEED_HZ,
            MSBFIRST,
            SPI_MODE0));

    delayMicroseconds(2);

    digitalWrite(SPI_CS_PIN, LOW);
    delayMicroseconds(2);

    h753SPI.transferBytes(
        const_cast<uint8_t *>(txBuffer),
        rxBuffer,
        length);

    delayMicroseconds(2);
    digitalWrite(SPI_CS_PIN, HIGH);

    h753SPI.endTransaction();
}

// =====================================================
// Original SpiRequest: same five-phase protocol
// =====================================================

static bool SpiRequest(
    uint8_t command,
    const uint8_t *requestData,
    uint32_t requestLength,
    uint8_t *responseData,
    uint32_t responseCapacity,
    uint32_t &responseLength)
{
    responseLength = 0U;

    if (requestLength > SPI_MAX_PAYLOAD_SIZE)
    {
        return false;
    }

    if ((requestLength > 0U) && (requestData == nullptr))
    {
        return false;
    }

    const uint32_t sequence = nextSequence++;

    SpiHeader requestHeader = {};
    requestHeader.magic = SPI_MAGIC;
    requestHeader.sequence = sequence;
    requestHeader.payloadLength = requestLength;
    requestHeader.command = command;
    requestHeader.status = SPI_STATUS_NONE;
    requestHeader.flags = SPI_FLAG_REQUEST;
    requestHeader.reserved = 0U;

    EncodeHeader(headerTx, requestHeader);
    memset(headerRx, 0, sizeof(headerRx));

    // Phase 1: Send Request Header
    if (!WaitReadyLevel(HIGH, READY_TIMEOUT_MS))
    {
        return false;
    }

    SpiTransfer(headerTx, headerRx, SPI_HEADER_SIZE);

    // Phase 2: Receive Header ACK
    if (!WaitNextReady())
    {
        return false;
    }

    memset(headerTx, 0, sizeof(headerTx));
    memset(headerRx, 0, sizeof(headerRx));

    SpiTransfer(headerTx, headerRx, SPI_HEADER_SIZE);

    SpiHeader ackHeader = {};
    DecodeHeader(headerRx, ackHeader);

    if ((ackHeader.magic != SPI_MAGIC) ||
        (ackHeader.sequence != sequence) ||
        (ackHeader.command != command) ||
        (ackHeader.flags != SPI_FLAG_RESPONSE) ||
        (ackHeader.payloadLength != 0U) ||
        (ackHeader.status != SPI_STATUS_HEADER_OK))
    {
        WaitReadyLevel(LOW, READY_TIMEOUT_MS);
        return false;
    }

    // Phase 3: Send Request Payload
    if (requestLength > 0U)
    {
        if (!WaitNextReady())
        {
            return false;
        }

        memset(dummyBuffer, 0, requestLength);
        SpiTransfer(requestData, dummyBuffer, requestLength);
    }

    // Phase 4: Receive Final Response Header
    if (!WaitNextReady())
    {
        return false;
    }

    memset(headerTx, 0, sizeof(headerTx));
    memset(headerRx, 0, sizeof(headerRx));

    SpiTransfer(headerTx, headerRx, SPI_HEADER_SIZE);

    SpiHeader responseHeader = {};
    DecodeHeader(headerRx, responseHeader);

    if ((responseHeader.magic != SPI_MAGIC) ||
        (responseHeader.sequence != sequence) ||
        (responseHeader.command != command) ||
        (responseHeader.flags != SPI_FLAG_RESPONSE) ||
        (responseHeader.payloadLength > SPI_MAX_PAYLOAD_SIZE) ||
        (responseHeader.payloadLength > responseCapacity))
    {
        return false;
    }

    // Phase 5: Receive Final Response Payload
    if (responseHeader.payloadLength > 0U)
    {
        if (responseData == nullptr)
        {
            return false;
        }

        if (!WaitNextReady())
        {
            return false;
        }

        memset(dummyBuffer, 0, responseHeader.payloadLength);

        SpiTransfer(
            dummyBuffer,
            responseData,
            responseHeader.payloadLength);
    }

    responseLength = responseHeader.payloadLength;

    WaitReadyLevel(LOW, READY_TIMEOUT_MS);

    return responseHeader.status == SPI_STATUS_OK;
}

// =====================================================
// Process one camera frame without building a second frame
// =====================================================

static bool ProcessAndSendFrame(const camera_fb_t *frame)
{
    if (!IsFrameBasicValid(frame))
    {
        ComputerSendError(
            g_frameId,
            0U,
            0U,
            0U,
            COMPUTER_STATUS_FRAME_INVALID);
        // ComputerFlush();
        return false;
    }

    const uint32_t totalLength = static_cast<uint32_t>(frame->len);
    const uint32_t chunkCount32 =
        (totalLength + SPI_MAX_PAYLOAD_SIZE - 1U) /
        SPI_MAX_PAYLOAD_SIZE;

    if ((chunkCount32 == 0U) || (chunkCount32 > 0xFFFFU))
    {
        ComputerSendError(
            g_frameId,
            totalLength,
            0U,
            0U,
            COMPUTER_STATUS_LENGTH_ERROR);
        ComputerFlush();
        return false;
    }

    const uint16_t chunkCount = static_cast<uint16_t>(chunkCount32);
    uint32_t offset = 0U;

    for (uint16_t chunkIndex = 0U;
         chunkIndex < chunkCount;
         chunkIndex++)
    {
        const uint32_t remaining = totalLength - offset;
        const uint32_t chunkLength =
            (remaining > SPI_MAX_PAYLOAD_SIZE)
                ? SPI_MAX_PAYLOAD_SIZE
                : remaining;

        uint32_t responseLength = 0U;

        const bool spiOk = SpiRequest(
            SPI_CMD_PROCESS,
            frame->buf + offset,
            chunkLength,
            responsePayload,
            sizeof(responsePayload),
            responseLength);

        if (!spiOk)
        {
            ComputerSendError(
                g_frameId,
                totalLength,
                chunkIndex,
                chunkCount,
                COMPUTER_STATUS_SPI_FAILED);
            ComputerFlush();
            return false;
        }

        if (responseLength != chunkLength)
        {
            ComputerSendError(
                g_frameId,
                totalLength,
                chunkIndex,
                chunkCount,
                COMPUTER_STATUS_LENGTH_ERROR);
            ComputerFlush();
            return false;
        }

        if (!ComputerSendDataChunk(
                g_frameId,
                totalLength,
                chunkIndex,
                chunkCount,
                responsePayload,
                responseLength))
        {
            return false;
        }

        offset += chunkLength;
    }

    if (!ComputerSendFrameEnd(
            g_frameId,
            totalLength,
            chunkCount))
    {
        return false;
    }

    // Flush once per complete frame, not once per chunk.
    ComputerFlush();
    return true;
}

// =====================================================
// Arduino setup / loop
// =====================================================

void setup()
{
    ComputerTransportBegin();
    delay(1000);

    pinMode(SPI_CS_PIN, OUTPUT);
    digitalWrite(SPI_CS_PIN, HIGH);

    pinMode(SPI_READY_PIN, INPUT_PULLDOWN);

    memset(dummyBuffer, 0, sizeof(dummyBuffer));

    h753SPI.begin(
        SPI_SCK_PIN,
        SPI_MISO_PIN,
        SPI_MOSI_PIN,
        SPI_CS_PIN);

    if (!InitCamera())
    {
        while (true)
        {
            ComputerSendError(
                g_frameId,
                0U,
                0U,
                0U,
                COMPUTER_STATUS_CAMERA_FAILED);
            ComputerFlush();
            delay(1000);
        }
    }

    // Discard unstable initial frames.
    for (int i = 0; i < 3; i++)
    {
        camera_fb_t *frame = esp_camera_fb_get();

        if (frame != nullptr)
        {
            esp_camera_fb_return(frame);
        }

        delay(100);
    }

    // Allow Python time to open the serial port.
    delay(1500);
}

void loop()
{
    camera_fb_t *frame = esp_camera_fb_get();

    if (frame == nullptr)
    {
        ComputerSendError(
            g_frameId,
            0U,
            0U,
            0U,
            COMPUTER_STATUS_CAMERA_FAILED);

        return;
    }

    ProcessAndSendFrame(frame);

    esp_camera_fb_return(frame);
    g_frameId++;
}