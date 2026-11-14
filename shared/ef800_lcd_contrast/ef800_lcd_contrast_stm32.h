/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Configure STM32 timer PWM for physical LCD contrast control. */
#ifndef EF800_LCD_CONTRAST_STM32_H
#define EF800_LCD_CONTRAST_STM32_H

#include <stdint.h>

/** @brief Initialize or update 40 kHz TIM4_CH3 PWM output on PB8.
 * @param duty_percent Duty cycle from 0 through 100.
 * @return 0 on update; negative errno if timer/GPIO setup fails.
 */
int ef800_lcd_contrast_stm32_set(uint8_t duty_percent);

/** @brief Stop TIM4_CH3 and return PB8 to a non-driving input state.
 * @return 0 on stop; negative errno if timer/GPIO shutdown fails.
 */
int ef800_lcd_contrast_stm32_stop(void);

#endif /* EF800_LCD_CONTRAST_STM32_H */
