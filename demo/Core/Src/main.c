#include "main.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// =====================================================
// Peripheral handles
// =====================================================

RNG_HandleTypeDef hrng;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart3;

// =====================================================
// Function prototypes
// =====================================================

void SystemClock_Config(void);
void Error_Handler(void);

static void MX_GPIO_Init(void);
static void MX_RNG_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART3_UART_Init(void);

static void DWT_DelayInit(void);
static void DelayUs(uint32_t microseconds);
static void SPI_HandleOneRequest(void);

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

#define SPI_TIMEOUT_MS          5000U

// Restore a very visible READY LOW pulse first.
#define SPI_READY_LOW_US        20U

#define SPI_READY_GPIO_Port     GPIOC
#define SPI_READY_Pin           GPIO_PIN_6

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t payloadLength;
    uint8_t command;
    uint8_t status;
    uint8_t flags;
    uint8_t reserved;
} SpiHeader;

// =====================================================
// Buffers - all in SRAM
// =====================================================

static uint8_t requestHeaderRx[SPI_HEADER_SIZE];
static uint8_t requestHeaderDummyTx[SPI_HEADER_SIZE];

static uint8_t responseHeaderTx[SPI_HEADER_SIZE];
static uint8_t responseHeaderDummyRx[SPI_HEADER_SIZE];

static uint8_t requestPayload[SPI_MAX_PAYLOAD_SIZE];
static uint8_t responsePayload[SPI_MAX_PAYLOAD_SIZE];

static uint8_t payloadDummyTx[SPI_MAX_PAYLOAD_SIZE];
static uint8_t payloadDummyRx[SPI_MAX_PAYLOAD_SIZE];

// =====================================================
// DWT delay
// =====================================================

static void DWT_DelayInit(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void DelayUs(uint32_t microseconds)
{
    const uint32_t cycles =
        (SystemCoreClock / 1000000U) * microseconds;

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
    HAL_GPIO_WritePin(
        SPI_READY_GPIO_Port,
        SPI_READY_Pin,
        GPIO_PIN_SET);
}

static void SPI_ReadyLow(void)
{
    HAL_GPIO_WritePin(
        SPI_READY_GPIO_Port,
        SPI_READY_Pin,
        GPIO_PIN_RESET);
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
    if ((txBuffer == NULL) ||
        (rxBuffer == NULL) ||
        (length == 0U))
    {
        return HAL_ERROR;
    }

    SPI_ReadyHigh();

    HAL_StatusTypeDef status =
        HAL_SPI_TransmitReceive(
            &hspi1,
            (uint8_t *)txBuffer,
            rxBuffer,
            length,
            SPI_TIMEOUT_MS);

    SPI_ReadyLow();
    DelayUs(SPI_READY_LOW_US);

    return status;
}

// =====================================================
// Validation / processing
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

    if (header->reserved != 0U)
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
            return
                (header->payloadLength == 0U)
                    ? SPI_STATUS_OK
                    : SPI_STATUS_BAD_LENGTH;

        case SPI_CMD_PROCESS:
            return
                (header->payloadLength > 0U)
                    ? SPI_STATUS_OK
                    : SPI_STATUS_BAD_LENGTH;

        default:
            return SPI_STATUS_BAD_COMMAND;
    }
}

static uint8_t ProcessRequest(
    const SpiHeader *requestHeader,
    SpiHeader *responseHeader)
{
    if ((requestHeader == NULL) ||
        (responseHeader == NULL))
    {
        return SPI_STATUS_PROCESS_FAIL;
    }

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
            static const uint8_t pong[] =
            {
                'P', 'O', 'N', 'G'
            };

            memcpy(responsePayload, pong, sizeof(pong));
            responseHeader->payloadLength =
                (uint32_t)sizeof(pong);

            return SPI_STATUS_OK;
        }

        case SPI_CMD_PROCESS:
        {
            const uint32_t size =
                requestHeader->payloadLength;

            for (uint32_t i = 0U; i < size; i++)
            {
                responsePayload[i] =
                    requestPayload[i] ^ 0xA5U;
            }

            responseHeader->payloadLength = size;

            return SPI_STATUS_OK;
        }

        default:
            responseHeader->payloadLength = 0U;
            return SPI_STATUS_BAD_COMMAND;
    }
}

static HAL_StatusTypeDef SPI_SendHeader(const SpiHeader *header)
{
    EncodeHeader(responseHeaderTx, header);

    return SPI_SlaveTransferPhase(
        responseHeaderTx,
        responseHeaderDummyRx,
        SPI_HEADER_SIZE);
}

