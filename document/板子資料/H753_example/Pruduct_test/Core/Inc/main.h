/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LCD_DC_Pin GPIO_PIN_3
#define LCD_DC_GPIO_Port GPIOE
#define W25_NSS_Pin GPIO_PIN_4
#define W25_NSS_GPIO_Port GPIOE
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define B1_EXTI_IRQn EXTI15_10_IRQn
#define SDMMC1_CD_Pin GPIO_PIN_6
#define SDMMC1_CD_GPIO_Port GPIOF
#define SegS2_Pin GPIO_PIN_10
#define SegS2_GPIO_Port GPIOF
#define LCD_RST_Pin GPIO_PIN_3
#define LCD_RST_GPIO_Port GPIOC
#define SegB_Pin GPIO_PIN_3
#define SegB_GPIO_Port GPIOA
#define LD1_Pin GPIO_PIN_0
#define LD1_GPIO_Port GPIOB
#define exB0_Pin GPIO_PIN_1
#define exB0_GPIO_Port GPIOB
#define exB0_EXTI_IRQn EXTI1_IRQn
#define exB1_Pin GPIO_PIN_2
#define exB1_GPIO_Port GPIOB
#define exB1_EXTI_IRQn EXTI2_IRQn
#define SegH_Pin GPIO_PIN_11
#define SegH_GPIO_Port GPIOF
#define SegE_Pin GPIO_PIN_11
#define SegE_GPIO_Port GPIOE
#define SegF_Pin GPIO_PIN_12
#define SegF_GPIO_Port GPIOE
#define Buzzer_Pin GPIO_PIN_13
#define Buzzer_GPIO_Port GPIOE
#define SegA_Pin GPIO_PIN_14
#define SegA_GPIO_Port GPIOE
#define SegS0_Pin GPIO_PIN_15
#define SegS0_GPIO_Port GPIOE
#define RoteryA_Pin GPIO_PIN_10
#define RoteryA_GPIO_Port GPIOB
#define RoteryA_EXTI_IRQn EXTI15_10_IRQn
#define ESP_EN_Pin GPIO_PIN_11
#define ESP_EN_GPIO_Port GPIOB
#define LD3_Pin GPIO_PIN_14
#define LD3_GPIO_Port GPIOB
#define RoteryB_Pin GPIO_PIN_15
#define RoteryB_GPIO_Port GPIOB
#define RoteryB_EXTI_IRQn EXTI15_10_IRQn
#define STLINK_RX_Pin GPIO_PIN_8
#define STLINK_RX_GPIO_Port GPIOD
#define STLINK_TX_Pin GPIO_PIN_9
#define STLINK_TX_GPIO_Port GPIOD
#define USB_OTG_FS_PWR_EN_Pin GPIO_PIN_10
#define USB_OTG_FS_PWR_EN_GPIO_Port GPIOD
#define SegS1_Pin GPIO_PIN_11
#define SegS1_GPIO_Port GPIOD
#define SegG_Pin GPIO_PIN_5
#define SegG_GPIO_Port GPIOG
#define SegS3_Pin GPIO_PIN_6
#define SegS3_GPIO_Port GPIOG
#define USB_OTG_FS_OVCR_Pin GPIO_PIN_7
#define USB_OTG_FS_OVCR_GPIO_Port GPIOG
#define USB_OTG_FS_OVCR_EXTI_IRQn EXTI9_5_IRQn
#define LCD_CTRL_Pin GPIO_PIN_3
#define LCD_CTRL_GPIO_Port GPIOD
#define SegD_Pin GPIO_PIN_14
#define SegD_GPIO_Port GPIOG
#define RoteryBtn_Pin GPIO_PIN_5
#define RoteryBtn_GPIO_Port GPIOB
#define RoteryBtn_EXTI_IRQn EXTI9_5_IRQn
#define LCD_CS_Pin GPIO_PIN_7
#define LCD_CS_GPIO_Port GPIOB
#define SegC_Pin GPIO_PIN_0
#define SegC_GPIO_Port GPIOE
#define LD2_Pin GPIO_PIN_1
#define LD2_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
#define LCD_XM_Pin GPIO_PIN_8
#define LCD_XM_GPIO_Port GPIOF
#define LCD_YP_Pin GPIO_PIN_9
#define LCD_YP_GPIO_Port GPIOF
#define LCD_YM_Pin GPIO_PIN_0
#define LCD_YM_GPIO_Port GPIOC
#define LCD_XP_Pin GPIO_PIN_3
#define LCD_XP_GPIO_Port GPIOC
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
