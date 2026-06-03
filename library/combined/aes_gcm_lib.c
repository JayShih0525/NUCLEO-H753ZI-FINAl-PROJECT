#include "aes_gcm_lib.h"
#include "mbedtls/gcm.h"
#include <string.h>

static mbedtls_gcm_context gcm_ctx;
static uint8_t aes_key_is_set = 0;

uint8_t AESGCM_SetKey(const uint8_t *key, uint16_t key_len)
{
	int ret;

	if (key == NULL) return AES_GCM_ERR_NULL_PTR;

	if (key_len != AES_GCM_KEY_SIZE) return AES_GCM_ERR_SETKEY;

	mbedtls_gcm_init(&gcm_ctx);

	ret = mbedtls_gcm_setkey(
		&gcm_ctx,
		MBEDTLS_CIPHER_ID_AES,
		key,
		256
	);

	if (ret != 0){
		aes_key_is_set = 0;
		return AES_GCM_ERR_SETKEY;
	}

	aes_key_is_set = 1;

	return AES_GCM_OK;
}

uint8_t AESGCM_Encrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len, uint8_t *output, uint8_t *tag)
{
	int ret;

	if (nonce == NULL || input == NULL || output == NULL || tag == NULL) return AES_GCM_ERR_NULL_PTR;

	if (!aes_key_is_set) return AES_GCM_ERR_SETKEY;


	ret = mbedtls_gcm_crypt_and_tag(
		&gcm_ctx,
		MBEDTLS_GCM_ENCRYPT,
		input_len,
		nonce,
		AES_GCM_NONCE_SIZE,
		NULL,
		0,
		input,
		output,
		AES_GCM_TAG_SIZE,
		tag
	);

	if (ret != 0) return AES_GCM_ERR_ENCRYPT;

	return AES_GCM_OK;
}

uint8_t AESGCM_Decrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len, const uint8_t *tag, uint8_t *output)
{
	int ret;

	if (nonce == NULL || input == NULL || tag == NULL || output == NULL) return AES_GCM_ERR_NULL_PTR;

	if (!aes_key_is_set) return AES_GCM_ERR_SETKEY;


	ret = mbedtls_gcm_auth_decrypt(
		&gcm_ctx,
		input_len,
		nonce,
		AES_GCM_NONCE_SIZE,
		NULL,
		0,
		tag,
		AES_GCM_TAG_SIZE,
		input,
		output
	);

	if (ret != 0) return AES_GCM_ERR_AUTH_FAIL;

	return AES_GCM_OK;
}
