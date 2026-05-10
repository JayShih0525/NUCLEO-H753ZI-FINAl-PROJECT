
#ifndef MY_KEYPAD_H_
#define MY_KEYPAD_H_

#include "stm32h7xx_hal.h"


extern void Error_Handler(void);

void KEYPAD_GPIO_Init(void);
void KEYPAD_Init(void);
uint8_t read_keypad(void);


#endif /* MY_KEYPAD_H_ */
