#include "transport.h"

// =====================================================
// UART (native USB CDC) implementation of transport.h
// =====================================================
// This is the ONLY file in the project allowed to touch
// `Serial` directly. When UART is replaced by WiFi, this
// whole file gets swapped for transport_wifi.cpp (same
// four function signatures, backed by WiFiClient/TCP
// instead) - nothing else in the project changes.
// =====================================================

#define TRANSPORT_SERIAL           Serial
// #define TRANSPORT_BAUD_RATE        115200U
#define TRANSPORT_BAUD_RATE        1000000U
#define TRANSPORT_WRITE_TIMEOUT_MS 5000U

bool Transport_Init(void)
{
    // Native USB CDC's default RX ring buffer is far smaller
    // than our largest single write (e.g. a KEM_DECAPSULATE
    // control request: 24-byte header + ~1088-byte payload).
    // Without this, bytes arriving faster than loop() drains
    // them get silently dropped - confirmed empirically: a
    // 1112-byte write only produced 496 bytes on the device
    // side before this fix. Must be called before begin().
    TRANSPORT_SERIAL.setRxBufferSize(4096);

    TRANSPORT_SERIAL.begin(TRANSPORT_BAUD_RATE);

    const uint32_t start = millis();

    while (!TRANSPORT_SERIAL)
    {
        if ((millis() - start) >= 5000U)
        {
            break;
        }

        delay(10);
    }

    return true;
}

bool Transport_Write(const uint8_t *data, size_t length)
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
            TRANSPORT_SERIAL.write(data + totalWritten, blockSize);

        if (written == 0U)
        {
            if ((millis() - lastProgress) >= TRANSPORT_WRITE_TIMEOUT_MS)
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

int Transport_Available(void)
{
    return TRANSPORT_SERIAL.available();
}

size_t Transport_Read(uint8_t *buffer, size_t maxLength, uint32_t timeoutMs)
{
    if ((buffer == nullptr) || (maxLength == 0U))
    {
        return 0U;
    }

    size_t totalRead = 0U;
    uint32_t lastProgress = millis();

    while (totalRead < maxLength)
    {
        if (TRANSPORT_SERIAL.available() > 0)
        {
            buffer[totalRead] = static_cast<uint8_t>(TRANSPORT_SERIAL.read());
            totalRead++;
            lastProgress = millis();
        }
        else if ((millis() - lastProgress) >= timeoutMs)
        {
            // No new byte for `timeoutMs` - genuinely stalled,
            // not just a normal gap between USB CDC bursts.
            break;
        }
        else
        {
            delay(1);
        }
    }

    return totalRead;
}