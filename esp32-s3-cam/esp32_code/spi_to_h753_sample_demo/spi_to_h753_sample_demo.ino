#include <Arduino.h>
#include <SPI.h>
#include <string.h>

// =====================================================
// SPI pins
// =====================================================

#define SPI_SCK_PIN       1
#define SPI_MISO_PIN      2
#define SPI_MOSI_PIN      3
#define SPI_CS_PIN        14
#define SPI_READY_PIN     21

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
#define SPI_STATUS_OK           0x80U
#define SPI_STATUS_BAD_HEADER   0xE1U
#define SPI_STATUS_BAD_LENGTH   0xE2U
#define SPI_STATUS_BAD_COMMAND  0xE3U
#define SPI_STATUS_PROCESS_FAIL 0xE4U
#define SPI_STATUS_HEADER_OK    0x10U

#define SPI_SPEED_HZ            800000U
#define READY_TIMEOUT_MS        5000U

unsigned long long pass_count = 0, fail_count = 0;

bool printSerial = false;

#define DEBUG_PRINT(...)       \
    do                         \
    {                          \
        if (printSerial)       \
        {                      \
            Serial0.print(__VA_ARGS__); \
        }                      \
    } while (0)

#define DEBUG_PRINTLN(...)     \
    do                         \
    {                          \
        if (printSerial)       \
        {                      \
            Serial0.println(__VA_ARGS__); \
        }                      \
    } while (0)

#define DEBUG_PRINTF(...)      \
    do                         \
    {                          \
        if (printSerial)       \
        {                      \
            Serial0.printf(__VA_ARGS__); \
        }                      \
    } while (0)

#define DEBUG_WRITE(...)                  \
    do                                    \
    {                                     \
        if (printSerial)                  \
        {                                 \
            Serial0.write(__VA_ARGS__);   \
        }                                 \
    } while (0)


// =====================================================
// SPI object and buffers
// =====================================================

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

static uint8_t requestPayload[SPI_MAX_PAYLOAD_SIZE];
static uint8_t responsePayload[SPI_MAX_PAYLOAD_SIZE];
static uint8_t dummyBuffer[SPI_MAX_PAYLOAD_SIZE];

static uint32_t nextSequence = 1U;

// =====================================================
// Big-endian functions
// =====================================================

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
// Header encode/decode
// =====================================================

static void EncodeHeader(
    uint8_t *buffer,
    const SpiHeader &header)
{
    WriteU32BE(&buffer[0], header.magic);
    WriteU32BE(&buffer[4], header.sequence);
    WriteU32BE(&buffer[8], header.payloadLength);

    buffer[12] = header.command;
    buffer[13] = header.status;
    buffer[14] = header.flags;
    buffer[15] = header.reserved;
}

static void DecodeHeader(
    const uint8_t *buffer,
    SpiHeader &header)
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

static bool WaitReadyLevel(
    int expectedLevel,
    uint32_t timeoutMs)
{
    uint32_t startTime = millis();

    while (digitalRead(SPI_READY_PIN) != expectedLevel)
    {
        if ((millis() - startTime) >= timeoutMs)
        {
            return false;
        }

        delayMicroseconds(20);
    }

    return true;
}

/*
 * Wait for a new READY phase.
 *
 * The current phase must first finish:
 * READY HIGH → READY LOW
 *
 * Then H753 prepares the next phase:
 * READY LOW → READY HIGH
 */


static bool WaitNextReady(void)
{
    if (!WaitReadyLevel(LOW, READY_TIMEOUT_MS))
    {
        DEBUG_PRINTLN("Timeout waiting READY LOW");
        return false;
    }

    if (!WaitReadyLevel(HIGH, READY_TIMEOUT_MS))
    {
        DEBUG_PRINTLN("Timeout waiting READY HIGH");
        return false;
    }

    return true;
}

// =====================================================
// SPI transaction
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
            SPI_MODE0
        )
    );

    /*
     * 讓 H753 有時間真正進入 HAL_SPI_TransmitReceive()
     */
    delayMicroseconds(20);

    digitalWrite(SPI_CS_PIN, LOW);
    delayMicroseconds(10);

    h753SPI.transferBytes(
        const_cast<uint8_t *>(txBuffer),
        rxBuffer,
        length
    );

    delayMicroseconds(10);
    digitalWrite(SPI_CS_PIN, HIGH);

    h753SPI.endTransaction();
}

// =====================================================
// Status display
// =====================================================

static const char *StatusToString(uint8_t status)
{
    switch (status)
    {
        case SPI_STATUS_HEADER_OK:
            return "HEADER_OK";

        case SPI_STATUS_OK:
            return "OK";

        case SPI_STATUS_BAD_HEADER:
            return "BAD_HEADER";

        case SPI_STATUS_BAD_LENGTH:
            return "BAD_LENGTH";

        case SPI_STATUS_BAD_COMMAND:
            return "BAD_COMMAND";

        case SPI_STATUS_PROCESS_FAIL:
            return "PROCESS_FAIL";

        default:
            return "UNKNOWN_STATUS";
    }
}

