#include "main.h"
#include "string.h"
#include "uart3_protocol.h"
#include "randombytes.h"
#include "ml_kem_uart_app.h"
#include "aes_gcm_uart_app.h"

#include <stdint.h>
#include <stdio.h>

#include "dilithium_api.h"
#include "ml_kem_api.h"
UART_HandleTypeDef huart3;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);

static uint8_t app_cmd_buffer[256];

static uint8_t dilithium_pk[pqcrystals_dilithium2_ref_PUBLICKEYBYTES];
static uint8_t dilithium_sk[pqcrystals_dilithium2_ref_SECRETKEYBYTES];
static uint8_t dilithium_sig[pqcrystals_dilithium2_ref_BYTES];

static uint8_t msg_buffer[1024];
static uint8_t sig_buffer[pqcrystals_dilithium2_ref_BYTES];
static uint8_t laptop_dilithium_pk[pqcrystals_dilithium2_ref_PUBLICKEYBYTES];
static uint8_t auth_challenge[32];

static uint8_t dilithium_key_ready = 0;
static uint8_t laptop_dilithium_key_ready = 0;
static uint8_t auth_challenge_ready = 0;

static uint8_t APP_InitCrypto(UART_HandleTypeDef *huart)
{
    uint8_t ok = 1;

    if (AESGCM_UART_Init() != 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "AES_INIT_FAIL\n");
        ok = 0;
    }

    if (MLKEM_UART_Init() != 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "KEM_INIT_FAIL\n");
        ok = 0;
    }

    if (pqcrystals_dilithium2_ref_keypair(dilithium_pk, dilithium_sk) != 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "DILITHIUM_INIT_FAIL\n");
        dilithium_key_ready = 0;
        ok = 0;
    } else {
        dilithium_key_ready = 1;
    }

    return ok;
}

static void DILITHIUM_Rekey(UART_HandleTypeDef *huart)
{
    int ret;

    ret = pqcrystals_dilithium2_ref_keypair(dilithium_pk, dilithium_sk);

    if (ret == 0) {
        dilithium_key_ready = 1;
        UART3_Printf(huart, HAL_MAX_DELAY, "DILITHIUM_REKEY_OK\n");
    } else {
        dilithium_key_ready = 0;
        UART3_Printf(huart, HAL_MAX_DELAY, "DILITHIUM_REKEY_FAIL\n");
    }
}

static void DILITHIUM_SelfTest(UART_HandleTypeDef *huart)
{
    uint8_t msg[] = "hello dilithium stm32";
    size_t msglen = strlen((char *)msg);
    size_t siglen = 0;
    int ret;

    UART3_Printf(huart, HAL_MAX_DELAY, "DILITHIUM_SELFTEST_START\n");

    ret = pqcrystals_dilithium2_ref_keypair(dilithium_pk, dilithium_sk);
    if (ret != 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "KEYPAIR_FAIL\n");
        return;
    }

    dilithium_key_ready = 1;
    UART3_Printf(huart, HAL_MAX_DELAY, "KEYPAIR_PASS\n");

    ret = pqcrystals_dilithium2_ref_signature(
        dilithium_sig,
        &siglen,
        msg,
        msglen,
        NULL,
        0,
        dilithium_sk
    );

    if (ret != 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "SIGN_FAIL\n");
        return;
    }

    UART3_Printf(huart, HAL_MAX_DELAY, "SIGN_PASS siglen=%u\n", (unsigned int)siglen);

    ret = pqcrystals_dilithium2_ref_verify(
        dilithium_sig,
        siglen,
        msg,
        msglen,
        NULL,
        0,
        dilithium_pk
    );

    if (ret == 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "VERIFY_PASS\n");
    } else {
        UART3_Printf(huart, HAL_MAX_DELAY, "VERIFY_FAIL\n");
        return;
    }

    msg[0] ^= 1;

    ret = pqcrystals_dilithium2_ref_verify(
        dilithium_sig,
        siglen,
        msg,
        msglen,
        NULL,
        0,
        dilithium_pk
    );

    if (ret != 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "TAMPER_TEST_PASS\n");
    } else {
        UART3_Printf(huart, HAL_MAX_DELAY, "TAMPER_TEST_FAIL\n");
    }
}

static void DILITHIUM_SendPublicKey(UART_HandleTypeDef *huart)
{
    if (!dilithium_key_ready) {
        DILITHIUM_Rekey(huart);
    }

    UART3_SendPacket(
        huart,
        dilithium_pk,
        pqcrystals_dilithium2_ref_PUBLICKEYBYTES,
        pqcrystals_dilithium2_ref_PUBLICKEYBYTES
    );
}