static HAL_StatusTypeDef SendResponse(
    const SpiHeader *responseHeader)
{
    HAL_StatusTypeDef status =
        SPI_SendHeader(responseHeader);

    if (status != HAL_OK)
    {
        return status;
    }

    if (responseHeader->payloadLength > 0U)
    {
        status = SPI_SlaveTransferPhase(
            responsePayload,
            payloadDummyRx,
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

static void SPI_HandleOneRequest(void)
{
    HAL_StatusTypeDef status;

    SpiHeader requestHeader = {0};
    SpiHeader ackHeader = {0};
    SpiHeader responseHeader = {0};

    // Phase 1: Request Header
    status = SPI_SlaveTransferPhase(
        requestHeaderDummyTx,
        requestHeaderRx,
        SPI_HEADER_SIZE);

    if (status != HAL_OK)
    {
        SPI_Recover();
        return;
    }

    DecodeHeader(requestHeaderRx, &requestHeader);

    const uint8_t validationStatus =
        ValidateRequestHeader(&requestHeader);

    // Phase 2: Header ACK
    ackHeader.magic = SPI_MAGIC;
    ackHeader.sequence = requestHeader.sequence;
    ackHeader.payloadLength = 0U;
    ackHeader.command = requestHeader.command;
    ackHeader.status =
        (validationStatus == SPI_STATUS_OK)
            ? SPI_STATUS_HEADER_OK
            : validationStatus;
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
            payloadDummyTx,
            requestPayload,
            (uint16_t)requestHeader.payloadLength);

        if (status != HAL_OK)
        {
            SPI_Recover();
            return;
        }
    }

    responseHeader.status =
        ProcessRequest(
            &requestHeader,
            &responseHeader);

    // Phase 4/5: Final Response
    status = SendResponse(&responseHeader);

    if (status != HAL_OK)
    {
        SPI_Recover();
    }
}

// =====================================================
// Main
// =====================================================

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    DWT_DelayInit();

    MX_GPIO_Init();
    MX_RNG_Init();
    MX_SPI1_Init();
    MX_USART3_UART_Init();

    memset(
        requestHeaderDummyTx,
        0,
        sizeof(requestHeaderDummyTx));

    memset(
        payloadDummyTx,
        0,
        sizeof(payloadDummyTx));

    SPI_ReadyLow();

    while (1)
    {
        SPI_HandleOneRequest();

//    	SPI1_KCLK=120000000
//    	uint32_t spi1_kclk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI1);
//    	char buf[64];
//    	int len = snprintf(buf, sizeof(buf), "SPI1_KCLK=%lu\r\n", (unsigned long)spi1_kclk);
//    	HAL_UART_Transmit(&huart3, (uint8_t*)buf, len, 100);

    }
}

// =====================================================
// Clock
// =====================================================

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    RCC_OscInitStruct.OscillatorType =
        RCC_OSCILLATORTYPE_HSI48 |
        RCC_OSCILLATORTYPE_HSI;

    RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
    RCC_OscInitStruct.HSICalibrationValue =
        RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 15;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2 |
        RCC_CLOCKTYPE_D3PCLK1 |
        RCC_CLOCKTYPE_D1PCLK1;

    RCC_ClkInitStruct.SYSCLKSource =
        RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider =
        RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider =
        RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider =
        RCC_APB3_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider =
        RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider =
        RCC_APB2_DIV1;
    RCC_ClkInitStruct.APB4CLKDivider =
        RCC_APB4_DIV1;

    if (HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct,
            FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

// =====================================================
// RNG
// =====================================================

static void MX_RNG_Init(void)
{
    hrng.Instance = RNG;
    hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;

    if (HAL_RNG_Init(&hrng) != HAL_OK)
    {
        Error_Handler();
    }
}

// =====================================================
// SPI1
// =====================================================

static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_SLAVE;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    hspi1.Init.TxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi1.Init.RxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi1.Init.MasterSSIdleness =
        SPI_MASTER_SS_IDLENESS_00CYCLE;
    hspi1.Init.MasterInterDataIdleness =
        SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hspi1.Init.MasterReceiverAutoSusp =
        SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi1.Init.MasterKeepIOState =
        SPI_MASTER_KEEP_IO_STATE_DISABLE;
    hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        Error_Handler();
    }
}

// =====================================================
// USART3
// =====================================================

static void MX_USART3_UART_Init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart3.AdvancedInit.AdvFeatureInit =
        UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart3) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_SetTxFifoThreshold(
            &huart3,
            UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_SetRxFifoThreshold(
            &huart3,
            UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
    {
        Error_Handler();
    }
}

// =====================================================
// GPIO
// =====================================================

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(
        SPI_READY_GPIO_Port,
        SPI_READY_Pin,
        GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = SPI_READY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    HAL_GPIO_Init(
        SPI_READY_GPIO_Port,
        &GPIO_InitStruct);
}

// =====================================================
// Error
// =====================================================

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT

void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}

#endif
