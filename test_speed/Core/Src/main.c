/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

CRYP_HandleTypeDef hcryp;
__ALIGN_BEGIN static const uint32_t pKeyCRYP[8] __ALIGN_END = {
                            0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000};
__ALIGN_BEGIN static const uint32_t pInitVectCRYP[4] __ALIGN_END = {
                            0x00000000,0x00000000,0x00000000,0x00000002};
__ALIGN_BEGIN static const uint32_t HeaderCRYP[1] __ALIGN_END = {
                            0x00000000};

HASH_HandleTypeDef hhash;

RNG_HandleTypeDef hrng;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CRYP_Init(void);
static void MX_HASH_Init(void);
static void MX_RNG_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CRYP_Init();
  MX_HASH_Init();
  MX_RNG_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE BEGIN 2 */

  /* 覆蓋掉 CubeMX 預設的 HeaderSize=1（那是 1 word 非零長度的 AAD 佔位值），
     我們這次測試用的是「完全沒有 AAD」，長度要設 0，
     不然會跟 Python 那邊 associated_data=None 的情況對不起來。
     同時把 DataWidthUnit 切成 BYTE，允許非 4 的倍數長度。 */
  hcryp.Init.HeaderSize = 0;
  hcryp.Init.DataType = CRYP_BYTE_SWAP;              /* <-- 加這行 */
  hcryp.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_BYTE;
  hcryp.Init.HeaderWidthUnit = CRYP_HEADERWIDTHUNIT_BYTE;

  if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK)
  {
      Error_Handler();
  }

  /* 33 bytes 明文，"Hello CRYP hardware AES-GCM test!"
     特意選一個不是 4 的倍數的長度，順便驗證 BYTE width unit 有沒有正確處理
     非對齊長度。 */
  __ALIGN_BEGIN static uint8_t test_plaintext[33] __ALIGN_END =
  {
      0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x43, 0x52,
      0x59, 0x50, 0x20, 0x68, 0x61, 0x72, 0x64, 0x77,
      0x61, 0x72, 0x65, 0x20, 0x41, 0x45, 0x53, 0x2d,
      0x47, 0x43, 0x4d, 0x20, 0x74, 0x65, 0x73, 0x74,
      0x21
  };

  __ALIGN_BEGIN static uint8_t test_ciphertext[33] __ALIGN_END = {0};
  __ALIGN_BEGIN static uint8_t test_tag[16] __ALIGN_END = {0};

  HAL_StatusTypeDef enc_status = HAL_CRYP_Encrypt(
      &hcryp,
      (uint32_t *)test_plaintext,
      33,
      (uint32_t *)test_ciphertext,
      HAL_MAX_DELAY
  );

  HAL_StatusTypeDef tag_status = HAL_CRYPEx_AESGCM_GenerateAuthTAG(
      &hcryp,
      (uint32_t *)test_tag,
      HAL_MAX_DELAY
  );

  /* 在這行下中斷點，看 enc_status/tag_status 是不是 HAL_OK，
     再看 test_ciphertext / test_tag 的記憶體內容跟已知答案比對。 */
  volatile int breakpoint_here = (enc_status == HAL_OK && tag_status == HAL_OK) ? 1 : 0;
  (void)breakpoint_here;

  /* USER CODE END 2 */

  /* USER CODE END 2 */

  /*
   * 接在 encrypt 測試後面（同一個 USER CODE BEGIN 2 / END 2 區塊，
   * 或另外開一個中斷點都可以）。用剛剛已經驗證過的 ciphertext/tag
   * 反過來解密，確認 Decrypt 這條路徑跟 Encrypt 一樣正確。
   *
   * 預期結果：test_decrypted 應該要能還原回
   * "Hello CRYP hardware AES-GCM test!" 這 33 bytes。
   */

  /* 已知的 ciphertext/tag（就是剛剛 encrypt 驗證通過的那組結果） */
  __ALIGN_BEGIN static uint8_t known_ciphertext[33] __ALIGN_END =
  {
      0x86, 0xc2, 0x2c, 0x51, 0x22, 0x40, 0x28, 0x3c,
      0x5e, 0x1e, 0xe5, 0xbb, 0xdb, 0x81, 0xf9, 0x6f,
      0x13, 0x12, 0x66, 0xea, 0x76, 0xe3, 0x79, 0x59,
      0x96, 0xe1, 0xb8, 0xae, 0x01, 0x63, 0x46, 0xfa,
      0xfc
  };

  __ALIGN_BEGIN static uint8_t test_decrypted[33] __ALIGN_END = {0};
  __ALIGN_BEGIN static uint8_t test_decrypt_tag[16] __ALIGN_END = {0};

  /* 重新套用同一組設定（key 沒變，IV/counter 也要跟 encrypt 時一樣，
     GCM decrypt 用的是同一個 J0，不是另一組）。 */
  hcryp.Init.HeaderSize = 0;
  hcryp.Init.DataType = CRYP_BYTE_SWAP;
  hcryp.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_BYTE;
  hcryp.Init.HeaderWidthUnit = CRYP_HEADERWIDTHUNIT_BYTE;

  if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK)
  {
      Error_Handler();
  }

  HAL_StatusTypeDef dec_status = HAL_CRYP_Decrypt(
      &hcryp,
      (uint32_t *)known_ciphertext,
      33,
      (uint32_t *)test_decrypted,
      HAL_MAX_DELAY
  );

  HAL_StatusTypeDef dec_tag_status = HAL_CRYPEx_AESGCM_GenerateAuthTAG(
      &hcryp,
      (uint32_t *)test_decrypt_tag,
      HAL_MAX_DELAY
  );

  /* 在這行下中斷點。
   * 1. test_decrypted 這 33 bytes 應該要能拼出 "Hello CRYP hardware AES-GCM test!"
   * 2. test_decrypt_tag 這 16 bytes 應該要等於 ca1307ca5139dab72467638fdc71f846
   *    （硬體解密時不會自動比對 tag，這一步只是「算出」tag，
   *    真正的驗證要靠軟體自己拿這個結果去跟收到的 tag 比對，
   *    這也是等一下要寫進 aes_gcm_lib.c 的邏輯之一）。
   */
  volatile int decrypt_breakpoint_here =
      (dec_status == HAL_OK && dec_tag_status == HAL_OK) ? 1 : 0;
  (void)decrypt_breakpoint_here;


  /*
   * 第二輪已知答案測試——這次用非零的 nonce（跟你正式專案的 nonce 格式一樣：
   * 'S','T','M','3', 0,0,0,0, 0,0,0,1），驗證 IV 欄位的 byte 排列方式對不對。
   * 上一輪測試用全 0 nonce，就算 byte-swap 方向搞錯也看不出差異，這輪才是
   * 真正的考驗。
   *
   * 已知答案：
   *   ciphertext = 55bf0f6acb27157ab2701e5ad8f539a9e8119c8821ef4a32f9591132fb226427a7
   *   tag        = 5da3fc11e6f8200ea8fb4a4c96dbeb80
   */

  /* 用跟正式專案一樣的 nonce 格式：'S','T','M','3' + 8 bytes counter(=1)
     12 bytes 的 nonce 要拆成 3 個 word（不是 2 個！上一版這裡漏掉
     nonce 真正的最後一個 word，錯設成 0，這才是上次結果對不上的原因）：
       word0 = 'S','T','M','3'      = 0x53544d33
       word1 = 0,0,0,0              = 0x00000000
       word2 = 0,0,0,1（nonce 最後4 bytes）= 0x00000001
       word3 = GCM 規格附加的 counter，固定 0x00000002（跟 nonce 內容無關，
                這個之前第一輪全 0 nonce 測試已經驗證過） */
  __ALIGN_BEGIN static uint32_t pInitVectCRYP_v2[4] __ALIGN_END =
  {
      0x53544d33,   /* 'S','T','M','3' */
      0x00000000,
      0x00000001,   /* <-- 修正：上一版這裡誤設成 0x00000000 */
      0x00000002
  };

  hcryp.Init.pInitVect = (uint32_t *)pInitVectCRYP_v2;
  hcryp.Init.HeaderSize = 0;
  hcryp.Init.DataType = CRYP_BYTE_SWAP;
  hcryp.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_BYTE;
  hcryp.Init.HeaderWidthUnit = CRYP_HEADERWIDTHUNIT_BYTE;

  if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK)
  {
      Error_Handler();
  }

  __ALIGN_BEGIN static uint8_t test_plaintext_v2[33] __ALIGN_END =
  {
      0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x43, 0x52,
      0x59, 0x50, 0x20, 0x68, 0x61, 0x72, 0x64, 0x77,
      0x61, 0x72, 0x65, 0x20, 0x41, 0x45, 0x53, 0x2d,
      0x47, 0x43, 0x4d, 0x20, 0x74, 0x65, 0x73, 0x74,
      0x21
  };

  __ALIGN_BEGIN static uint8_t test_ciphertext_v2[33] __ALIGN_END = {0};
  __ALIGN_BEGIN static uint8_t test_tag_v2[16] __ALIGN_END = {0};

  HAL_StatusTypeDef enc_status_v2 = HAL_CRYP_Encrypt(
      &hcryp,
      (uint32_t *)test_plaintext_v2,
      33,
      (uint32_t *)test_ciphertext_v2,
      HAL_MAX_DELAY
  );

  HAL_StatusTypeDef tag_status_v2 = HAL_CRYPEx_AESGCM_GenerateAuthTAG(
      &hcryp,
      (uint32_t *)test_tag_v2,
      HAL_MAX_DELAY
  );

  /* 在這行下中斷點，比對：
   *   test_ciphertext_v2 應該等於 55bf0f6acb27157ab2701e5ad8f539a9e8119c8821ef4a32f9591132fb226427a7
   *   test_tag_v2        應該等於 5da3fc11e6f8200ea8fb4a4c96dbeb80
   */
  volatile int breakpoint_v2 = (enc_status_v2 == HAL_OK && tag_status_v2 == HAL_OK) ? 1 : 0;
  (void)breakpoint_v2;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CRYP Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRYP_Init(void)
{

  /* USER CODE BEGIN CRYP_Init 0 */

  /* USER CODE END CRYP_Init 0 */

  /* USER CODE BEGIN CRYP_Init 1 */

  /* USER CODE END CRYP_Init 1 */
  hcryp.Instance = CRYP;
  hcryp.Init.DataType = CRYP_BYTE_SWAP;
  hcryp.Init.KeySize = CRYP_KEYSIZE_256B;
  hcryp.Init.pKey = (uint32_t *)pKeyCRYP;
  hcryp.Init.pInitVect = (uint32_t *)pInitVectCRYP;
  hcryp.Init.Algorithm = CRYP_AES_GCM;
  hcryp.Init.Header = (uint32_t *)HeaderCRYP;
  hcryp.Init.HeaderSize = 1;
  if (HAL_CRYP_Init(&hcryp) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRYP_Init 2 */

  /* USER CODE END CRYP_Init 2 */

}

/**
  * @brief HASH Initialization Function
  * @param None
  * @retval None
  */
static void MX_HASH_Init(void)
{

  /* USER CODE BEGIN HASH_Init 0 */

  /* USER CODE END HASH_Init 0 */

  /* USER CODE BEGIN HASH_Init 1 */

  /* USER CODE END HASH_Init 1 */
  hhash.Init.DataType = HASH_DATATYPE_32B;
  if (HAL_HASH_Init(&hhash) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN HASH_Init 2 */

  /* USER CODE END HASH_Init 2 */

}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pins : STLINK_RX_Pin STLINK_TX_Pin */
  GPIO_InitStruct.Pin = STLINK_RX_Pin|STLINK_TX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

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
