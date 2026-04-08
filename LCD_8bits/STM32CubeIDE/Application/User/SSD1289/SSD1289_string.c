/*
 * SSD1289_string.c
 *
 *  Created on: Sep 27, 2024
 *      Author: yulin
 */

#include "SSD1289_string.h"
#include "font.h"
#include <stdlib.h>
#include <string.h>
static void _decode_str_line(uint16_t *buf, uint8_t line_index, char *str,
		uint32_t str_len, uint16_t color) {
	for (int row = 0; row < 16; row++) {
		for (int index = 0; index < str_len; index++) {
			for (int col = 0; col < 8; col++) {
				*buf++ =
						((AsciiLib[*(str + index) - 32][row] >> (7 - col))
								& 0x01) == 1 ? color : 0;
			}
		}
	}
}

bool SSD1289_put_str(SSD1289_handle *lcd, uint16_t x, uint16_t y, char *str,
		uint16_t color) {
	int str_len = strlen(str); //計算字串長度
	const uint8_t ch_line_limit = (lcd->width - x) / 8; //計算一行可容納的字元量
	char *ch_line_buf = (char*) malloc(ch_line_limit); //暫存一行的字元
	if (!ch_line_limit)
		return false;

	while (str_len > 0) {
		uint16_t *buf;
		uint32_t str_line_len = 0;
		if (str_len > ch_line_limit) {
			memcpy(ch_line_buf, str, ch_line_limit);
			str_line_len = ch_line_limit;
			str += ch_line_limit;
		} else {
			memcpy(ch_line_buf, str, str_len);
			str_line_len = str_len;
		}

		buf = (uint16_t*) malloc(8 * 16 * sizeof(uint16_t) * str_line_len);
		if (!buf)
			break;
		for (int i = 0; i < 16; i++)
			_decode_str_line(buf, i, ch_line_buf, str_line_len, color);
		SSD1289_set_window(lcd, x, y, str_line_len * 8, 16);
		SSD1289_transmit_to_ram(lcd, buf,
				8 * 16 * sizeof(uint16_t) * str_line_len);

		free(buf);
		str_len -= ch_line_limit;
		if (y < lcd->heigh - 16) {
			y += 16;
			x = 0;
		} else
			break;
	}
	free(ch_line_buf);
	return true;
}



