/*
 * bip_buffer.c
 *
 *  Created on: Sep 4, 2024
 *      Author: Yulin
 */

#include "bip_buffer.h"

#include <stdlib.h>
#include <string.h>

/*private function*/

/*public implement*/
Bip_buffer_t* bip_buffer_new(const uint32_t size) {
	Bip_buffer_t *new_bip = malloc(sizeof(Bip_buffer_t) + size);
	if (new_bip) {
		new_bip->a_index = new_bip->a_size = new_bip->b_index =
				new_bip->b_size = new_bip->reserve_index =
						new_bip->reserve_size = 0;
		new_bip->size = size;
		new_bip->data = (uint8_t*) (new_bip + sizeof(Bip_buffer_t)
				- sizeof(uint8_t*));
		return new_bip;
	}
	return NULL;
}

void bip_buffer_free(Bip_buffer_t *bip) {
	if (bip)
		free(bip);
}

uint8_t* bip_buffer_reserve(Bip_buffer_t *bip, const uint32_t data_size) {
	//如果 b 有大小，代表b region 使用中
	if (bip->b_size) {
		uint32_t remind_size = bip->a_index - bip->b_index - bip->b_size;
		if (remind_size < data_size)
			return NULL;
		bip->reserve_size = data_size;
		bip->reserve_index = bip->b_index + bip->b_size;
		return bip->data + bip->reserve_index;
	} else {
		uint32_t remind_size = bip->size - bip->a_index - bip->a_size;
		if (remind_size < data_size)
			return NULL;
		if (remind_size > bip->a_index) {
			bip->reserve_index = bip->a_index + bip->a_size;
		} else {
			bip->reserve_index = 0;
		}
		bip->reserve_size = data_size;
		return bip->data + bip->reserve_index;
	}
	return NULL;
}

void bip_buffer_commit(Bip_buffer_t *bip, uint32_t data_size) {
	if (!data_size) {
		//取消提交預約
		bip->reserve_index = bip->reserve_size = 0;
		return;
	}

	if (data_size > bip->reserve_size)
		data_size = bip->reserve_size;

	if (bip->a_size == 0 && bip->b_size) {
		//沒有任何暫存資料，創建 a region
		bip->a_index = bip->reserve_index;
		bip->a_size = data_size;
		bip->reserve_index = bip->reserve_size = 0;
		return;
	}
	if (bip->reserve_index == bip->a_size + bip->a_index) {
		//預定在 a region之後，延長a region
		bip->a_size += data_size;
	} else {
		//延長b region
		bip->b_size += data_size;
	}
	bip->reserve_size = bip->reserve_index = 0;
}

uint8_t* bip_buffer_contiguous_block(Bip_buffer_t *bip, uint32_t *size_ref) {
	if (bip->a_size) {
		if (size_ref)
			*size_ref = bip->a_size;
		return bip->data + bip->a_index;
	} else {
		if (size_ref)
			*size_ref = 0;
		return NULL;
	}
}

void bip_buffer_decommit(Bip_buffer_t *bip, uint32_t size) {
	if (size > bip->a_size)
		size = bip->a_size;
	if (bip->a_size == size) {
		bip->a_index = bip->b_index;
		bip->a_size = bip->b_size;
		bip->b_index = 0;
		bip->b_size = 0;
	} else {
		bip->a_size -= size;
		bip->a_index += size;
	}
}

