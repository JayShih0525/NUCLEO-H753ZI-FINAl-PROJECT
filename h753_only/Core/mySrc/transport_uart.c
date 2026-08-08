#include "transport_uart.h"
#include <string.h>

/* 保留原本 uart3_protocol.c 的硬體清 buffer 邏輯，只是包成 Transport 介面 */

static Transport_Status_t UART_Read(Transport_t *self, uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)self->ctx;

    if (huart == NULL || buf == NULL) return TRANSPORT_ERR_PARAM;

    if (HAL_UART_Receive(huart, buf, len, timeout_ms) != HAL_OK) {
        return TRANSPORT_ERR_TIMEOUT;
    }

    return TRANSPORT_OK;
}

static Transport_Status_t UART_Write(Transport_t *self, const uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)self->ctx;

    if (huart == NULL || buf == NULL) return TRANSPORT_ERR_PARAM;

    if (HAL_UART_Transmit(huart, (uint8_t *)buf, len, timeout_ms) != HAL_OK) {
        return TRANSPORT_ERR_IO;
    }

    return TRANSPORT_OK;
}

static void UART_Flush(Transport_t *self)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)self->ctx;
    volatile uint8_t dummy;

    if (huart == NULL) return;

    HAL_UART_AbortReceive(huart);

#if defined(UART_FLAG_RXFNE)
    while (__HAL_UART_GET_FLAG(huart, UART_FLAG_RXFNE)) {
        dummy = (uint8_t)(huart->Instance->RDR & 0xFF);
        (void)dummy;
    }
#else
    while (__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE)) {
        dummy = (uint8_t)(huart->Instance->RDR & 0xFF);
        (void)dummy;
    }
#endif

    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);
}

void TransportUART_Init(Transport_t *transport, UART_HandleTypeDef *huart, uint32_t baudrate)
{
    huart->Instance = USART3;
    huart->Init.BaudRate = baudrate;
    huart->Init.WordLength = UART_WORDLENGTH_8B;
    huart->Init.StopBits = UART_STOPBITS_1;
    huart->Init.Parity = UART_PARITY_NONE;
    huart->Init.Mode = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_8;
    huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(huart) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_UARTEx_DisableFifoMode(huart) != HAL_OK) {
        Error_Handler();
    }

    transport->ctx = huart;
    transport->read = UART_Read;
    transport->write = UART_Write;
    transport->flush = UART_Flush;
    transport->notify_ready = NULL; /* UART 不需要 */
}
