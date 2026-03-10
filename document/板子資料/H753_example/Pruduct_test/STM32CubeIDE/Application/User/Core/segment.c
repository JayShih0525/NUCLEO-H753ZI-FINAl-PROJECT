/*
 * segment.c
 *
 *  Created on: Sep 30, 2024
 *      Author: yulin
 */

#include "segment.h"
#include "main.h"
#include<math.h>

const uint8_t _map[] = { 0x3F, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f,
		0x6f };

static void _flush(uint32_t index, uint8_t bits);

segment_t* segment_new(uint32_t display_len) {
	segment_t *new_ = (segment_t*) malloc(sizeof(segment_t) + display_len);
	if (!new_)
		return NULL;
	new_->display_len = display_len;
	new_->bits_buffer = ((uint8_t*) new_) + sizeof(segment_t);
	return new_;
}

void segment_distroy(segment_t *seg) {
	if (seg)
		free(seg);
}

void segment_str_set(segment_t *seg, const char *str) {
	int count = 0;
	while (count < seg->display_len && *str) {
		if (*str == '.') {
			*seg->bits_buffer |= 0x80;
			count++;

		} else if (*str >= '0' || *str <= '9') {
			*(seg->bits_buffer + count++) = _map[(*str) - '0'];
		}
		str++;
	}
}

void segment_num_set(segment_t *seg, uint32_t num) {
	for (int i = 0; i < seg->display_len; i++) {
		*(seg->bits_buffer + i) = _map[num % 10];
		num /= 10;
	}
}

void segment_float_set(segment_t *seg, uint32_t integer, uint32_t fraction,
		uint32_t round) {
	uint8_t *dist_ptr = seg->bits_buffer;
	for (int i = 0; i < round; i++) {
		*(dist_ptr++) = _map[fraction % 10];
		fraction /= 10;
	}
	for (int i = 0; i < seg->display_len - round; i++) {
		*(dist_ptr++) = _map[integer % 10];
		integer /= 10;
	}
	*(seg->bits_buffer + round) = *(seg->bits_buffer + round) | 0x80;
}

void segment_flush(segment_t *seg) {
	uint8_t index = ++seg->flush_index;
	if (index > seg->display_len) {
		index = seg->flush_index = 0;
	}
	_flush(index, seg->bits_buffer[index]);
}

static void _flush(uint32_t index, uint8_t bits) {

	GPIO_TypeDef *_port_map[] = { SegA_GPIO_Port, SegB_GPIO_Port,
	SegC_GPIO_Port, SegD_GPIO_Port, SegE_GPIO_Port, SegF_GPIO_Port,
	SegG_GPIO_Port, SegH_GPIO_Port };
	uint16_t _pin_map[] = { SegA_Pin, SegB_Pin, SegC_Pin, SegD_Pin,
	SegE_Pin, SegF_Pin, SegG_Pin, SegH_Pin };
	GPIO_TypeDef *_4_port_map[] = { SegS0_GPIO_Port, SegS1_GPIO_Port,
	SegS2_GPIO_Port, SegS3_GPIO_Port };
	uint16_t _4_pin_map[] = { SegS0_Pin, SegS1_Pin, SegS2_Pin,
	SegS3_Pin };
	uint8_t s = 1 << index;
	for (int i = 0; i < 4; i++) {
		HAL_GPIO_WritePin(_4_port_map[i], _4_pin_map[i],
				((s >> i) & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
	}
	for (int i = 0; i < 8; i++) {
		HAL_GPIO_WritePin(_port_map[i], _pin_map[i],
				((bits >> i) & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
	}
}
