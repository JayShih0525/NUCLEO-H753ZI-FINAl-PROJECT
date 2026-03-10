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

bool rotary_init(rotary_handle_t* handle, uint32_t pin_a, uint32_t pin_b, void (*callback)(rotary_dir_e dir));

void rotary_set_callback(rotary_handle_t *handle,void (*fn)(rotary_dir_e dir));

void rotary_reset(rotary_handle_t *handle);

void rotary_event(rotary_handle_t *handle,uint32_t pin);

/* Private defines -----------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* __ROTARY_H */