// =====================================================
// Send request and receive response
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
        DEBUG_PRINTLN("Request payload is too large");
        return false;
    }

    if ((requestLength > 0U) && (requestData == nullptr))
    {
        DEBUG_PRINTLN("Request payload pointer is null");
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

    memset(
        headerRx,
        0,
        sizeof(headerRx));

    // =================================================
    // Phase 1: Send Request Header
    // =================================================

    if (!WaitReadyLevel(HIGH, READY_TIMEOUT_MS))
    {
        DEBUG_PRINTLN(
            "Timeout waiting request-header READY");

        return false;
    }

    SpiTransfer(
        headerTx,
        headerRx,
        SPI_HEADER_SIZE);

    // =================================================
    // Phase 2: Receive Header ACK
    // =================================================

    if (!WaitNextReady())
    {
        DEBUG_PRINTLN(
            "Timeout waiting header-ACK READY");

        return false;
    }

    memset(
        headerTx,
        0,
        sizeof(headerTx));

    memset(
        headerRx,
        0,
        sizeof(headerRx));

    SpiTransfer(
        headerTx,
        headerRx,
        SPI_HEADER_SIZE);

    SpiHeader ackHeader = {};

    DecodeHeader(
        headerRx,
        ackHeader);

    DEBUG_PRINTF(
        "Header ACK: magic=%08lX sequence=%lu "
        "command=%02X status=%02X flags=%02X length=%lu\n",
        static_cast<unsigned long>(ackHeader.magic),
        static_cast<unsigned long>(ackHeader.sequence),
        ackHeader.command,
        ackHeader.status,
        ackHeader.flags,
        static_cast<unsigned long>(ackHeader.payloadLength));

    if (ackHeader.magic != SPI_MAGIC)
    {
        DEBUG_PRINTLN("Invalid ACK magic");

        WaitReadyLevel(
            LOW,
            READY_TIMEOUT_MS);

        return false;
    }

    if (ackHeader.sequence != sequence)
    {
        DEBUG_PRINTLN("ACK sequence mismatch");

        WaitReadyLevel(
            LOW,
            READY_TIMEOUT_MS);

        return false;
    }

    if (ackHeader.command != command)
    {
        DEBUG_PRINTLN("ACK command mismatch");

        WaitReadyLevel(
            LOW,
            READY_TIMEOUT_MS);

        return false;
    }

    if (ackHeader.flags != SPI_FLAG_RESPONSE)
    {
        DEBUG_PRINTLN("Invalid ACK flags");

        WaitReadyLevel(
            LOW,
            READY_TIMEOUT_MS);

        return false;
    }

    if (ackHeader.payloadLength != 0U)
    {
        DEBUG_PRINTLN("ACK payload length is not zero");

        WaitReadyLevel(
            LOW,
            READY_TIMEOUT_MS);

        return false;
    }

    /*
     * Header 不合法時，H753 不會接收 Request Payload。
     */
    if (ackHeader.status != SPI_STATUS_HEADER_OK)
    {
        DEBUG_PRINTF(
            "Header rejected by H753: %s (0x%02X)\n",
            StatusToString(ackHeader.status),
            ackHeader.status);

        WaitReadyLevel(
            LOW,
            READY_TIMEOUT_MS);

        return false;
    }

    // =================================================
    // Phase 3: Send Request Payload
    // =================================================

    if (requestLength > 0U)
    {
        if (!WaitNextReady())
        {
            DEBUG_PRINTLN(
                "Timeout waiting request-payload READY");

            return false;
        }

        memset(
            dummyBuffer,
            0,
            requestLength);

        SpiTransfer(
            requestData,
            dummyBuffer,
            requestLength);
    }

    // =================================================
    // Phase 4: Receive Final Response Header
    // =================================================

    if (!WaitNextReady())
    {
        DEBUG_PRINTLN(
            "Timeout waiting final-response-header READY");

        return false;
    }

    memset(
        headerTx,
        0,
        sizeof(headerTx));

    memset(
        headerRx,
        0,
        sizeof(headerRx));

    SpiTransfer(
        headerTx,
        headerRx,
        SPI_HEADER_SIZE);

    SpiHeader responseHeader = {};

    DecodeHeader(
        headerRx,
        responseHeader);

    DEBUG_PRINTF(
        "Final response: magic=%08lX sequence=%lu "
        "command=%02X status=%02X flags=%02X length=%lu\n",
        static_cast<unsigned long>(responseHeader.magic),
        static_cast<unsigned long>(responseHeader.sequence),
        responseHeader.command,
        responseHeader.status,
        responseHeader.flags,
        static_cast<unsigned long>(responseHeader.payloadLength));

    if (responseHeader.magic != SPI_MAGIC)
    {
        DEBUG_PRINTLN("Invalid final response magic");
        return false;
    }

    if (responseHeader.sequence != sequence)
    {
        DEBUG_PRINTLN("Final response sequence mismatch");
        return false;
    }

    if (responseHeader.command != command)
    {
        DEBUG_PRINTLN("Final response command mismatch");
        return false;
    }

    if (responseHeader.flags != SPI_FLAG_RESPONSE)
    {
        DEBUG_PRINTLN("Invalid final response flags");
        return false;
    }

    if (responseHeader.payloadLength > SPI_MAX_PAYLOAD_SIZE)
    {
        DEBUG_PRINTLN("Final response payload too large");
        return false;
    }

    if (responseHeader.payloadLength > responseCapacity)
    {
        DEBUG_PRINTLN("Response buffer is too small");
        return false;
    }

    // =================================================
    // Phase 5: Receive Final Response Payload
    // =================================================

    if (responseHeader.payloadLength > 0U)
    {
        if (responseData == nullptr)
        {
            DEBUG_PRINTLN("Response pointer is null");
            return false;
        }

        if (!WaitNextReady())
        {
            DEBUG_PRINTLN(
                "Timeout waiting response-payload READY");

            return false;
        }

        memset(
            dummyBuffer,
            0,
            responseHeader.payloadLength);

        SpiTransfer(
            dummyBuffer,
            responseData,
            responseHeader.payloadLength);
    }

    responseLength =
        responseHeader.payloadLength;

    if (!WaitReadyLevel(
            LOW,
            READY_TIMEOUT_MS))
    {
        DEBUG_PRINTLN(
            "Warning: READY did not return LOW");
    }

    if (responseHeader.status != SPI_STATUS_OK)
    {
        DEBUG_PRINTF(
            "H753 processing error: %s (0x%02X)\n",
            StatusToString(responseHeader.status),
            responseHeader.status);

        return false;
    }

    return true;
}