static void DILITHIUM_SignTask(UART_HandleTypeDef *huart)
{
    uint32_t msg_len = 0;
    size_t siglen = 0;
    uint8_t status;
    int ret;

    if (!dilithium_key_ready) {
        DILITHIUM_Rekey(huart);
    }

    status = UART3_ReceivePacket(
        huart,
        msg_buffer,
        sizeof(msg_buffer),
        &msg_len
    );

    if (status != UART3_OK) {
        UART3_Printf(huart, HAL_MAX_DELAY, "SIGN_RX_FAIL\n");
        return;
    }

    ret = pqcrystals_dilithium2_ref_signature(
        dilithium_sig,
        &siglen,
        msg_buffer,
        msg_len,
        NULL,
        0,
        dilithium_sk
    );

    if (ret != 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "SIGN_FAIL\n");
        return;
    }

    UART3_SendPacket(
        huart,
        dilithium_sig,
        pqcrystals_dilithium2_ref_BYTES,
        (uint32_t)siglen
    );
}

static void DILITHIUM_VerifyTask(UART_HandleTypeDef *huart)
{
    uint32_t sig_len = 0;
    uint32_t msg_len = 0;
    uint8_t status;
    int ret;

    if (!dilithium_key_ready) {
        UART3_Printf(huart, HAL_MAX_DELAY, "NO_KEY\n");
        return;
    }

    status = UART3_ReceivePacket(
        huart,
        sig_buffer,
        sizeof(sig_buffer),
        &sig_len
    );

    if (status != UART3_OK) {
        UART3_Printf(huart, HAL_MAX_DELAY, "VERIFY_SIG_RX_FAIL\n");
        return;
    }

    status = UART3_ReceivePacket(
        huart,
        msg_buffer,
        sizeof(msg_buffer),
        &msg_len
    );

    if (status != UART3_OK) {
        UART3_Printf(huart, HAL_MAX_DELAY, "VERIFY_MSG_RX_FAIL\n");
        return;
    }

    ret = pqcrystals_dilithium2_ref_verify(
        sig_buffer,
        sig_len,
        msg_buffer,
        msg_len,
        NULL,
        0,
        dilithium_pk
    );

    if (ret == 0) {
        UART3_Printf(huart, HAL_MAX_DELAY, "VERIFY_OK\n");
    } else {
        UART3_Printf(huart, HAL_MAX_DELAY, "VERIFY_FAIL\n");
    }
}

static void DILITHIUM_SetLaptopPublicKey(UART_HandleTypeDef *huart)
{
    uint32_t pk_len = 0;
    uint8_t status;

    UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");

    status = UART3_ReceivePacket(
        huart,
        laptop_dilithium_pk,
        sizeof(laptop_dilithium_pk),
        &pk_len
    );

    if (status != UART3_OK) {
        laptop_dilithium_key_ready = 0;
        UART3_Printf(huart, HAL_MAX_DELAY, "LAPTOP_PK_RX_FAIL\n");
        return;
    }

    if (pk_len != sizeof(laptop_dilithium_pk)) {
        laptop_dilithium_key_ready = 0;
        UART3_Printf(huart, HAL_MAX_DELAY, "LAPTOP_PK_LEN_FAIL\n");
        return;
    }

    laptop_dilithium_key_ready = 1;
    UART3_Printf(huart, HAL_MAX_DELAY, "LAPTOP_PK_OK\n");
}

static void DILITHIUM_SendAuthChallenge(UART_HandleTypeDef *huart)
{
    randombytes(auth_challenge, sizeof(auth_challenge));
    auth_challenge_ready = 1;

    UART3_SendPacket(
        huart,
        auth_challenge,
        sizeof(auth_challenge),
        sizeof(auth_challenge)
    );
}

static void DILITHIUM_VerifyLaptopAuth(UART_HandleTypeDef *huart)
{
    uint32_t sig_len = 0;
    uint8_t status;
    int ret;

    if (!laptop_dilithium_key_ready) {
        UART3_Printf(huart, HAL_MAX_DELAY, "NO_LAPTOP_KEY\n");
        return;
    }

    if (!auth_challenge_ready) {
        UART3_Printf(huart, HAL_MAX_DELAY, "NO_AUTH_CHALLENGE\n");
        return;
    }

    UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");

    status = UART3_ReceivePacket(
        huart,
        sig_buffer,
        sizeof(sig_buffer),
        &sig_len
    );

    if (status != UART3_OK) {
        UART3_Printf(huart, HAL_MAX_DELAY, "LAPTOP_SIG_RX_FAIL\n");
        return;
    }

    ret = pqcrystals_dilithium2_ref_verify(
        sig_buffer,
        sig_len,
        auth_challenge,
        sizeof(auth_challenge),
        NULL,
        0,
        laptop_dilithium_pk
    );

    if (ret == 0) {
        auth_challenge_ready = 0;
        UART3_Printf(huart, HAL_MAX_DELAY, "LAPTOP_AUTH_OK\n");
    } else {
        UART3_Printf(huart, HAL_MAX_DELAY, "LAPTOP_AUTH_FAIL\n");
    }
}

