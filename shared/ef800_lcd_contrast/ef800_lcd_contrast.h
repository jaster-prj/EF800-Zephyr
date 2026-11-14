/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Manage LCD contrast mode, calibration, and PWM conversion. */
#ifndef EF800_LCD_CONTRAST_H
#define EF800_LCD_CONTRAST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stddef.h>

#include <zephyr/kernel.h>

#define EF800_LCD_CONTRAST_DEFAULT_PERCENT 78U
#define EF800_LCD_CONTRAST_MIN_PERCENT 0U
#define EF800_LCD_CONTRAST_MAX_PERCENT 100U
#define EF800_LCD_CONTRAST_PWM_FREQUENCY_HZ 40000U

enum ef800_lcd_contrast_mode {
	EF800_LCD_CONTRAST_STATIC = 0,
	EF800_LCD_CONTRAST_PWM = 1,
};

struct ef800_lcd_contrast_ops {
	int (*load_duty)(void *context, uint8_t *duty_percent);
	int (*save_duty)(void *context, uint8_t duty_percent);
	int (*set_pwm)(void *context, uint8_t duty_percent);
	void (*state_changed)(void *context,
		enum ef800_lcd_contrast_mode mode, uint8_t duty_percent);
	void (*diagnostic)(void *context, const char *message, int error);
};

struct ef800_lcd_contrast {
	struct k_mutex lock;
	struct ef800_lcd_contrast_ops ops;
	void *context;
	enum ef800_lcd_contrast_mode mode;
	uint8_t duty_percent;
	bool initialized;
};

/** @brief Initialize contrast state and load the saved duty setting.
 * @param contrast Service state to initialize.
 * @param mode Static or PWM contrast mode.
 * @param ops Persistence, PWM, notification, and diagnostic callbacks.
 * @param context Opaque argument passed to callbacks.
 * @return 0 on initialization; negative errno for invalid mode or setup failure.
 */
int ef800_lcd_contrast_init(struct ef800_lcd_contrast *contrast,
			    enum ef800_lcd_contrast_mode mode,
			    const struct ef800_lcd_contrast_ops *ops,
			    void *context);

/** @brief Validate, apply, and persist a contrast duty percentage.
 * @param contrast Initialized contrast service.
 * @param duty_percent Requested duty from 0 through 100.
 * @return 0 on update; negative errno on invalid value or adapter failure.
 */
int ef800_lcd_contrast_set_duty(struct ef800_lcd_contrast *contrast,
				uint8_t duty_percent);

/** @brief Read the active mode and duty under the service lock.
 * @param contrast Initialized contrast service.
 * @param mode Receives active mode.
 * @param duty_percent Receives duty percentage.
 * @return 0 on read; negative errno for invalid arguments/state.
 */
int ef800_lcd_contrast_get(struct ef800_lcd_contrast *contrast,
			   enum ef800_lcd_contrast_mode *mode,
			   uint8_t *duty_percent);

/** @brief Parse bounded decimal text as a contrast percentage.
 * @param text Input characters.
 * @param length Character count.
 * @param duty_percent Receives value from 0 through 100.
 * @return 0 on parse; negative errno for malformed or out-of-range text.
 */
int ef800_lcd_contrast_parse_duty(const char *text, size_t length,
				  uint8_t *duty_percent);

/** @brief Convert timer frequency and duty into PWM period/pulse counts.
 * @param timer_hz Timer input frequency.
 * @param duty_percent Requested contrast duty.
 * @param period_counts Receives period count.
 * @param pulse_counts Receives active pulse count.
 * @return 0 on calculation; negative errno for invalid frequency/duty.
 */
int ef800_lcd_contrast_calculate_counts(uint32_t timer_hz,
					uint8_t duty_percent,
					uint32_t *period_counts,
					uint32_t *pulse_counts);

/** @brief Return menu item count for a mode and fixed visible-item count.
 * @param mode Contrast mode.
 * @param always_visible_count Number of items shown in every mode.
 * @return Total settings count for the selected mode.
 */
size_t ef800_lcd_contrast_settings_count(enum ef800_lcd_contrast_mode mode,
					size_t always_visible_count);

#endif /* EF800_LCD_CONTRAST_H */
