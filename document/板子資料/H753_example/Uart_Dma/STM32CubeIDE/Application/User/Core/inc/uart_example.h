/*
 * uart_dma_example.h
 *
 *  Created on: Aug 28, 2024
 *      Author: yulin
 */

#ifndef APPLICATION_USER_CORE_INC_UART_EXAMPLE_H_
#define APPLICATION_USER_CORE_INC_UART_EXAMPLE_H_

#include "stm32h7xx_hal.h"
#include <string.h>
#include <stdlib.h>

#define UART_DMA_EXAMPLE_TX_INDEX 0
#define UART_DMA_EXAMPLE_RX_INDEX 1


/**
 *  初始化範例結構，並配置緩衝大小。
 *
 *  根據傳入值 buffer_size配置緩衝大小，如果相應的dma handle 為 NULL 則不會啟動DMA 功能，也不會為其配置緩衝記憶體。
 *  特需情況需要釋放配置時，呼叫 uart_dma_example_destroy() 釋放記憶體空間。
 *
 *  @example_handle 需要初始化的句柄
 *  @uart_handle 關聯的uart 句柄
 *  @dma_tx_handle 關聯的傳出dma 句柄，這個值可以是NULL
 *  @dma_rx_handle  關聯的接收dma 句柄，這個值可以是NULL
 *  @buffer_size 期望配置的緩衝大小，如果 tx 與 rx 的句柄不為NULL ，兩者都會被分配相同的緩衝大小。
 *  @return HAL status
 */
HAL_StatusTypeDef uart_example_init(
		UART_HandleTypeDef *uart_handle, DMA_HandleTypeDef *dma_tx_handle,
		DMA_HandleTypeDef *dma_rx_handle, uint32_t buffer_size);

/**
 * 傳送資料
 *
 * @example_handle 操作的句柄
 * @data 要傳送的資料位置
 * @data_len 要傳送的資料長度
 * @return HAL status
 */
HAL_StatusTypeDef uart_example_send(
		const uint8_t *data, uint32_t data_len);

/**
 * 綁定接收資料的回調函數
 *
 * @example_handle 操作的句柄
 * @callback 綁定的函數指標，data 為接收到的資料位置，data_len 為接收到的資料長度
 * @return HAL status
 */
HAL_StatusTypeDef uart_example_bind_receve_call_back(

		void(* callback)(uint8_t *data, uint32_t data_len));

/**
 * 啟動uart dma 服務
 *
 * @example_handle 操作的句柄
 * @return HAL status
 */
HAL_StatusTypeDef uart_example_start();

#endif /* APPLICATION_USER_CORE_INC_UART_EXAMPLE_H_ */
