/*
 * lcd_touch.h
 *
 *  Created on: Nov 1, 2024
 *      Author: yulin
 */

#ifndef APPLICATION_USER_CORE_LCD_TOUCH_H_
#define APPLICATION_USER_CORE_LCD_TOUCH_H_

#include "stm32h7xx.h"
#include "main.h"
#include "stdbool.h"

/* Private typedef -----------------------------------------------------------*/
typedef struct
{
	ADC_HandleTypeDef *hadc;
	uint32_t XM_ADC_CH;         /*!< Select which ADC channel to monitor X-.
                                   This parameter can be a value of @ref ADC_HAL_EC_CHANNEL. */
	uint32_t YP_ADC_CH;         /*!< Select which ADC channel to monitor Y+.
                                   This parameter can be a value of @ref ADC_HAL_EC_CHANNEL. */
} LCD_Touch_Handle_TypeDef;

/* Private define ------------------------------------------------------------*/
#define TOUCH_SAMPLE_TIMES 4
#define LCD_XP_ADC_CH      ADC_CHANNEL_1
#define LCD_XM_ADC_CH      ADC_CHANNEL_7
#define LCD_YP_ADC_CH      ADC_CHANNEL_2
#define LCD_YM_ADC_CH      ADC_CHANNEL_10

void LCD_Touch_Init(ADC_HandleTypeDef *hadc, uint32_t xm_adc_ch, uint32_t yp_adc_ch);

void LCD_Touch_Get_Position(uint16_t *x, uint16_t *y);

bool LCD_Touch_is_Touching(void);

#endif /* APPLICATION_USER_CORE_LCD_TOUCH_H_ */
