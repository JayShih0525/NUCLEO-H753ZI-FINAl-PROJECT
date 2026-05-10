#include "aes_gcm_uart_app.h"
#include "aes_gcm_lib.h"
#include "uart3_protocol.h"
#include <string.h>

static uint8_t key[32] = {
	0x00, 0x01, 0x02, 0x03,
	0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B,
	0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13,
	0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B,
	0x1C, 0x1D, 0x1E, 0x1F
};

static uint8_t nonce[12] = {
	'S', 'T', 'M', '3',
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01
};

static uint8_t cmd_buffer[256];

static uint8_t plaintext[AES_GCM_APP_MAX_SIZE];
static uint8_t ciphertext[AES_GCM_APP_MAX_SIZE];
static uint8_t decrypted[AES_GCM_APP_MAX_SIZE];
static uint8_t tag[16];

static void AESGCM_IncrementNonce(void)
{
	for (int i = 11; i >= 4; i--){
		nonce[i]++;
		if (nonce[i] != 0) break;
	}
}

uint8_t AESGCM_UART_Init(void)
{
	return AESGCM_SetKey(key, 32);
}

void AESGCM_UART_EncryptTask(UART_HandleTypeDef *huart)
{
	uint32_t len;
	uint8_t status;

	UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");

	status = UART3_ReceivePacket(huart, plaintext, &len);

	if (status != UART3_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "RX_ERROR\n");
		UART3_ClearAll(huart);
		return;
	}

	status = AESGCM_Encrypt(
		nonce,
		plaintext,
		len,
		ciphertext,
		tag
	);

	if (status != AES_GCM_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "ENC_ERROR\n");
		return;
	}

	UART3_SendPacket(huart, nonce, 12);
	UART3_SendPacket(huart, ciphertext, len);
	UART3_SendPacket(huart, tag, 16);

	AESGCM_IncrementNonce();
}

void AESGCM_UART_DecryptTask(UART_HandleTypeDef *huart)
{
	uint32_t nonce_len;
	uint32_t cipher_len;
	uint32_t tag_len;
	uint8_t status;
	uint8_t received_nonce[12];

	UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");

	status = UART3_ReceivePacket(huart, received_nonce, &nonce_len);
	if (status != UART3_OK || nonce_len != 12){
		UART3_Printf(huart, HAL_MAX_DELAY, "NONCE_ERROR\n");
		UART3_ClearAll(huart);
		return;
	}

	status = UART3_ReceivePacket(huart, ciphertext, &cipher_len);
	if (status != UART3_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "CIPHER_ERROR\n");
		UART3_ClearAll(huart);
		return;
	}

	status = UART3_ReceivePacket(huart, tag, &tag_len);
	if (status != UART3_OK || tag_len != 16){
		UART3_Printf(huart, HAL_MAX_DELAY, "TAG_ERROR\n");
		UART3_ClearAll(huart);
		return;
	}

	status = AESGCM_Decrypt(
		received_nonce,
		ciphertext,
		cipher_len,
		tag,
		decrypted
	);

	if (status != AES_GCM_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "DEC_ERROR\n");
		return;
	}

	UART3_SendPacket(huart, decrypted, cipher_len);
}

void AESGCM_UART_CommandLoop(UART_HandleTypeDef *huart)
{
	uint16_t cmd_len;

	cmd_len = UART3_ReadLine(
		huart,
		cmd_buffer,
		sizeof(cmd_buffer),
		HAL_MAX_DELAY
	);

	if (cmd_len == 0) return;

	if (strcmp((char *)cmd_buffer, "ENCRYPT") == 0){
		AESGCM_UART_EncryptTask(huart);
	}

	else if (strcmp((char *)cmd_buffer, "DECRYPT") == 0){
		AESGCM_UART_DecryptTask(huart);
	}

	else if (strcmp((char *)cmd_buffer, "CLEAR") == 0){
		UART3_ClearRxBuffer();
		UART3_ClearHardwareRx(huart);
		HAL_Delay(1);
		UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");
	}

	else{
		UART3_Printf(huart, HAL_MAX_DELAY, "UNKNOWN_CMD\n");
	}
}
