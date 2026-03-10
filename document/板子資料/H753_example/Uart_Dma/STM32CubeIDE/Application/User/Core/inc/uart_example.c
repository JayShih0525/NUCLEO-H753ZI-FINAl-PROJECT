#include "uart_example.h"

#include "bip_buffer.h"
/*declare private property*/
struct {
	UART_HandleTypeDef *uart_handle; // 關聯的uart 句柄位置
	DMA_HandleTypeDef *dma_handle[2]; // 關聯的dma 句柄位置
	uint8_t *rx_buf; // 分配的緩衝位置
	uint32_t rx_buf_size;
	Bip_buffer_t *tx_bip_buf;
	uint32_t tx_dma_busy_flag;
	void (*receve_callback)(uint8_t *data, uint32_t data_len); //接收到資料後的回調函數
} example_handle = { 0 };

uint32_t _get_tx_buffer_remind();
void _push_data_into_tx_buffer(const uint8_t *data, uint32_t data_len);
HAL_StatusTypeDef _trigge_tx_send();

/* public functions */
HAL_StatusTypeDef uart_example_init(UART_HandleTypeDef *uart_handle,
		DMA_HandleTypeDef *dma_tx_handle, DMA_HandleTypeDef *dma_rx_handle,
		uint32_t buffer_size) {
	example_handle.uart_handle = uart_handle;
	example_handle.dma_handle[UART_DMA_EXAMPLE_RX_INDEX] = dma_rx_handle;
	example_handle.dma_handle[UART_DMA_EXAMPLE_TX_INDEX] = dma_tx_handle;

	/*配置Rx 緩衝位置*/
	example_handle.rx_buf = malloc(buffer_size);
	if (example_handle.rx_buf == NULL) {
		return HAL_ERROR;
	}
	// 如果有dma 句柄啟動DMA模式，不然啟動中斷
	if (example_handle.dma_handle[UART_DMA_EXAMPLE_RX_INDEX]) {
		//啟動Uart Dma RX 服務
		HAL_StatusTypeDef ret = HAL_UARTEx_ReceiveToIdle_DMA(
				example_handle.uart_handle, example_handle.rx_buf, buffer_size);
		if (ret != HAL_OK)
			return ret;
	} else {
		//啟動Uart interrupt RX 服務
		HAL_StatusTypeDef ret = HAL_UARTEx_ReceiveToIdle_IT(
				example_handle.uart_handle, example_handle.rx_buf, buffer_size);
		if (ret != HAL_OK)
			return ret;
	}
	uart_example_bind_receve_call_back(NULL);
	example_handle.rx_buf_size = buffer_size;

	/* 配置Tx緩衝位置 */
	example_handle.tx_bip_buf = bip_buffer_new(buffer_size);
	if (example_handle.tx_bip_buf == NULL)
		return HAL_ERROR;

	example_handle.tx_dma_busy_flag = 0;

	return HAL_OK;
}

HAL_StatusTypeDef uart_example_send(const uint8_t *data, uint32_t data_len) {
	HAL_StatusTypeDef ret = HAL_ERROR;
	__disable_irq();
	//預約要寫入的空間
	uint8_t *write_ptr = bip_buffer_reserve(example_handle.tx_bip_buf,
			data_len);
	if (write_ptr) {
		memcpy(write_ptr, data, data_len);
		bip_buffer_commit(example_handle.tx_bip_buf, data_len);
		__enable_irq();
		ret = _trigge_tx_send();
	} else {
		//寫入空間不足
		__enable_irq();
	}
	return ret;
}

HAL_StatusTypeDef uart_example_bind_receve_call_back(
		void (*callback)(uint8_t *data, uint32_t data_len)) {
	example_handle.receve_callback = callback;
	return HAL_OK;
}

/*private function*/

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
	if (example_handle.receve_callback)
		example_handle.receve_callback(example_handle.rx_buf, Size);
	// 如果有dma 句柄啟動DMA模式，不然啟動中斷
	if (example_handle.dma_handle[UART_DMA_EXAMPLE_RX_INDEX]) {
		//啟動Uart Dma RX 服務
		HAL_UARTEx_ReceiveToIdle_DMA(example_handle.uart_handle,
				example_handle.rx_buf, example_handle.rx_buf_size);
	} else {
		//啟動Uart interrupt RX 服務
		HAL_UARTEx_ReceiveToIdle_IT(example_handle.uart_handle,
				example_handle.rx_buf, example_handle.rx_buf_size);
	}
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart == example_handle.uart_handle) {
		bip_buffer_decommit(example_handle.tx_bip_buf,
				example_handle.tx_dma_busy_flag);
		example_handle.tx_dma_busy_flag = 0;
		_trigge_tx_send();
	}
}

HAL_StatusTypeDef _trigge_tx_send() {
	uint32_t block_len = 0;
	HAL_StatusTypeDef ret = HAL_OK;
	if (example_handle.tx_dma_busy_flag == 0) {
		uint8_t *read_ptr = bip_buffer_contiguous_block(
				example_handle.tx_bip_buf, &block_len);
		if (block_len) {
			example_handle.tx_dma_busy_flag = block_len;
			if (example_handle.dma_handle[UART_DMA_EXAMPLE_TX_INDEX]) {
				ret = HAL_UART_Transmit_DMA(example_handle.uart_handle,
						read_ptr, block_len);
			} else {
				ret = HAL_UART_Transmit_IT(example_handle.uart_handle, read_ptr,
						block_len);
			}
			return ret;
		}
	}
	return ret;
}
