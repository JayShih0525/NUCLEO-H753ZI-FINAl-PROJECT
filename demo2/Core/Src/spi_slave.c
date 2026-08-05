#include "spi_slave.h"
#include "crypto_app.h"

#include <string.h>

extern SPI_HandleTypeDef hspi1;

// =====================================================
// Buffers - all in SRAM
// =====================================================

static uint8_t requestHeaderRx[SPI_HEADER_SIZE];
static uint8_t requestHeaderDummyTx[SPI_HEADER_SIZE];

static uint8_t responseHeaderTx[SPI_HEADER_SIZE];
static uint8_t responseHeaderDummyRx[SPI_HEADER_SIZE];

static uint8_t requestPayload[SPI_MAX_PAYLOAD_SIZE];
static uint8_t responsePayload[SPI_MAX_PAYLOAD_SIZE];

static uint8_t payloadDummyRx[SPI_MAX_PAYLOAD_SIZE];

// =====================================================
// DWT delay (needed for the READY-low settle time)
// =====================================================

static void DWT_DelayInit(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void DelayUs(uint32_t microseconds)
{
    const uint32_t cycles = (SystemCoreClock / 1000000U) * microseconds;
    const uint32_t start = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
        __NOP();
    }
}

// =====================================================
// READY
// =====================================================

static void SPI_ReadyHigh(void)
{
    HAL_GPIO_WritePin(SPI_READY_GPIO_Port, SPI_READY_Pin, GPIO_PIN_SET);
}

static void SPI_ReadyLow(void)
{
    HAL_GPIO_WritePin(SPI_READY_GPIO_Port, SPI_READY_Pin, GPIO_PIN_RESET);
}

// =====================================================
// Endian helpers
// =====================================================

static void WriteU32BE(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)((value >> 24U) & 0xFFU);
    destination[1] = (uint8_t)((value >> 16U) & 0xFFU);
    destination[2] = (uint8_t)((value >> 8U) & 0xFFU);
    destination[3] = (uint8_t)(value & 0xFFU);
}

static uint32_t ReadU32BE(const uint8_t *source)
{
    return
        ((uint32_t)source[0] << 24U) |
        ((uint32_t)source[1] << 16U) |
        ((uint32_t)source[2] << 8U) |
        (uint32_t)source[3];
}

static void EncodeHeader(uint8_t *buffer, const SpiHeader *header)
{
    WriteU32BE(&buffer[0], header->magic);
    WriteU32BE(&buffer[4], header->sequence);
    WriteU32BE(&buffer[8], header->payloadLength);

    buffer[12] = header->command;
    buffer[13] = header->status;
    buffer[14] = header->flags;
    buffer[15] = header->reserved;
}

static void DecodeHeader(const uint8_t *buffer, SpiHeader *header)
{
    header->magic = ReadU32BE(&buffer[0]);
    header->sequence = ReadU32BE(&buffer[4]);
    header->payloadLength = ReadU32BE(&buffer[8]);

    header->command = buffer[12];
    header->status = buffer[13];
    header->flags = buffer[14];
    header->reserved = buffer[15];
}

// =====================================================
// SPI transfer
// =====================================================

static void SPI_Recover(void)
{
    SPI_ReadyLow();
    HAL_SPI_Abort(&hspi1);
    HAL_Delay(1);
}

