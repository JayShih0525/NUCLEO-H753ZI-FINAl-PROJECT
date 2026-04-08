/*
 * SSD1289_driver.h
 *
 *  Created on: Sep 24, 2024
 *      Author: yulin
 */

#ifndef DRIVERS_SSD1289_SSD1289_DRIVER_H_
#define DRIVERS_SSD1289_SSD1289_DRIVER_H_
#include <stdint.h>
#include <stdbool.h>

typedef struct {
	bool (*reset)();
	bool (*init_sig)();
	bool (*put_cmd)(uint16_t command);
	bool (*put_data)(uint16_t data);
	bool (*put_reg)(uint16_t command, uint16_t data);
	bool (*reg_wirte_block)(uint16_t command, uint16_t* data, uint32_t data_len);
	void (*delayMs)(uint32_t ms);
}SSD1289_drive_t;

#ifdef STM32H753xx
#include "SSD1289_STM32F753xx_proting.h"
SSD1289_drive_t get_stm32H753_driver(void);
#endif

#endif /* DRIVERS_SSD1289_SSD1289_DRIVER_H_ */
