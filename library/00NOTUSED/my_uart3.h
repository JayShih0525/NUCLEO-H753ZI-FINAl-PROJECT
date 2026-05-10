
#ifndef MY_UART3_H_
#define MY_UART3_H_




#include "stm32h7xx_hal.h"
#include <stdarg.h>

extern UART_HandleTypeDef huart3;
extern uint8_t uart3_read_data[256];

extern void Error_Handler(void);

void UART3_GPIO_Init(void);
void UART3_Init(void);
void printf_uart3(const char *format, ...);
uint16_t read_uart3();





#endif /* MY_UART3_H_ */