// =====================================================
// Tests
// =====================================================

static void TestPing(void)
{
    uint32_t responseLength = 0U;

    bool result = SpiRequest(
        SPI_CMD_PING,
        nullptr,
        0U,
        responsePayload,
        sizeof(responsePayload),
        responseLength);

    if (!result)
    {
        DEBUG_PRINTLN("PING failed");
        return;
    }

    DEBUG_PRINT("PING response: ");

    for (uint32_t i = 0; i < responseLength; i++)
    {
        DEBUG_WRITE(responsePayload[i]);
    }

    DEBUG_PRINTLN();
}

static void TestProcess(void)
{
    constexpr uint32_t testLength = 4096U;

    for (uint32_t i = 0; i < testLength; i++)
    {
        requestPayload[i] =
            static_cast<uint8_t>(i & 0xFFU);
    }

    uint32_t responseLength = 0U;
    uint32_t startTime = micros();

    bool result = SpiRequest(
        SPI_CMD_PROCESS,
        requestPayload,
        testLength,
        responsePayload,
        sizeof(responsePayload),
        responseLength
    );

    uint32_t elapsedUs = micros() - startTime;

    if (!result)
    {
        fail_count++;

        DEBUG_PRINTLN("PROCESS failed");
        return;
    }

    if (responseLength != testLength)
    {
        fail_count++;

        DEBUG_PRINTF(
            "Length mismatch: request=%lu response=%lu\n",
            static_cast<unsigned long>(testLength),
            static_cast<unsigned long>(responseLength)
        );

        return;
    }

    for (uint32_t i = 0; i < testLength; i++)
    {
        uint8_t expected =
            static_cast<uint8_t>(
                requestPayload[i] ^ 0xA5U
            );

        if (responsePayload[i] != expected)
        {
            fail_count++;

            DEBUG_PRINTF(
                "Mismatch at %lu: expected=%02X actual=%02X\n",
                static_cast<unsigned long>(i),
                expected,
                responsePayload[i]
            );

            DEBUG_PRINTLN("PROCESS DATA FAIL");
            return;
        }
    }

    pass_count++;

    DEBUG_PRINTF(
        "PROCESS PASS, size=%lu, total=%lu us\n",
        static_cast<unsigned long>(responseLength),
        static_cast<unsigned long>(elapsedUs)
    );
}

static void PrintStatistics(void)
{
    unsigned long long total =
        pass_count + fail_count;

    if (total == 0ULL)
    {
        return;
    }

    if ((total % 100ULL) != 0ULL)
    {
        return;
    }

    double passRate =
        static_cast<double>(pass_count) * 100.0 /
        static_cast<double>(total);

    Serial0.printf(
        "Statistics: PASS=%llu FAIL=%llu "
        "TOTAL=%llu RATE=%.2f%%\n",
        pass_count,
        fail_count,
        total,
        passRate
    );
}

// =====================================================
// Arduino setup/loop
// =====================================================

void setup()
{
    Serial0.begin(115200);
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

    DEBUG_PRINTLN();
    DEBUG_PRINTLN("ESP32-S3 SPI Master started");
}

void loop()
{
    TestPing();

    TestProcess();

    PrintStatistics();
}