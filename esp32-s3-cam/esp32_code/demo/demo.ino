#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include "esp_camera.h"
#include "esp_log.h"

// =====================================================
// Computer transport: ESP32-S3 Native USB CDC
// =====================================================

#define COMPUTER_SERIAL Serial
#define COMPUTER_SERIAL_BAUD_RATE 115200U
#define COMPUTER_WRITE_TIMEOUT_MS 5000U

// =====================================================
// ESP32 -> Computer packet protocol
// =====================================================
//
// 0..3   magic              "CAM1"
// 4      version            1
// 5      packetType
//        1 = DATA
//        2 = FRAME_END
//        3 = ERROR
//        4 = PERFORMANCE
// 6      status
// 7      reserved
// 8..11  frameId
// 12..15 totalFrameLength
// 16..17 chunkIndex / SPI error phase
// 18..19 chunkCount
// 20..23 payloadLength
// 24..   payload
//
// All multi-byte integers are Big Endian.
// =====================================================

#define COMPUTER_MAGIC_TEXT       "CAM1"
#define COMPUTER_MAGIC_SIZE       4U
#define COMPUTER_PROTOCOL_VERSION 1U
#define COMPUTER_HEADER_SIZE      24U

#define COMPUTER_PACKET_DATA        0x01U
#define COMPUTER_PACKET_FRAME_END   0x02U
#define COMPUTER_PACKET_ERROR       0x03U
#define COMPUTER_PACKET_PERFORMANCE 0x04U

#define COMPUTER_STATUS_OK             0x00U
#define COMPUTER_STATUS_CAMERA_FAILED  0x01U
#define COMPUTER_STATUS_FRAME_INVALID  0x02U
#define COMPUTER_STATUS_SPI_FAILED     0x03U
#define COMPUTER_STATUS_LENGTH_ERROR   0x04U
#define COMPUTER_STATUS_OUTPUT_FAILED  0x05U

#define MAX_JPEG_SIZE (1024U * 1024U)

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
#define SPI_CMD_PROCESS         0x02U

#define SPI_STATUS_NONE         0x00U
#define SPI_STATUS_HEADER_OK    0x10U
#define SPI_STATUS_OK           0x80U
#define SPI_STATUS_BAD_HEADER   0xE1U
#define SPI_STATUS_BAD_LENGTH   0xE2U
#define SPI_STATUS_BAD_COMMAND  0xE3U
#define SPI_STATUS_PROCESS_FAIL 0xE4U

// Start with a stable speed. Raise after checking PERF output.
#define SPI_SPEED_HZ            985000U
#define READY_TIMEOUT_MS        5000U

// =====================================================
// SPI error phases
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

// =====================================================
// Global state
// =====================================================

static uint32_t g_frameId = 0U;
static uint32_t g_nextSequence = 1U;
static uint8_t g_spiErrorPhase = SPI_ERROR_NONE;

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
static uint8_t g_spiDebugHeader[SPI_HEADER_SIZE];

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
// Computer transport
// =====================================================

static void ComputerTransportBegin(void)
{
    COMPUTER_SERIAL.begin(COMPUTER_SERIAL_BAUD_RATE);

    const uint32_t start = millis();

    while (!COMPUTER_SERIAL)
    {
        if ((millis() - start) >= 5000U)
        {
            break;
        }

        delay(10);
    }
}

