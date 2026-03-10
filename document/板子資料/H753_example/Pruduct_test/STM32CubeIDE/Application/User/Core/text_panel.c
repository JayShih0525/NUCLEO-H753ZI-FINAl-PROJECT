/*
 * text_panel.c
 *
 *  Created on: Oct 29, 2024
 *      Author: yulin
 */

#include "text_panel.h"
#include "SSD1289_string.h"
#include "cmsis_gcc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define _END (20 * 30)
SSD1289_handle handle;
static int curse = 0;
static int curse_end = 0;

void text_panel_init() {
	SSD1289_init(&handle, get_stm32H753_driver(), 240, 320);
	SSD1289_clear(&handle, 0);
}

void text_panel_seek(int index) {
	__disable_irq();
	curse = index;
	__enable_irq();
}

void text_panel_set() {
	__disable_irq();
	curse = 0;
	__enable_irq();
}

void text_panel_end() {
	__disable_irq();
	curse = curse_end;
	__enable_irq();
}

int text_panael_pos() {
	return curse;
}

int text_panel_print( __const char *__restrict __format, ...) {
	if (curse >= _END)
		return 0;
	char buf_va[256] = { 0 };
	va_list args;
	va_start(args, __format);
	int return_status = vsprintf(buf_va, __format, args);
	va_end(args);

	int n_ = 0;
	if (buf_va[strlen(buf_va) - 1] == '\n')
		n_ = 1;

	const char s[] = "\n";
	char *tok = strtok(buf_va, s);
	char tok_buf[256];
	__disable_irq();
	while (tok != NULL && curse + strlen(tok) < _END) {

		strcpy(tok_buf, tok);
		SSD1289_put_str(&handle, (curse % 30) * 8, (curse / 30) * 16, tok_buf,
				0xffff);
		curse += strlen(tok_buf);

		tok = strtok(NULL, s);
		if (tok != NULL) {
			if (curse % 30)
				curse += (30 - (curse % 30));
			else {
				curse += 30;
			}
		}
	}

	if (n_) {
		if (curse % 30)
			curse += (30 - (curse % 30));
		else {
			curse += 30;
		}
	}

	curse_end = curse > curse_end ? curse : curse_end;
	__enable_irq();
	return return_status;
}

