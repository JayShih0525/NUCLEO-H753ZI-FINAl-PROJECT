#include "ml_kem_uart_app.h"
#include "ml_kem_lib.h"
#include "aes_gcm_lib.h"
#include "uart3_protocol.h"
#include <string.h>

static uint8_t cmd_buffer[ML_KEM_CMD_BUFFER_SIZE];

static uint8_t public_key[MLKEM_PUBLIC_KEY_SIZE];
static uint8_t secret_key[MLKEM_SECRET_KEY_SIZE];
static uint8_t kem_ciphertext[MLKEM_CIPHERTEXT_SIZE];
static uint8_t shared_secret[MLKEM_SHARED_SIZE];

static uint8_t kem_ready = 0;

uint8_t MLKEM_UART_Init(void)
{
	uint8_t status;

	memset(public_key, 0, sizeof(public_key));
	memset(secret_key, 0, sizeof(secret_key));
	memset(kem_ciphertext, 0, sizeof(kem_ciphertext));
	memset(shared_secret, 0, sizeof(shared_secret));

	status = MLKEM_Keypair(public_key, secret_key);

	if (status != MLKEM_OK){
		kem_ready = 0;
		return ML_KEM_UART_ERR_KEYPAIR;
	}

	kem_ready = 1;

	return ML_KEM_UART_OK;
}

/*
	GET_KEM_PUBLIC_KEY

	STM32:
		回傳 public_key 給 Python

	Python:
		用 public_key 做 ML-KEM encapsulate
*/
void MLKEM_UART_SendPublicKeyTask(UART_HandleTypeDef *huart)
{
	if (kem_ready == 0){
		UART3_Printf(huart, HAL_MAX_DELAY, "KEM_NOT_READY\n");
		return;
	}

	UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");

	UART3_SendPacket(
		huart,
		public_key,
		MLKEM_PUBLIC_KEY_SIZE
	);
}

/*
	KEM_DECAPSULATE

	Python:
		傳 kem_ciphertext 給 STM32

	STM32:
		用 secret_key decapsulate
		得到 shared_secret
		把 shared_secret 設成 AES-GCM key
		只回 KEM_OK，不回傳 shared_secret

	正式 app 用這個。
*/
void MLKEM_UART_DecapsulateTask(UART_HandleTypeDef *huart)
{
	uint16_t cipher_len;
	uint8_t status;

	if (kem_ready == 0){
		UART3_Printf(huart, HAL_MAX_DELAY, "KEM_NOT_READY\n");
		return;
	}

	UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");

	status = UART3_ReceivePacket(
		huart,
		kem_ciphertext,
		&cipher_len
	);

	if (status != UART3_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "KEM_RX_ERROR\n");
		UART3_ClearAll(huart);
		return;
	}

	if (cipher_len != MLKEM_CIPHERTEXT_SIZE){
		UART3_Printf(huart, HAL_MAX_DELAY, "KEM_CIPHERTEXT_LEN_ERROR\n");
		UART3_ClearAll(huart);
		return;
	}

	memset(shared_secret, 0, sizeof(shared_secret));

	status = MLKEM_Decapsulate(
		shared_secret,
		kem_ciphertext,
		secret_key
	);

	if (status != MLKEM_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "KEM_DECAPSULATE_ERROR\n");
		return;
	}

	status = AESGCM_SetKey(shared_secret, 32);

	if (status != AES_GCM_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "AES_KEY_ERROR\n");
		return;
	}

	UART3_Printf(huart, HAL_MAX_DELAY, "KEM_OK\n");
}

/*
	KEM_ENCAPSULATE

	STM32:
		自己用 public_key encapsulate
		產生 kem_ciphertext + shared_secret
		把 shared_secret 設成 AES-GCM key
		回傳 kem_ciphertext 給 Python

	注意：
		這個方向是 STM32 encapsulate，Python 要用 secret_key decapsulate。
		只有在 Python 端先產生 keypair 並把 public_key 傳給 STM32 時才有用。
		如果你的主流程是 STM32 keypair、Python encapsulate，就不用這個。
*/
void MLKEM_UART_EncapsulateTask(UART_HandleTypeDef *huart)
{
	uint8_t status;

	if (kem_ready == 0){
		UART3_Printf(huart, HAL_MAX_DELAY, "KEM_NOT_READY\n");
		return;
	}

	memset(kem_ciphertext, 0, sizeof(kem_ciphertext));
	memset(shared_secret, 0, sizeof(shared_secret));

	status = MLKEM_Encapsulate(
		kem_ciphertext,
		shared_secret,
		public_key
	);

	if (status != MLKEM_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "KEM_ENCAPSULATE_ERROR\n");
		return;
	}

	status = AESGCM_SetKey(shared_secret, 32);

	if (status != AES_GCM_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "AES_KEY_ERROR\n");
		return;
	}

	UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");

	UART3_SendPacket(
		huart,
		kem_ciphertext,
		MLKEM_CIPHERTEXT_SIZE
	);
}

/*
	KEM_REKEY

	重新產生 STM32 public_key / secret_key
*/
void MLKEM_UART_RekeyTask(UART_HandleTypeDef *huart)
{
	uint8_t status;

	status = MLKEM_UART_Init();

	if (status != ML_KEM_UART_OK){
		UART3_Printf(huart, HAL_MAX_DELAY, "KEM_REKEY_ERROR\n");
		return;
	}

	UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");
}

void MLKEM_UART_CommandLoop(UART_HandleTypeDef *huart)
{
	uint16_t cmd_len;

	cmd_len = UART3_ReadLine(
		huart,
		cmd_buffer,
		sizeof(cmd_buffer),
		HAL_MAX_DELAY
	);

	if (cmd_len == 0) return;

	if (strcmp((char *)cmd_buffer, "GET_KEM_PUBLIC_KEY") == 0){
		MLKEM_UART_SendPublicKeyTask(huart);
	}

	else if (strcmp((char *)cmd_buffer, "KEM_DECAPSULATE") == 0){
		MLKEM_UART_DecapsulateTask(huart);
	}

	else if (strcmp((char *)cmd_buffer, "KEM_ENCAPSULATE") == 0){
		MLKEM_UART_EncapsulateTask(huart);
	}

	else if (strcmp((char *)cmd_buffer, "KEM_REKEY") == 0){
		MLKEM_UART_RekeyTask(huart);
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
