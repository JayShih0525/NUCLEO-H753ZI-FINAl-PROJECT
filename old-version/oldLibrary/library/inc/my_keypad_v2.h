
#ifndef MY_KEYPAD_V2_H_
#define MY_KEYPAD_V2_H_

#include "stm32h7xx_hal.h"

extern volatile uint8_t keyValue;
extern void Error_Handler(void);
void KEYPAD_V2_Init(void);


#endif /* MY_KEYPAD_V2_H_ */
