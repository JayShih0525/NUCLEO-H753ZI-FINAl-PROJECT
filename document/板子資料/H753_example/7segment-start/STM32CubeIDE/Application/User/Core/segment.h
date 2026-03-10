/*
 * segment.h
 *
 *  Created on: Sep 30, 2024
 *      Author: aaas1
 */

#ifndef APPLICATION_USER_CORE_SEGMENT_H_
#define APPLICATION_USER_CORE_SEGMENT_H_

#include <stdint.h>
#include <stdlib.h>

typedef struct {
	uint32_t display_len, flush_index;
	uint8_t* bits_buffer;
}segment_t;

/**
 * create a new handle
 *
 * @return new handle pointer. return NULL if Ram overflow
 */
segment_t* segment_new(uint32_t display_len);

/**
 * distroy handle
 * release memory by handle
 */
void segment_distroy(segment_t* seg);

/**
 * set display data with string, such like "3413" "3.14"
 */
void segment_str_set(segment_t *seg, const char *str);

/**
 * set display data with number
 */
void segment_num_set(segment_t *seg, uint32_t num);

/**
 * set display data by integer and fraction
 */
void segment_float_set(segment_t *seg, uint32_t integer, uint32_t fraction, uint32_t round);

/*
 * flush data to segment, this function needs to be period called.
 */
void segment_flush(segment_t *seg);

#endif /* APPLICATION_USER_CORE_SEGMENT_H_ */
