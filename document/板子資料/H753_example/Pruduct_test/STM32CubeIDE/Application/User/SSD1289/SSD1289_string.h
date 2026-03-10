/*
 * SSD1289_string.h
 *
 *  Created on: Sep 27, 2024
 *      Author: yulin
 */

#ifndef APPLICATION_USER_SSD1289_SSD1289_STRING_H_
#define APPLICATION_USER_SSD1289_SSD1289_STRING_H_

#include "SSD1289.h"

bool SSD1289_put_str(SSD1289_handle *lcd, uint16_t x, uint16_t y, char* str, uint16_t color);

#endif /* APPLICATION_USER_SSD1289_SSD1289_STRING_H_ */
