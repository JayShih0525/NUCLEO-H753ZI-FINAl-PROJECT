/*
 * bip_buffer.h
 *
 *  Created on: Sep 4, 2024
 *      Author: aaas1
 */

#ifndef APPLICATION_USER_CORE_INC_BIP_BUFFER_H_
#define APPLICATION_USER_CORE_INC_BIP_BUFFER_H_

#include <stdint.h>

typedef struct {
	uint32_t size;
	//region A
	uint32_t a_index, a_size;
	//region B
	uint32_t b_index, b_size;
	//region reserve
	uint32_t reserve_index, reserve_size;

	uint8_t *data;
}Bip_buffer_t;

/**
 * 動態配置一個buffer 的記憶體
 *
 * @size buffer 的大小
 */
Bip_buffer_t* bip_buffer_new(const uint32_t size);

/**
 * 釋放記憶體
 *
 * @bip bip buffer handle
 */
void bip_buffer_free(Bip_buffer_t* bip);

/**
 * 預定buffer size
 * 預訂後可以用回傳的的指標寫入資料
 *
 * @bip bip buffer handle
 * @data_size 要預定的資料大小
 * @return 預定的buffer 位址，如果無法預定回傳NULL
 */
uint8_t* bip_buffer_reserve(Bip_buffer_t* bip, uint32_t data_size);

/**
 * 承認預定
 * 承認對預訂空間的修改，使資料確實進入暫存中
 *
 * @bip bip buffer handle
 * @data_size 承認的資料長度
 */
void bip_buffer_commit(Bip_buffer_t* bip, uint32_t data_size);

/**
 * 取得連續的資料區塊
 * 取得的資區塊不一定會清空暫存內的所有資料，最好週期的使用這個函式檢查是否有資料存在於暫存中
 *
 * @bip bip buffer handle
 * @size_ref 區塊長度的參照
 */
uint8_t* bip_buffer_contiguous_block(Bip_buffer_t *bip, uint32_t *size_ref);

/**
 * 否認資料
 * 使用bip_buffer_contiguous_block處理完在暫存中的資料後，使用這個函式將區塊從暫存中移除。
 *
 * @bip bip buffer handle
 * @size 要否認的資料長度
 */
void bip_buffer_decommit(Bip_buffer_t *bip, uint32_t size);

#endif /* APPLICATION_USER_CORE_INC_BIP_BUFFER_H_ */
