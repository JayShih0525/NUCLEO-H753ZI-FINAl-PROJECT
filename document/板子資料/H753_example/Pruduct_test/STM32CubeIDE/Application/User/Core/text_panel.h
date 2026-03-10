/*
 * text_panel.h
 *
 *  Created on: Oct 29, 2024
 *      Author: yulin
 */

#ifndef APPLICATION_USER_CORE_TEXT_PANEL_H_
#define APPLICATION_USER_CORE_TEXT_PANEL_H_

void text_panel_init();

void text_panel_set();

void text_panel_end();

void text_panel_seek(int index);

int text_panael_pos();

int text_panel_print( __const char *__restrict __format, ...);

#endif /* APPLICATION_USER_CORE_TEXT_PANEL_H_ */
