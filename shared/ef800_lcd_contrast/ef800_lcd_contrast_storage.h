/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Adapt LCD contrast calibration persistence to contrast callbacks. */
#ifndef EF800_LCD_CONTRAST_STORAGE_H
#define EF800_LCD_CONTRAST_STORAGE_H

#include "ef800_lcd_contrast.h"

/** @brief Load the persisted contrast duty percentage.
 * @param context Persistence service used by the adapter.
 * @param duty_percent Receives the saved percentage.
 * @return 0 on load; negative errno when absent, invalid, or unreadable.
 */
int ef800_lcd_contrast_storage_load_duty(void *context,
					uint8_t *duty_percent);

/** @brief Persist the selected contrast duty percentage.
 * @param context Persistence service used by the adapter.
 * @param duty_percent Duty percentage to store.
 * @return 0 on save; negative errno on validation or storage failure.
 */
int ef800_lcd_contrast_storage_save_duty(void *context,
					uint8_t duty_percent);

#endif /* EF800_LCD_CONTRAST_STORAGE_H */
