#ifndef TRANSPORT_UART_H
#define TRANSPORT_UART_H

#include "transport.h"
#include "main.h"   /* for UART_HandleTypeDef */

/*
 * 初始化 UART3 硬體並回填一個 Transport_t，供上層使用。
 * 呼叫端不需要再直接碰 huart。
 */
void TransportUART_Init(Transport_t *transport, UART_HandleTypeDef *huart, uint32_t baudrate);

#endif /* TRANSPORT_UART_H */
