
#include <my_keypad.h>

GPIO_TypeDef* ROW_PORT[4] = {GPIOE, GPIOE, GPIOE, GPIOD};
uint16_t ROW_PIN[4] = {GPIO_PIN_9, GPIO_PIN_8, GPIO_PIN_7, GPIO_PIN_1};

GPIO_TypeDef* COL_PORT[4] = {GPIOD, GPIOF, GPIOF, GPIOF};
uint16_t COL_PIN[4] = {GPIO_PIN_0, GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2};

void KEYPAD_GPIO_Init(void){
	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOF_CLK_ENABLE();

	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	// row0 ~ row2
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9 | GPIO_PIN_8 | GPIO_PIN_7, GPIO_PIN_SET);
	GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

	// row3
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_SET);
	GPIO_InitStruct.Pin = GPIO_PIN_1;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	// col0
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);
	GPIO_InitStruct.Pin =  GPIO_PIN_0;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	// col1 ~ col3
	HAL_GPIO_WritePin(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_RESET);
	GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
}

void KEYPAD_Init(void){
	KEYPAD_GPIO_Init();
}


uint8_t read_keypad(void){
	for(int row=0; row<4; row++){
		// 所有 row -> high
		HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9 | GPIO_PIN_8 | GPIO_PIN_7, GPIO_PIN_SET);
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_SET);

		// 選擇 row -> low
		HAL_GPIO_WritePin(ROW_PORT[row], ROW_PIN[row], GPIO_PIN_RESET);
		while(HAL_GPIO_ReadPin(ROW_PORT[row], ROW_PIN[row]) != GPIO_PIN_RESET){}

		for(int col=0; col<4; col++){
			GPIO_PinState state;

			state = HAL_GPIO_ReadPin(COL_PORT[col], COL_PIN[col]);

			if(state == GPIO_PIN_RESET){
				return row * 4 + col + 1;
			}
		}
	}

	return 0;
}
