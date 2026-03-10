/*
 * test_case.c
 *
 *  Created on: Oct 30, 2024
 *      Author: yulin
 */

#include "main.h"
#include "test_case.h"
#include "rotary.h"
#include "text_panel.h"
#include "string.h"
#include "fatfs.h"

rotary_handle_t rotary_handle;
void roteryCallback(rotary_dir_e dir);

bool test_case_init() {
	rotary_init(&rotary_handle, RoteryA_Pin, RoteryB_Pin, roteryCallback);
	return true;
}

bool test_case_i2c() {
	const uint16_t i2c_addr = 0xA0;
	extern I2C_HandleTypeDef hi2c4; //extern from main.c
	uint8_t rx_buf[8] = { 0 };
	uint8_t tx_data[1] = { 1 };
	if (HAL_I2C_Master_Transmit(&hi2c4, i2c_addr, tx_data, 1, 200) != HAL_OK)
		return false;
	if (HAL_I2C_Master_Receive(&hi2c4, i2c_addr, rx_buf, 1, 200) != HAL_OK)
		return false;
	return true;
}

bool test_case_spi() {
	const uint8_t tx_data[] = { 0, 0x90, 0, 0, 0, 0 };
	uint8_t rx_buf[sizeof(tx_data)] = { 0 };
	extern SPI_HandleTypeDef hspi4; //extern from main.c
	HAL_GPIO_WritePin(W25_NSS_GPIO_Port, W25_NSS_Pin, GPIO_PIN_RESET);
	if (HAL_SPI_TransmitReceive(&hspi4, tx_data, rx_buf, sizeof(tx_data), 200)
			!= HAL_OK)
		return false;
	HAL_GPIO_WritePin(W25_NSS_GPIO_Port, W25_NSS_Pin, GPIO_PIN_SET);
	if (rx_buf[5] != 0xEF)
		return false;
	return true;
}

static uint8_t *uart_rx_buf_ptr = NULL;
bool test_case_rs232() {
	const uint8_t tx_data[] = "hello";
	static uint8_t rx_buf[64] = { 0 };
	extern UART_HandleTypeDef huart6; //extern from main.c
	uart_rx_buf_ptr = rx_buf;
	HAL_UART_Receive_IT(&huart6, uart_rx_buf_ptr, 1);
	if (HAL_UART_Transmit(&huart6, tx_data, sizeof(tx_data), 200) != HAL_OK) {
		return false;
	}
	HAL_Delay(100);
	if (strcmp((char*) tx_data, (char*) rx_buf) != 0) {
		return false;
	}
	return true;
}

bool test_case_WV2() {
	const uint8_t tx_data[] = "AT";
	uint8_t rx_buf[12] = { 0 };
	extern UART_HandleTypeDef huart5; //extern from main.c
	HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_SET);

	if (HAL_UART_Transmit(&huart5, tx_data, 3, 200) != HAL_OK) {
		return false;
	}
	if (HAL_UART_Receive(&huart5, rx_buf, 2, 1000) != HAL_OK) {
		return false;
	}
	text_panel_print("%s.", rx_buf);
	HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_RESET);
	return true;
}

bool test_case_SDIO() {
	FRESULT res; /* FatFs function common result code */
	uint32_t bytesread; /* File read counts */
	uint32_t byteswrite; /* File write counts */
	const char s[] = "test string.";
	uint8_t buf[sizeof(s)] = { 0 };
	FILINFO fno;

	if (f_mount(&SDFatFS, (TCHAR const*) SDPath, 0) != FR_OK) {
		return false;
	}

	res = f_open(&SDFile, "EXAMPLE.txt", FA_WRITE);
	if (res != FR_OK)
		return false;

	res = f_write(&SDFile, s, sizeof(s), (void*) &byteswrite);
	if (res != FR_OK) {
		f_close(&SDFile);
		return false;
	}
	f_close(&SDFile);

	res = f_stat("EXAMPLE.txt", &fno);
	if (res != FR_OK)
		return false;
	res = f_open(&SDFile, "EXAMPLE.txt", FA_READ);
	if (res != FR_OK)
		return false;

	if (f_read(&SDFile, buf, sizeof(buf), (void*) &bytesread) != FR_OK) {
		f_close(&SDFile);
		return false;
	}

	if (strcmp(s, (char*) buf) == 0)
		return true;
	else
		return false;
}

static int exB0_flag = 0, exB1_flag = 0;
bool test_case_button() {
#include "text_panel.h"
	const uint32_t timeout = 9000;
	const uint32_t start = HAL_GetTick();
	exB0_flag = exB1_flag = 0;
	int count = 9;
	text_panel_print("%d", 9);
	while ((!exB0_flag || !exB1_flag) && HAL_GetTick() - start < timeout) {
		if (HAL_GetTick() - start > timeout - count * 1000) {

			text_panel_seek(text_panael_pos() - 1);
			text_panel_print("%d", count--);
		}
	}
	text_panel_seek(text_panael_pos() - 1);
	if (HAL_GetTick() - start >= timeout)
		return false;
	return true;
}

static int rotery_dira_flag = 0;
static int rotery_dirb_flag = 0;
static int rotery_btn_flag = 0;
void roteryCallback(rotary_dir_e dir) {
	switch (dir) {
	case ROTARY_DIR_A:
		rotery_dira_flag = 1;
		break;
	case ROTARY_DIR_B:
		rotery_dirb_flag = 1;
		break;
	}
}

bool test_case_rotery() {
	const int timeout = 9000;
	int count = 9;
	uint32_t start = HAL_GetTick();
	rotery_dira_flag = rotery_dirb_flag = rotery_btn_flag = 0;
	while ((!rotery_dira_flag || !rotery_dirb_flag || !rotery_btn_flag)
			&& HAL_GetTick() - start < timeout) {
		if (HAL_GetTick() - start > timeout - count * 1000) {
			text_panel_seek(text_panael_pos() - 1);
			text_panel_print("%d", count--);
		}
	}
	text_panel_seek(text_panael_pos() - 1);
	if (HAL_GetTick() - start >= timeout) {
		return false;
	}
	return true;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	uart_rx_buf_ptr++;
	HAL_UART_Receive_IT(huart, uart_rx_buf_ptr, 1);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	switch (GPIO_Pin) {
	case exB0_Pin:
		exB0_flag = 1;
		break;
	case exB1_Pin:
		exB1_flag = 1;
		break;
	case RoteryA_Pin:
		rotary_event(&rotary_handle, RoteryA_Pin);
		break;
	case RoteryB_Pin:
		rotary_event(&rotary_handle, RoteryB_Pin);
		break;
	case RoteryBtn_Pin:
		rotery_btn_flag = 1;
		break;
	case B1_Pin:
		break;
	}
}
