/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_LCD_CONTRAST_STM32_H
#define EF800_LCD_CONTRAST_STM32_H

#include <stdint.h>

/* Initialize/update 40 kHz TIM4_CH3 output on PB8. */
int ef800_lcd_contrast_stm32_set(uint8_t duty_percent);

/* Stop TIM4_CH3 and return PB8 to a non-driving GPIO input state. */
int ef800_lcd_contrast_stm32_stop(void);

#endif /* EF800_LCD_CONTRAST_STM32_H */
