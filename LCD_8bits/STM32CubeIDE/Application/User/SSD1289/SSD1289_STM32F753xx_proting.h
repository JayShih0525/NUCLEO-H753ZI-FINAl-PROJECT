/*
 * SSD1289_STM32F753xx_proting.h
 *
 *  Created on: Sep 24, 2024
 *      Author: yulin
 */

#ifndef DRIVERS_SSD1289_SSD1289_STM32F753XX_PROTING_H_
#define DRIVERS_SSD1289_SSD1289_STM32F753XX_PROTING_H_

#include "SSD1289Def.h"
#include <stdint.h>
#include <stdbool.h>

bool stm32h7_ssd1289_set_cs(SSD1289_CS_e flag);
bool stm32h7_ssd1289_set_dc(SSD1289_DC_e select);
bool stm32h7_ssd1289_reset();
bool stm32h7_ssd1289_init_sig();
bool stm32h7_ssd1289_put_command(uint16_t command);
bool stm32h7_ssd1289_put_data(uint16_t data);
bool stm32h7_ssd1289_put_reg(uint16_t command, uint16_t data);
bool stm32h7_ssd1289_reg_wirte_block(uint16_t command, uint16_t* data, uint32_t data_len);
void stm32h7_ssd1289_delayMs(uint32_t ms);

#endif /* DRIVERS_SSD1289_SSD1289_STM32F753XX_PROTING_H_ */
