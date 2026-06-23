
#include "my_led.h"

GPIO_TypeDef  * LED_PORT_ARRAY[3] = {LD1_GPIO_Port, LD2_GPIO_Port, LD3_GPIO_Port};
uint16_t LED_PIN_ARRAY[3] = {LD1_Pin, LD2_Pin, LD3_Pin};

void LED_GPIO_Init(void){
	// led
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();

	/* USER CODE BEGIN MX_GPIO_Init_2 */
	HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	// LD1 and LD3 是同一個 port
	GPIO_InitStruct.Pin = LD1_Pin | LD3_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	// LD2
	GPIO_InitStruct.Pin = LD2_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

void LED_Init(void){
	LED_GPIO_Init();
}

void open_all_led(void){
	for(int i=0; i<3; i++){
		HAL_GPIO_WritePin(LED_PORT_ARRAY[i], LED_PIN_ARRAY[i], GPIO_PIN_SET);
	}
}

void close_all_led(void){
	for(int i=0; i<3; i++){
		HAL_GPIO_WritePin(LED_PORT_ARRAY[i], LED_PIN_ARRAY[i], GPIO_PIN_RESET);
	}
}

void toggle_all_led(void){
	for(int i=0; i<3; i++){
		HAL_GPIO_TogglePin(LED_PORT_ARRAY[i], LED_PIN_ARRAY[i]);
	}
}

void open_single_led(uint8_t LED_ID){
	if(LED_ID > 3 || LED_ID < 1) return;

	HAL_GPIO_WritePin(LED_PORT_ARRAY[LED_ID-1], LED_PIN_ARRAY[LED_ID-1], GPIO_PIN_SET);
}

void close_single_led(uint8_t LED_ID){
	if(LED_ID > 3 || LED_ID < 1) return;

	HAL_GPIO_WritePin(LED_PORT_ARRAY[LED_ID-1], LED_PIN_ARRAY[LED_ID-1], GPIO_PIN_RESET);
}

void toggle_single_led(uint8_t LED_ID){
	if(LED_ID > 3 || LED_ID < 1) return;

	HAL_GPIO_TogglePin(LED_PORT_ARRAY[LED_ID-1], LED_PIN_ARRAY[LED_ID-1]);
}

GPIO_PinState read_led_state(uint8_t LED_ID){
	if(LED_ID > 3 || LED_ID < 1) return GPIO_PIN_RESET;

	return HAL_GPIO_ReadPin(LED_PORT_ARRAY[LED_ID-1], LED_PIN_ARRAY[LED_ID-1]);
}


