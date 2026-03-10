/*
 * SSD1289_STM32F753xx_proting.c
 *
 *  Created on: Sep 24, 2024
 *      Author: aaas1
 */

#ifdef STM32H753xx
#include "main.h"
#include "stm32h7xx_hal.h"
#include "SSD1289_STM32F753xx_proting.h"

extern SPI_HandleTypeDef hspi4;
static SPI_HandleTypeDef *user_spi = &hspi4;

static bool _send_spi_data(uint8_t *data, uint32_t data_len) {
	bool ret = false;
	stm32h7_ssd1289_set_cs(SSD1289_CS_ASSERT);
	if (user_spi->Init.DataSize == SPI_DATASIZE_16BIT)
		ret = HAL_SPI_Transmit(user_spi, data, data_len / 2, 2000) == HAL_OK ?
				true : false;
	else if (user_spi->Init.DataSize == SPI_DATASIZE_8BIT) {
		uint32_t count = 0;
		while (count < data_len) {
			uint8_t new_data[2] = { 0 };
			new_data[0] = *(data + count + 1);
			new_data[1] = *(data + count);
			if (HAL_SPI_Transmit(user_spi, new_data, 2, data_len) != HAL_OK) {
				ret = false;
				break;
			}
			count += 2;
		}
	}
	stm32h7_ssd1289_set_cs(SSD1289_CS_DEASSERT);
	return ret;
}

bool stm32h7_ssd1289_set_cs(SSD1289_CS_e flag) {
	bool ret = false;
	switch (flag) {
	case SSD1289_CS_ASSERT:
		HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
		ret = true;
		break;
	case SSD1289_CS_DEASSERT:
		HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
		ret = true;
		break;
	default:
		ret = false;
		break;
	}
	return ret;
}

bool stm32h7_ssd1289_set_dc(SSD1289_DC_e select) {
	bool ret = false;
	switch (select) {
	case SSD1289_DC_COMMAND:
		HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
		ret = true;
		break;
	case SSD1289_DC_DATA:
		HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
		ret = true;
		break;
	default:
		ret = false;
		break;
	}
	return ret;
}

bool stm32h7_ssd1289_reset() {

	HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
	stm32h7_ssd1289_delayMs(3); //1ms by datasheet
	HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LCD_CTRL_GPIO_Port, LCD_CTRL_Pin, GPIO_PIN_RESET);
	stm32h7_ssd1289_delayMs(3); //1ms by datasheet
	HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(LCD_CTRL_GPIO_Port, LCD_CTRL_Pin, GPIO_PIN_SET);
	return true;
}

bool stm32h7_ssd1289_init_sig() {
	if (!stm32h7_ssd1289_set_cs(SSD1289_CS_DEASSERT))
		return false;
	if (!stm32h7_ssd1289_set_dc(SSD1289_DC_DATA))
		return false;
	HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
	return true;
}

bool stm32h7_ssd1289_put_command(uint16_t command) {
	if (!stm32h7_ssd1289_set_dc(SSD1289_DC_COMMAND))
		return false;

	if (!_send_spi_data((uint8_t*) &command, 2))
		return false;
	return true;
}

bool stm32h7_write_data(uint8_t *data, uint32_t data_len) {
	if (!stm32h7_ssd1289_set_dc(SSD1289_DC_DATA))
		return false;
	if (!_send_spi_data(data, data_len))
		return false;
	return true;
}

bool stm32h7_ssd1289_put_data(uint16_t data) {
	return stm32h7_write_data((uint8_t*) &data, 2);
}

bool stm32h7_ssd1289_put_reg(uint16_t command, uint16_t data) {
	if (!stm32h7_ssd1289_put_command(command))
		return false;
	if (!stm32h7_ssd1289_put_data(data))
		return false;
	return true;
}

void stm32h7_ssd1289_delayMs(uint32_t ms) {
	HAL_Delay(ms);
}

bool stm32h7_ssd1289_reg_wirte_block(uint16_t command, uint16_t *data,
		uint32_t data_len) {
	uint32_t count = 0;
	uint8_t *data_ptr = (uint8_t*) data;
	const uint32_t block_size = 240;
	stm32h7_ssd1289_put_command(command);
	while (count < data_len) {
		if (count + block_size < data_len) {
			stm32h7_write_data(data_ptr + count, 240);
			count += block_size;
		} else {
			stm32h7_write_data(data_ptr + count, data_len - count);
			count += (data_len - count);
		}
	}
	return true;
}

#endif //STM32H753xx