void APP_CommandLoop(UART_HandleTypeDef *huart)
{
    uint16_t cmd_len;

    cmd_len = UART3_ReadLine(
        huart,
        app_cmd_buffer,
        sizeof(app_cmd_buffer),
        HAL_MAX_DELAY
    );

    if (cmd_len == 0) return;

    if (strcmp((char *)app_cmd_buffer, "DILITHIUM_SELFTEST") == 0) {
        DILITHIUM_SelfTest(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "DILITHIUM_REKEY") == 0) {
        DILITHIUM_Rekey(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "GET_DILITHIUM_PUBLIC_KEY") == 0) {
        DILITHIUM_SendPublicKey(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "DILITHIUM_SIGN") == 0) {
        DILITHIUM_SignTask(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "DILITHIUM_VERIFY") == 0) {
        DILITHIUM_VerifyTask(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "SET_LAPTOP_DILITHIUM_PUBLIC_KEY") == 0) {
        DILITHIUM_SetLaptopPublicKey(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "GET_AUTH_CHALLENGE") == 0) {
        DILITHIUM_SendAuthChallenge(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "VERIFY_LAPTOP_AUTH") == 0) {
        DILITHIUM_VerifyLaptopAuth(huart);
    }

    // ML-KEM
    else if (strcmp((char *)app_cmd_buffer, "GET_KEM_PUBLIC_KEY") == 0){
	    MLKEM_UART_SendPublicKeyTask(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "KEM_DECAPSULATE") == 0){
	    MLKEM_UART_DecapsulateTask(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "KEM_REKEY") == 0){
	    MLKEM_UART_RekeyTask(huart);
    }

    else if (strcmp((char *)app_cmd_buffer, "KEM_ENCAPSULATE") == 0){
	    MLKEM_UART_EncapsulateTask(huart);
    }

    // AES-GCM
    else if (strcmp((char *)app_cmd_buffer, "ENCRYPT") == 0){
	    AESGCM_UART_EncryptTask(huart);
    }
    else if (strcmp((char *)app_cmd_buffer, "DECRYPT") == 0){
	    AESGCM_UART_DecryptTask(huart);
    }

    // Common
    else if (strcmp((char *)app_cmd_buffer, "CLEAR") == 0){
	    UART3_ClearRxBuffer();
	    UART3_ClearHardwareRx(huart);
	    HAL_Delay(1);
	    UART3_Printf(huart, HAL_MAX_DELAY, "READY\n");
    }

    else {
	    UART3_Printf(huart, HAL_MAX_DELAY, "UNKNOWN_CMD\n");
    }
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    UART3_Init(&huart3, 4000000);
    Random_Init();

    UART3_ClearAll(&huart3);

    if (APP_InitCrypto(&huart3)) {
        UART3_Printf(&huart3, HAL_MAX_DELAY, "APP_READY\n");
    } else {
        UART3_Printf(&huart3, HAL_MAX_DELAY, "APP_INIT_ERROR\n");
    }

    while (1) {
        APP_CommandLoop(&huart3);
    }
}

void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	/*
	 * Supply configuration
	 */
	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

	/*
	 * Voltage scaling for high frequency
	 * VOS1 is needed for 400 MHz range.
	 */
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

	while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
	{
	}

	/*
	 * HSI = 64 MHz
	 *
	 * PLL input  = HSI / PLLM = 64 / 4 = 16 MHz
	 * PLL VCO    = 16 * PLLN = 16 * 50 = 800 MHz
	 * SYSCLK     = VCO / PLLP = 800 / 2 = 400 MHz
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = 4;
	RCC_OscInitStruct.PLL.PLLN = 50;
	RCC_OscInitStruct.PLL.PLLP = 2;
	RCC_OscInitStruct.PLL.PLLQ = 2;
	RCC_OscInitStruct.PLL.PLLR = 2;
	RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
	RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
	RCC_OscInitStruct.PLL.PLLFRACN = 0;

	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	/*
	 * CPU clock = 400 MHz
	 * AHB clock = 200 MHz
	 * APB clocks = 100 MHz
	 *
	 * This is safer for STM32H7 bus limits.
	 */
	RCC_ClkInitStruct.ClockType =
		RCC_CLOCKTYPE_HCLK  |
		RCC_CLOCKTYPE_SYSCLK |
		RCC_CLOCKTYPE_PCLK1 |
		RCC_CLOCKTYPE_PCLK2 |
		RCC_CLOCKTYPE_D3PCLK1 |
		RCC_CLOCKTYPE_D1PCLK1;

	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
	RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
* @brief GPIO Initialization Function
* @param None
* @retval None
*/
static void MX_GPIO_Init(void)
{
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
* @brief  This function is executed in case of error occurrence.
* @retval None
*/
void Error_Handler(void)
{
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1)
	{
	}
	/* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
* @brief  Reports the name of the source file and the source line number
*         where the assert_param error has occurred.
* @param  file: pointer to the source file name
* @param  line: assert_param error line source number
* @retval None
*/
void assert_failed(uint8_t *file, uint32_t line)
{
	/* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
	ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	/* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
