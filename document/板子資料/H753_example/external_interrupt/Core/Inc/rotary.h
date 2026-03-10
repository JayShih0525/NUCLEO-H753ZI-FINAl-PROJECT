/**
 ******************************************************************************
 * @file           : rotary.h
 * @brief          : Header for rotary.c file.
 ******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __ROTARY_H
#define __ROTARY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

/* Private includes ----------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
/* Exported types ------------------------------------------------------------*/

typedef enum {
	ROTARY_DIR_A, ROTARY_DIR_B
} rotary_dir_e;

typedef struct rotary_handle {
	uint32_t pin_flag;
	uint32_t a_pin, b_pin;
	void (*callback)(rotary_dir_e dir);
} rotary_handle_t;

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/*
 * 初始化旋轉編碼器的句柄
 * param:
 * handle 句柄
 * pin_a 旋轉編碼器的GPIO_PIN編號
 * pin_b 旋轉編碼器的GPIO_PIN編號
 * callback 事件發生時的回調
 */
bool rotary_init(rotary_handle_t* handle, uint32_t pin_a, uint32_t pin_b, void (*callback)(rotary_dir_e dir));

/*
 *綁定事件發生時呼叫的函式，綁定的函式有一個傳入值dir ，可能為rotary_dir_e 類型的ROTARY_DIR_A 或ROTARY_DIR_B，用於判斷事件類型。
 *
 */
void rotary_bind_callback(rotary_handle_t *handle,void (*fn)(rotary_dir_e dir));
/*
 * 重置旋轉編碼器的事件
 */
void rotary_reset(rotary_handle_t *handle);
/*
 *  驅動旋轉編碼器事件的函式，必須被週期的呼叫。
 */
void rotary_event(rotary_handle_t *handle,uint32_t pin);

/* Private defines -----------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* __ROTARY_H */

