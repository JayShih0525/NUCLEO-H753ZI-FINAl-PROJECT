/*
 * rotary.c
 *
 *  Created on: Sep 23, 2024
 *      Author: aaas1
 */

#include "rotary.h"

static void _invoke_callback(rotary_handle_t *handle, uint32_t pin) {
	if (handle->a_pin == pin && handle->b_pin == handle->pin_flag) {
		handle->callback(ROTARY_DIR_B);
	}
	if (handle->b_pin == pin && handle->a_pin == handle->pin_flag) {
		handle->callback(ROTARY_DIR_A);
	}
	rotary_reset(handle);
}
static void _record_event(rotary_handle_t *handle, uint32_t pin) {
	if (handle->a_pin == pin || handle->b_pin == pin)
		handle->pin_flag = pin;
}

bool rotary_init(rotary_handle_t *handle, uint32_t pin_a, uint32_t pin_b,
		void (*callback)(rotary_dir_e dir)) {
	handle->pin_flag = 0;
	handle->a_pin = pin_a;
	handle->b_pin = pin_b;
	handle->callback = callback;
	return true;
}

void rotary_bind_callback(rotary_handle_t *handle, void (*fn)(rotary_dir_e dir)) {
	handle->callback = fn;
}

void rotary_reset(rotary_handle_t *handle) {
	handle->pin_flag = 0;
}

void rotary_event(rotary_handle_t *handle, uint32_t pin) {
	if (handle->pin_flag == 0)
		/* 當事件rotary 觸發事件首次發生，紀錄先發生的腳位 */
		_record_event(handle, pin);
	else
		/* 當事件rotary 觸發事件再次發生，調用回調函式 */
		_invoke_callback(handle, pin);
}
