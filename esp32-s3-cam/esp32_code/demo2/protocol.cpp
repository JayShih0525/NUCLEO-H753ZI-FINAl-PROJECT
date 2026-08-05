#include "protocol.h"
#include "transport.h"
#include "byte_utils.h"

#include <string.h>

static uint8_t g_controlPayloadBuf[CONTROL_MAX_PAYLOAD];

bool Protocol_Init(void)
{
    return Transport_Init();
}

static bool SendPacket(
    uint8_t packetType,
    uint8_t status,
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkIndex,
    uint16_t chunkCount,
    const uint8_t *payload,
    uint32_t payloadLength)
{
    uint8_t header[COMPUTER_HEADER_SIZE] = {0};

    memcpy(&header[0], COMPUTER_MAGIC_TEXT, COMPUTER_MAGIC_SIZE);

    header[4] = COMPUTER_PROTOCOL_VERSION;
    header[5] = packetType;
    header[6] = status;
    header[7] = 0U;

    WriteU32BE(&header[8], frameId);
    WriteU32BE(&header[12], totalFrameLength);
    WriteU16BE(&header[16], chunkIndex);
    WriteU16BE(&header[18], chunkCount);
    WriteU32BE(&header[20], payloadLength);

    if (!Transport_Write(header, sizeof(header)))
    {
        return false;
    }

    if (payloadLength > 0U)
    {
        if (payload == nullptr)
        {
            return false;
        }

        if (!Transport_Write(payload, payloadLength))
        {
            return false;
        }
    }

    return true;
}

bool Protocol_SendDataChunk(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkIndex,
    uint16_t chunkCount,
    const uint8_t *data,
    uint32_t length)
{
    return SendPacket(
        COMPUTER_PACKET_DATA,
        COMPUTER_STATUS_OK,
        frameId,
        totalFrameLength,
        chunkIndex,
        chunkCount,
        data,
        length);
}

bool Protocol_SendFrameEnd(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t chunkCount)
{
    return SendPacket(
        COMPUTER_PACKET_FRAME_END,
        COMPUTER_STATUS_OK,
        frameId,
        totalFrameLength,
        chunkCount,
        chunkCount,
        nullptr,
        0U);
}

bool Protocol_SendError(
    uint32_t frameId,
    uint32_t totalFrameLength,
    uint16_t errorPhase,
    uint16_t chunkCount,
    uint8_t status,
    const uint8_t *debugPayload,
    uint32_t debugLength)
{
    return SendPacket(
        COMPUTER_PACKET_ERROR,
        status,
        frameId,
        totalFrameLength,
        errorPhase,
        chunkCount,
        debugPayload,
        debugLength);
}

bool Protocol_SendPerformance(
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

    return SendPacket(
        COMPUTER_PACKET_PERFORMANCE,
        COMPUTER_STATUS_OK,
        frameId,
        jpegSize,
        0U,
        0U,
        payload,
        sizeof(payload));
}

bool Protocol_SendControlResponse(
    uint32_t requestId,
    uint8_t status,
    const uint8_t *payload,
    uint32_t payloadLength)
{
    return SendPacket(
        COMPUTER_PACKET_CONTROL_RESPONSE,
        status,
        requestId,
        0U,
        0U,
        0U,
        payload,
        payloadLength);
}

bool Protocol_PollControlRequest(ControlRequest &out)
{
    // Cheap non-blocking check first - most loop() iterations
    // there is nothing waiting, and we must not stall image
    // streaming waiting for a control packet that isn't coming.
    if (Transport_Available() <= 0)
    {
        return false;
    }

    uint8_t header[COMPUTER_HEADER_SIZE];

    // Something arrived - a control request is small and sent
    // as one burst by the PC, so a short bounded wait for the
    // rest of the header is fine here.
    const size_t headerGot =
        Transport_Read(header, sizeof(header), 200U);

    if (headerGot != sizeof(header))
    {
        return false;
    }

    if (memcmp(header, COMPUTER_MAGIC_TEXT, COMPUTER_MAGIC_SIZE) != 0)
    {
        return false;
    }

    if (header[5] != COMPUTER_PACKET_CONTROL_REQUEST)
    {
        // Not for us right now (e.g. stray bytes). Drop it -
        // the PC is expected to retry.
        return false;
    }

    const uint32_t payloadLength = ReadU32BE(&header[20]);

    if (payloadLength > CONTROL_MAX_PAYLOAD)
    {
        return false;
    }

    if (payloadLength > 0U)
    {
        const size_t payloadGot = Transport_Read(
            g_controlPayloadBuf, payloadLength, 500U);

        if (payloadGot != payloadLength)
        {
            return false;
        }
    }

    out.requestId = ReadU32BE(&header[8]);
    out.spiCommand = header[6];
    out.payloadLength = payloadLength;
    out.payload = g_controlPayloadBuf;

    return true;
}
