
#include <my_keypad_v2.h>

volatile uint8_t keyValue = 0;

static GPIO_TypeDef* 	ROW_PORT[4] = {GPIOE, GPIOE, GPIOE, GPIOD};
static uint16_t 			ROW_PIN[4] = {GPIO_PIN_9, GPIO_PIN_8, GPIO_PIN_7, GPIO_PIN_1};
static GPIO_TypeDef* 	COL_PORT[4] = {GPIOD, GPIOF, GPIOF, GPIOF};
static uint16_t			COL_PIN[4] = {GPIO_PIN_0, GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2};

static void KEYPAD_GPIO_Init(void){
	// Mode: GPIO_MODE_OUTPUT_PP (Push-Pull Output)
	// GPIO 會主動輸出 HIGH 或 LOW：
	// GPIO_PIN_SET   -> 將腳位拉到 HIGH (接近 VCC)
	// GPIO_PIN_RESET -> 將腳位拉到 LOW  (接近 GND)
	// Push-Pull 的意思是 MCU 可以主動「推高」或「拉低」電位，
	// 不會讓腳位處於 floating (0 ~ 1 之間的狀態)。

	// Mode: GPIO_MODE_IT_FALLING (IT => Interrupt)
	// 當 GPIO 訊號從 HIGH → LOW（下降緣 / falling edge）時觸發 interrupt。

	// Pull: GPIO_PULLUP
	// 平時（沒按時）： 內部的上拉電阻像一根「彈簧」，把引腳吊在高電位 (1)。所以你程式讀到的是 SET。
	// 按下時： 按鍵通常會把引腳直接連到 GND (地)。因為「地的力量」比「電阻彈簧」大，引腳會被強行拉到低電位 (0)。

	// Speed: GPIO_SPEED_FREQ_LOW
	// 設定為「低速」頻率：減緩電壓爬升坡度，減少電磁干擾(EMI)與功耗，提升訊號穩定度

	// 將 column 預設輸出為 LOW (RESET)
	// 原因：row 腳位設定為 GPIO_MODE_IT_FALLING + GPIO_PULLUP
	// row 平常會被內部 pull-up 電阻拉成 HIGH
	// 當按下 keypad 時，row 會透過按鍵與 column 相連
	// 如果 column 是 LOW，row 會被拉成 LOW
	// 這樣就會產生 HIGH → LOW 的變化 (falling edge)
	// falling edge 會觸發 GPIO interrupt

	// 如果 column 設成 HIGH (SET)
	// 按下按鍵時 row 仍然會保持 HIGH
	// 不會產生 falling edge，也就不會觸發 interrupt
	// 因此 column 必須預設為 RESET (LOW)


	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOF_CLK_ENABLE();

	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	// col0
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);
	GPIO_InitStruct.Pin =  GPIO_PIN_0;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	// col1 ~ col3
	HAL_GPIO_WritePin(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_RESET);
	GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

	// row0 ~ row2
	GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

	// row3
	GPIO_InitStruct.Pin = GPIO_PIN_1;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	// EXTI9_5_IRQn => External Line[9:5] Interrupts =>
	// External Interrupt Line 5 ~ 9 => Px5 ~ Px9 => PE7, PE8, PE9
	HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
	HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

	// EXTI1_IRQn => EXTI Line1 Interrupt =>
	// External Interrupt Line 1 => Px1 => PD1
	HAL_NVIC_SetPriority(EXTI1_IRQn, 2, 0);
	HAL_NVIC_EnableIRQ(EXTI1_IRQn);
}

static void this_HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
	int8_t which_row = -1;
	for(int8_t row=0; row<4; row++){
		if(GPIO_Pin == ROW_PIN[row]){
			which_row = row;
		}
	}
	if(which_row == -1) return;

	for(uint8_t col=0; col<4; col++){
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET);
		HAL_GPIO_WritePin(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_SET);

		HAL_GPIO_WritePin(COL_PORT[col], COL_PIN[col], GPIO_PIN_RESET);
		while(HAL_GPIO_ReadPin(COL_PORT[col], COL_PIN[col]) != GPIO_PIN_RESET){}

		if(HAL_GPIO_ReadPin(ROW_PORT[which_row], ROW_PIN[which_row]) == GPIO_PIN_RESET){
			keyValue = which_row * 4 + col + 1;
			break;
		}
	}

	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_RESET);

	__HAL_GPIO_EXTI_CLEAR_IT(GPIO_Pin);
}

static void this_HAL_GPIO_EXTI_IRQHandler(uint16_t GPIO_Pin){
	#if defined(DUAL_CORE) && defined(CORE_CM4)
		if (__HAL_GPIO_EXTID2_GET_IT(GPIO_Pin) != 0x00U){
			__HAL_GPIO_EXTID2_CLEAR_IT(GPIO_Pin);
			this_HAL_GPIO_EXTI_Callback(GPIO_Pin);
		}
	#else
		// EXTI line interrupt detected
		if (__HAL_GPIO_EXTI_GET_IT(GPIO_Pin) != 0x00U){
			__HAL_GPIO_EXTI_CLEAR_IT(GPIO_Pin);
			this_HAL_GPIO_EXTI_Callback(GPIO_Pin);
		}
	#endif
}

void EXTI1_IRQHandler(void){
	this_HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
}

void EXTI9_5_IRQHandler(void){
	this_HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);
	this_HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
	this_HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);
}

void KEYPAD_Init(void){
	KEYPAD_GPIO_Init();
}
