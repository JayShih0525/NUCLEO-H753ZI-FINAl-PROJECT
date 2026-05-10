#ifndef AES_GCM_LIB_H
#define AES_GCM_LIB_H

#include <stdint.h>
#include <stddef.h>

#define AES_GCM_KEY_SIZE		32
#define AES_GCM_NONCE_SIZE	12
#define AES_GCM_TAG_SIZE		16

#define AES_GCM_OK              			0x00
#define AES_GCM_ERR_NULL_PTR	0xA1
#define AES_GCM_ERR_SETKEY      	0xA2
#define AES_GCM_ERR_ENCRYPT     	0xA3
#define AES_GCM_ERR_DECRYPT     	0xA4
#define AES_GCM_ERR_AUTH_FAIL   0xA5

uint8_t AESGCM_SetKey(
	const uint8_t *key,
	uint16_t key_len
);

uint8_t AESGCM_Encrypt(
	const uint8_t *nonce,
	const uint8_t *input,
	uint32_t input_len,
	uint8_t *output,
	uint8_t *tag
);

uint8_t AESGCM_Decrypt(
	const uint8_t *nonce,
	const uint8_t *input,
	uint32_t input_len,
	const uint8_t *tag,
	uint8_t *output
);

#endif
