/*
 * lcd_touch.c
 *
 *  Created on: Nov 1, 2024
 *      Author: yulin
 */


#include "lcd_touch.h"

#define NOT_IN_TESTING 1
#define WHILE_DELETE_AFTER_TEST 0

/* Private variables ---------------------------------------------------------*/
LCD_Touch_Handle_TypeDef lcd_handle;

#define X_MIN        0x1E00
#define X_MAX        0xED00
#define X_DIFF       (Y_MAX - Y_MIN)

#define Y_MIN        0x1600
#define Y_MAX        0xF800
#define Y_DIFF       (Y_MAX - Y_MIN)

static void LCD_SetPin_InputPullup(GPIO_TypeDef *GPIOx, uint16_t Pin)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Pin = Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

static void LCD_SetPin_InputFloating(GPIO_TypeDef *GPIOx, uint16_t Pin)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Pin = Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

static void LCD_SetPin_OutputPP(GPIO_TypeDef *GPIOx, uint16_t Pin, GPIO_PinState state)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	HAL_GPIO_WritePin(GPIOx, Pin, state);

	GPIO_InitStruct.Pin = Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

static void LCD_SetPin_Analog(uint32_t channel, GPIO_TypeDef *GPIOx, uint16_t Pin)
{
  ADC_ChannelConfTypeDef sConfig = {0};
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	sConfig.Channel = channel;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
	HAL_ADC_ConfigChannel(lcd_handle.hadc, &sConfig);

	GPIO_InitStruct.Pin = Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

static void LCD_Touch_Set_Standby(void)
{
	/* Set bias then pull-up pin wait falling edge */
	LCD_SetPin_OutputPP(LCD_XP_GPIO_Port, LCD_XP_Pin, GPIO_PIN_RESET);
	LCD_SetPin_InputFloating(LCD_XM_GPIO_Port, LCD_XM_Pin);
	LCD_SetPin_InputFloating(LCD_YP_GPIO_Port, LCD_YP_Pin);
	LCD_SetPin_InputPullup(LCD_YM_GPIO_Port, LCD_YM_Pin);
}

void LCD_Touch_Init(ADC_HandleTypeDef *hadc, uint32_t xm_adc_ch, uint32_t yp_adc_ch)
{
	lcd_handle.hadc = hadc;
	lcd_handle.XM_ADC_CH = xm_adc_ch;
	lcd_handle.YP_ADC_CH = yp_adc_ch;

	LCD_Touch_Set_Standby();
}

void LCD_Touch_Get_Position(uint16_t *x, uint16_t *y)
{
	int i;
	uint32_t touch_sum;

	/* Set bias for getting X position */
	LCD_SetPin_OutputPP(LCD_XP_GPIO_Port, LCD_XP_Pin, GPIO_PIN_SET);
	LCD_SetPin_OutputPP(LCD_XM_GPIO_Port, LCD_XM_Pin, GPIO_PIN_RESET);
	LCD_SetPin_Analog(lcd_handle.YP_ADC_CH, LCD_YP_GPIO_Port, LCD_YP_Pin);
	LCD_SetPin_InputFloating(LCD_YM_GPIO_Port, LCD_YM_Pin);

	//HAL_Delay(10);

	/* Get X touch ADC value */
	touch_sum = 0;
	for(i = 0; i < TOUCH_SAMPLE_TIMES; i++)
	{
		HAL_ADC_Start(lcd_handle.hadc);
		HAL_ADC_PollForConversion(lcd_handle.hadc, 100);
		touch_sum += HAL_ADC_GetValue(lcd_handle.hadc);
	}
	touch_sum = touch_sum / TOUCH_SAMPLE_TIMES;

	/* Get X Position */
	//*x = (touch_sum - X_MIN) * 240 / X_DIFF;
	*x = (X_MAX - touch_sum) * 240 / X_DIFF;

	/* Set bias for getting Y position */
	LCD_SetPin_InputFloating(LCD_XP_GPIO_Port, LCD_XP_Pin);
	LCD_SetPin_Analog(lcd_handle.XM_ADC_CH, LCD_XM_GPIO_Port, LCD_XM_Pin);
	LCD_SetPin_OutputPP(LCD_YP_GPIO_Port, LCD_YP_Pin, GPIO_PIN_RESET);
	LCD_SetPin_OutputPP(LCD_YM_GPIO_Port, LCD_YM_Pin, GPIO_PIN_SET);

	//HAL_Delay(10);

	/* Get Y touch ADC value */
	touch_sum = 0;
	for(i = 0; i < TOUCH_SAMPLE_TIMES; i++)
	{
		HAL_ADC_Start(lcd_handle.hadc);
		HAL_ADC_PollForConversion(lcd_handle.hadc, 100);
		touch_sum += HAL_ADC_GetValue(lcd_handle.hadc);
	}
	touch_sum = touch_sum / TOUCH_SAMPLE_TIMES;

	/* Get Y Position */
	//*y = (touch_sum - Y_MIN) * 320 / Y_DIFF;
	*y = (Y_MAX - touch_sum) *320 / Y_DIFF;

	LCD_Touch_Set_Standby();
}

/** @note Detect by interrupt while be triggered when initial */
bool LCD_Touch_is_Touching(void)
{
	if(HAL_GPIO_ReadPin(LCD_YM_GPIO_Port, LCD_YM_Pin) == GPIO_PIN_RESET)
		return true;
	else
		return false;
}
