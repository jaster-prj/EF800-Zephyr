/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_LCD_CONTRAST_STORAGE_H
#define EF800_LCD_CONTRAST_STORAGE_H

#include "ef800_lcd_contrast.h"

int ef800_lcd_contrast_storage_load_duty(void *context,
					uint8_t *duty_percent);
int ef800_lcd_contrast_storage_save_duty(void *context,
					uint8_t duty_percent);

#endif /* EF800_LCD_CONTRAST_STORAGE_H */
