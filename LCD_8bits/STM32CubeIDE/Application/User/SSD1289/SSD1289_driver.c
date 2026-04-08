/*
 * SSD1289_driver.c
 *
 *  Created on: Sep 24, 2024
 *      Author: yulin
 */

#include "SSD1289_driver.h"

#ifdef STM32H753xx
SSD1289_drive_t get_stm32H753_driver(void) {
	SSD1289_drive_t ret;
	ret.init_sig = stm32h7_ssd1289_init_sig;
	ret.reset = stm32h7_ssd1289_reset;
	ret.put_cmd = stm32h7_ssd1289_put_command;
	ret.put_data = stm32h7_ssd1289_put_data;
	ret.put_reg = stm32h7_ssd1289_put_reg;
	ret.delayMs = stm32h7_ssd1289_delayMs;
	ret.reg_wirte_block = stm32h7_ssd1289_reg_wirte_block;
	return ret;
}
#endif