static HAL_StatusTypeDef SPI_SlaveTransferPhase(
    const uint8_t *txBuffer,
    uint8_t *rxBuffer,
    uint16_t length)
{
    if ((txBuffer == NULL) || (rxBuffer == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    SPI_ReadyHigh();

    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
        &hspi1, (uint8_t *)txBuffer, rxBuffer, length, SPI_TIMEOUT_MS);

    SPI_ReadyLow();
    DelayUs(SPI_READY_LOW_US);

    return status;
}

// =====================================================
// Validation
// =====================================================
// NOTE: `reserved` used to be required to be 0. It now
// carries chunk flags (FIRST/LAST) for every command, so
// that check was removed - any value 0..3 is valid.
// =====================================================

static uint8_t ValidateRequestHeader(const SpiHeader *header)
{
    if (header == NULL)
    {
        return SPI_STATUS_BAD_HEADER;
    }

    if (header->magic != SPI_MAGIC)
    {
        return SPI_STATUS_BAD_HEADER;
    }

    if (header->flags != SPI_FLAG_REQUEST)
    {
        return SPI_STATUS_BAD_HEADER;
    }

    if (header->status != SPI_STATUS_NONE)
    {
        return SPI_STATUS_BAD_HEADER;
    }

    if (header->payloadLength > SPI_MAX_PAYLOAD_SIZE)
    {
        return SPI_STATUS_BAD_LENGTH;
    }

    switch (header->command)
    {
        case SPI_CMD_PING:
            return (header->payloadLength == 0U)
                ? SPI_STATUS_OK : SPI_STATUS_BAD_LENGTH;

        case SPI_CMD_PROCESS:
            return (header->payloadLength > 0U)
                ? SPI_STATUS_OK : SPI_STATUS_BAD_LENGTH;

        case SPI_CMD_KEM_GET_PUBKEY:
            return (header->payloadLength == 0U)
                ? SPI_STATUS_OK : SPI_STATUS_BAD_LENGTH;

        case SPI_CMD_KEM_DECAPSULATE:
            return (header->payloadLength > 0U)
                ? SPI_STATUS_OK : SPI_STATUS_BAD_LENGTH;

        default:
            return SPI_STATUS_BAD_COMMAND;
    }
}

// =====================================================
// Dispatch - the only place that knows which SPI command
// maps to which crypto_app.h call.
// =====================================================

static uint8_t ProcessRequest(
    const SpiHeader *requestHeader,
    SpiHeader *responseHeader)
{
    uint32_t outLength = 0U;
    uint8_t status = SPI_STATUS_PROCESS_FAIL;

    responseHeader->magic = SPI_MAGIC;
    responseHeader->sequence = requestHeader->sequence;
    responseHeader->payloadLength = 0U;
    responseHeader->command = requestHeader->command;
    responseHeader->status = SPI_STATUS_OK;
    responseHeader->flags = SPI_FLAG_RESPONSE;
    responseHeader->reserved = 0U;

    switch (requestHeader->command)
    {
        case SPI_CMD_PING:
        {
            static const uint8_t pong[] = { 'P', 'O', 'N', 'G' };
            memcpy(responsePayload, pong, sizeof(pong));
            responseHeader->payloadLength = (uint32_t)sizeof(pong);
            return SPI_STATUS_OK;
        }

        case SPI_CMD_PROCESS:
            status = CryptoApp_EncryptChunk(
                requestHeader->reserved,
                requestPayload, requestHeader->payloadLength,
                responsePayload, sizeof(responsePayload), &outLength);

            responseHeader->payloadLength = outLength;
            return status;

        case SPI_CMD_KEM_GET_PUBKEY:
            status = CryptoApp_KemGetPublicKey(
                responsePayload, sizeof(responsePayload), &outLength);

            responseHeader->payloadLength = outLength;
            return status;

        case SPI_CMD_KEM_DECAPSULATE:
            status = CryptoApp_KemDecapsulate(
                requestPayload, requestHeader->payloadLength);

            responseHeader->payloadLength = 0U;
            return status;

        default:
            responseHeader->payloadLength = 0U;
            return SPI_STATUS_BAD_COMMAND;
    }
}

static HAL_StatusTypeDef SPI_SendHeader(const SpiHeader *header)
{
    EncodeHeader(responseHeaderTx, header);

    return SPI_SlaveTransferPhase(
        responseHeaderTx, responseHeaderDummyRx, SPI_HEADER_SIZE);
}

static HAL_StatusTypeDef SendResponse(const SpiHeader *responseHeader)
{
    HAL_StatusTypeDef status = SPI_SendHeader(responseHeader);

    if (status != HAL_OK)
    {
        return status;
    }

    if (responseHeader->payloadLength > 0U)
    {
        status = SPI_SlaveTransferPhase(
            responsePayload, payloadDummyRx,
            (uint16_t)responseHeader->payloadLength);

        if (status != HAL_OK)
        {
            return status;
        }
    }

    return HAL_OK;
}

// =====================================================
// Handle one complete request
// =====================================================

void SPI_HandleOneRequest(void)
{
    HAL_StatusTypeDef status;

    SpiHeader requestHeader = {0};
    SpiHeader ackHeader = {0};
    SpiHeader responseHeader = {0};

    // Phase 1: Request Header
    status = SPI_SlaveTransferPhase(
        requestHeaderDummyTx, requestHeaderRx, SPI_HEADER_SIZE);

    if (status != HAL_OK)
    {
        SPI_Recover();
        return;
    }

    DecodeHeader(requestHeaderRx, &requestHeader);

    const uint8_t validationStatus = ValidateRequestHeader(&requestHeader);

    // Phase 2: Header ACK
    ackHeader.magic = SPI_MAGIC;
    ackHeader.sequence = requestHeader.sequence;
    ackHeader.payloadLength = 0U;
    ackHeader.command = requestHeader.command;
    ackHeader.status = (validationStatus == SPI_STATUS_OK)
        ? SPI_STATUS_HEADER_OK : validationStatus;
    ackHeader.flags = SPI_FLAG_RESPONSE;
    ackHeader.reserved = 0U;

    status = SPI_SendHeader(&ackHeader);

    if (status != HAL_OK)
    {
        SPI_Recover();
        return;
    }

    if (validationStatus != SPI_STATUS_OK)
    {
        return;
    }

    // Phase 3: Request Payload
    if (requestHeader.payloadLength > 0U)
    {
        status = SPI_SlaveTransferPhase(
            payloadDummyRx, requestPayload,
            (uint16_t)requestHeader.payloadLength);

        if (status != HAL_OK)
        {
            SPI_Recover();
            return;
        }
    }

    // Phase 4/5: Process + send final header/payload
    const uint8_t processStatus = ProcessRequest(&requestHeader, &responseHeader);
    responseHeader.status = processStatus;

    status = SendResponse(&responseHeader);

    if (status != HAL_OK)
    {
        SPI_Recover();
        return;
    }
}

void SPI_SlaveInit(void)
{
    DWT_DelayInit();
    SPI_ReadyLow();
}
