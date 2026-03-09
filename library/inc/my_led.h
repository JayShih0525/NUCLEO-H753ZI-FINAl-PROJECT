
#ifndef MY_LED_H_
#define MY_LED_H_

#include "stm32h7xx_hal.h"

#define LD1_Pin GPIO_PIN_0
#define LD1_GPIO_Port GPIOB
#define LD2_Pin GPIO_PIN_1
#define LD2_GPIO_Port GPIOE
#define LD3_Pin GPIO_PIN_14
#define LD3_GPIO_Port GPIOB

extern void Error_Handler(void);

void LED_GPIO_Init(void);
void LED_Init(void);
void open_all_led(void);
void close_all_led(void);
void toggle_all_led(void);
void open_single_led(uint8_t LED_ID);
void close_single_led(uint8_t LED_ID);
void toggle_single_led(uint8_t LED_ID);
GPIO_PinState read_led_state(uint8_t LED_ID);


#endif /* MY_LED_H_ */