static bool ComputerWriteAll(const uint8_t *data, size_t length)
{
    if ((data == nullptr) && (length > 0U))
    {
        return false;
    }

    size_t totalWritten = 0U;
    uint32_t lastProgress = millis();

    while (totalWritten < length)
    {
        const size_t remaining = length - totalWritten;
        const size_t blockSize =
            (remaining > 4096U) ? 4096U : remaining;

        const size_t written =
            COMPUTER_SERIAL.write(data + totalWritten, blockSize);

        if (written == 0U)
        {
            if ((millis() - lastProgress) >= COMPUTER_WRITE_TIMEOUT_MS)
            {
                return false;
            }

            delay(1);
            continue;
        }

        totalWritten += written;
        lastProgress = millis();
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

    if (payloadLength > 0U)
    {
        if (payload == nullptr)
        {
            return false;
        }

        if (!ComputerWriteAll(payload, payloadLength))
        {
            return false;
        }
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
    uint16_t errorPhase,
    uint16_t chunkCount,
    uint8_t status,
    const uint8_t *debugPayload,
    uint32_t debugLength)
{
    return ComputerSendPacket(
        COMPUTER_PACKET_ERROR,
        status,
        frameId,
        totalFrameLength,
        errorPhase,
        chunkCount,
        debugPayload,
        debugLength);
}

static bool ComputerSendPerformance(
    uint32_t frameId,
    uint32_t captureUs,
    uint32_t spiUs,
    uint32_t usbUs,
    uint32_t totalUs,
    uint32_t jpegSize)
{
    uint8_t payload[20];

    WriteU32BE(&payload[0], captureUs);
    WriteU32BE(&payload[4], spiUs);
    WriteU32BE(&payload[8], usbUs);
    WriteU32BE(&payload[12], totalUs);
    WriteU32BE(&payload[16], jpegSize);

    return ComputerSendPacket(
        COMPUTER_PACKET_PERFORMANCE,
        COMPUTER_STATUS_OK,
        frameId,
        jpegSize,
        0U,
        0U,
        payload,
        sizeof(payload));
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
    config.jpeg_quality = 30;

    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    config.fb_location =
        psramFound()
            ? CAMERA_FB_IN_PSRAM
            : CAMERA_FB_IN_DRAM;

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
// Five-phase SPI request
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
    requestHeader.reserved = 0U;

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
        (ackHeader.status != SPI_STATUS_HEADER_OK) ||
        (ackHeader.reserved != 0U))
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
        (responseHeader.payloadLength > responseCapacity) ||
        (responseHeader.reserved != 0U))
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
// Process one frame and collect timing
// =====================================================

static bool ProcessAndSendFrame(
    const camera_fb_t *frame,
    uint32_t captureUs,
    uint32_t frameStartUs)
{
    if (!IsFrameBasicValid(frame))
    {
        ComputerSendError(
            g_frameId,
            0U,
            0U,
            0U,
            COMPUTER_STATUS_FRAME_INVALID,
            nullptr,
            0U);

        return false;
    }

    const uint32_t totalLength =
        static_cast<uint32_t>(frame->len);

    const uint32_t chunkCount32 =
        (totalLength + SPI_MAX_PAYLOAD_SIZE - 1U) /
        SPI_MAX_PAYLOAD_SIZE;

    if ((chunkCount32 == 0U) ||
        (chunkCount32 > 0xFFFFU))
    {
        ComputerSendError(
            g_frameId,
            totalLength,
            0U,
            0U,
            COMPUTER_STATUS_LENGTH_ERROR,
            nullptr,
            0U);

        return false;
    }

    const uint16_t chunkCount =
        static_cast<uint16_t>(chunkCount32);

    uint32_t offset = 0U;
    uint32_t spiTotalUs = 0U;
    uint32_t usbTotalUs = 0U;

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

        const uint32_t spiStartUs = micros();

        const bool spiOk = SpiRequest(
            SPI_CMD_PROCESS,
            frame->buf + offset,
            chunkLength,
            responsePayload,
            sizeof(responsePayload),
            responseLength);

        spiTotalUs += micros() - spiStartUs;

        if (!spiOk)
        {
            ComputerSendError(
                g_frameId,
                totalLength,
                g_spiErrorPhase,
                chunkCount,
                COMPUTER_STATUS_SPI_FAILED,
                g_spiDebugHeader,
                SPI_HEADER_SIZE);

            return false;
        }

        if (responseLength != chunkLength)
        {
            ComputerSendError(
                g_frameId,
                totalLength,
                chunkIndex,
                chunkCount,
                COMPUTER_STATUS_LENGTH_ERROR,
                nullptr,
                0U);

            return false;
        }

        const uint32_t usbStartUs = micros();

        const bool outputOk = ComputerSendDataChunk(
            g_frameId,
            totalLength,
            chunkIndex,
            chunkCount,
            responsePayload,
            responseLength);

        usbTotalUs += micros() - usbStartUs;

        if (!outputOk)
        {
            return false;
        }

        offset += chunkLength;
    }

    const uint32_t usbStartUs = micros();

    const bool frameEndOk = ComputerSendFrameEnd(
        g_frameId,
        totalLength,
        chunkCount);

    usbTotalUs += micros() - usbStartUs;

    if (!frameEndOk)
    {
        return false;
    }

    const uint32_t totalUs = micros() - frameStartUs;

    ComputerSendPerformance(
        g_frameId,
        captureUs,
        spiTotalUs,
        usbTotalUs,
        totalUs,
        totalLength);

    return true;
}

// =====================================================
// Arduino
// =====================================================

void setup()
{
    esp_log_level_set("*", ESP_LOG_NONE);

    ComputerTransportBegin();

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
                COMPUTER_STATUS_CAMERA_FAILED,
                nullptr,
                0U);

            delay(1000);
        }
    }

    for (int i = 0; i < 3; i++)
    {
        camera_fb_t *frame = esp_camera_fb_get();

        if (frame != nullptr)
        {
            esp_camera_fb_return(frame);
        }

        delay(100);
    }
}

void loop()
{
    const uint32_t frameStartUs = micros();
    const uint32_t captureStartUs = micros();

    camera_fb_t *frame = esp_camera_fb_get();

    const uint32_t captureUs =
        micros() - captureStartUs;

    if (frame == nullptr)
    {
        ComputerSendError(
            g_frameId,
            0U,
            0U,
            0U,
            COMPUTER_STATUS_CAMERA_FAILED,
            nullptr,
            0U);

        g_frameId++;
        return;
    }

    ProcessAndSendFrame(
        frame,
        captureUs,
        frameStartUs);

    esp_camera_fb_return(frame);
    g_frameId++;
}
