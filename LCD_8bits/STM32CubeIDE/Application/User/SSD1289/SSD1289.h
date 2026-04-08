/*
 * SSD1289.h
 *
 *  Created on: Sep 24, 2024
 *      Author: yulin
 */

#ifndef DRIVERS_SSD1289_SSD1289_H_
#define DRIVERS_SSD1289_SSD1289_H_

#include "SSD1289_driver.h"

typedef struct {
	uint32_t width, heigh;
	SSD1289_drive_t driver;
} SSD1289_handle;

bool SSD1289_init(SSD1289_handle *lcd, SSD1289_drive_t driver, uint32_t width, uint32_t heigh);

bool SSD1289_clear(SSD1289_handle *lcd, uint16_t color);

bool SSD1289_set_window(SSD1289_handle *lcd, uint16_t x_pos, uint16_t y_pos,
		uint16_t width, uint16_t heigh);

bool SSD1289_transmit_to_ram(SSD1289_handle *lcd, uint16_t* data, uint32_t data_len);

#endif /* DRIVERS_SSD1289_SSD1289_H_ */
