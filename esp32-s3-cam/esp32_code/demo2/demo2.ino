#include <Arduino.h>
#include "esp_log.h"

#include "camera.h"
#include "spi_link.h"
#include "protocol.h"

static uint8_t g_responsePayload[SPI_MAX_PAYLOAD_SIZE];
static uint32_t g_frameId = 0U;

// Only start streaming camera frames once the KEM handshake
// has actually completed. Before that, PROCESS would just
// fail (no AES key on the STM32 side yet) and flood the USB
// link with ERROR packets while the PC is trying to do the
// handshake on the same channel - that contention is what
// was causing the control responses to get corrupted.
static bool g_streamReady = false;

// =====================================================
// Control channel: relay PC <-> STM32, opaque payloads
// =====================================================
// Called once per loop() iteration. Cheap when nothing is
// waiting (single Transport_Available() check inside
// Protocol_PollControlRequest), so it never meaningfully
// slows down image streaming.
// =====================================================

static void HandleControlIfAny(void)
{
    ControlRequest request;

    if (!Protocol_PollControlRequest(request))
    {
        return;
    }

    uint32_t responseLength = 0U;

    const bool spiOk = SpiLink_Request(
        request.spiCommand,
        SPI_CHUNK_FLAGS_SINGLE,
        request.payload,
        request.payloadLength,
        g_responsePayload,
        sizeof(g_responsePayload),
        responseLength);

    if (!spiOk)
    {
        Protocol_SendControlResponse(
            request.requestId,
            COMPUTER_STATUS_SPI_FAILED,
            g_spiDebugHeader,
            SPI_HEADER_SIZE);

        return;
    }

    if (request.spiCommand == SPI_CMD_KEM_DECAPSULATE)
    {
        g_streamReady = true;
    }

    Protocol_SendControlResponse(
        request.requestId,
        COMPUTER_STATUS_OK,
        g_responsePayload,
        responseLength);
}

// =====================================================
// Process one camera frame and send it to the computer
// =====================================================

static bool ProcessAndSendFrame(
    const camera_fb_t *frame,
    uint32_t captureUs,
    uint32_t frameStartUs)
{
    if (!Camera_IsFrameValid(frame))
    {
        Protocol_SendError(
            g_frameId, 0U, 0U, 0U,
            COMPUTER_STATUS_FRAME_INVALID,
            nullptr, 0U);

        return false;
    }

    const uint32_t totalLength = static_cast<uint32_t>(frame->len);

    const uint32_t chunkCount32 =
        (totalLength + SPI_MAX_PAYLOAD_SIZE - 1U) /
        SPI_MAX_PAYLOAD_SIZE;

    if ((chunkCount32 == 0U) || (chunkCount32 > 0xFFFFU))
    {
        Protocol_SendError(
            g_frameId, totalLength, 0U, 0U,
            COMPUTER_STATUS_LENGTH_ERROR,
            nullptr, 0U);

        return false;
    }

    const uint16_t chunkCount = static_cast<uint16_t>(chunkCount32);

    uint32_t offset = 0U;
    uint32_t spiTotalUs = 0U;
    uint32_t usbTotalUs = 0U;

    for (uint16_t chunkIndex = 0U; chunkIndex < chunkCount; chunkIndex++)
    {
        const uint32_t remaining = totalLength - offset;

        const uint32_t chunkLength =
            (remaining > SPI_MAX_PAYLOAD_SIZE)
                ? SPI_MAX_PAYLOAD_SIZE
                : remaining;

        // STM32 uses these to know when to start a fresh
        // AES-GCM stream (first chunk) and when to finalize
        // + append nonce/tag to the response (last chunk).
        uint8_t chunkFlags = 0U;
        if (chunkIndex == 0U) chunkFlags |= SPI_CHUNK_FLAG_FIRST;
        if (chunkIndex == (chunkCount - 1U)) chunkFlags |= SPI_CHUNK_FLAG_LAST;

        uint32_t responseLength = 0U;

        const uint32_t spiStartUs = micros();

        const bool spiOk = SpiLink_Request(
            SPI_CMD_PROCESS,
            chunkFlags,
            frame->buf + offset,
            chunkLength,
            g_responsePayload,
            sizeof(g_responsePayload),
            responseLength);

        spiTotalUs += micros() - spiStartUs;

        if (!spiOk)
        {
            Protocol_SendError(
                g_frameId, totalLength, g_spiErrorPhase, chunkCount,
                COMPUTER_STATUS_SPI_FAILED,
                g_spiDebugHeader, SPI_HEADER_SIZE);

            return false;
        }

        // Note: on the LAST chunk, responseLength will be
        // larger than chunkLength (ciphertext + nonce + tag
        // appended) - that is expected, not an error. Only
        // check length equality on non-final chunks.
        if (!(chunkFlags & SPI_CHUNK_FLAG_LAST) &&
            (responseLength != chunkLength))
        {
            Protocol_SendError(
                g_frameId, totalLength, chunkIndex, chunkCount,
                COMPUTER_STATUS_LENGTH_ERROR,
                nullptr, 0U);

            return false;
        }

        const uint32_t usbStartUs = micros();

        const bool outputOk = Protocol_SendDataChunk(
            g_frameId, totalLength, chunkIndex, chunkCount,
            g_responsePayload, responseLength);

        usbTotalUs += micros() - usbStartUs;

        if (!outputOk)
        {
            return false;
        }

        offset += chunkLength;
    }

    const uint32_t usbStartUs = micros();
    const bool frameEndOk =
        Protocol_SendFrameEnd(g_frameId, totalLength, chunkCount);
    usbTotalUs += micros() - usbStartUs;

    if (!frameEndOk)
    {
        return false;
    }

    const uint32_t totalUs = micros() - frameStartUs;

    Protocol_SendPerformance(
        g_frameId, captureUs, spiTotalUs, usbTotalUs,
        totalUs, totalLength);

    return true;
}

// =====================================================
// Arduino
// =====================================================

void setup()
{
    esp_log_level_set("*", ESP_LOG_NONE);

    Protocol_Init();
    SpiLink_Init();

    if (!Camera_Init())
    {
        while (true)
        {
            Protocol_SendError(
                g_frameId, 0U, 0U, 0U,
                COMPUTER_STATUS_CAMERA_FAILED,
                nullptr, 0U);

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
    // A pending KEM handshake request from the PC always
    // takes priority over pushing the next camera frame.
    HandleControlIfAny();

    if (!g_streamReady)
    {
        // No AES key yet - sending frames now would just
        // flood the link with ERROR packets and fight with
        // the handshake for USB bandwidth. Wait.
        return;
    }

    const uint32_t frameStartUs = micros();
    const uint32_t captureStartUs = micros();

    camera_fb_t *frame = esp_camera_fb_get();

    const uint32_t captureUs = micros() - captureStartUs;

    if (frame == nullptr)
    {
        Protocol_SendError(
            g_frameId, 0U, 0U, 0U,
            COMPUTER_STATUS_CAMERA_FAILED,
            nullptr, 0U);

        g_frameId++;
        return;
    }

    ProcessAndSendFrame(frame, captureUs, frameStartUs);

    esp_camera_fb_return(frame);
    g_frameId++;
}
