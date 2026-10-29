/* SPDX-License-Identifier: Apache-2.0 */
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

int ef800_lcd_contrast_init(struct ef800_lcd_contrast *contrast,
			    enum ef800_lcd_contrast_mode mode,
			    const struct ef800_lcd_contrast_ops *ops,
			    void *context);
int ef800_lcd_contrast_set_duty(struct ef800_lcd_contrast *contrast,
				uint8_t duty_percent);
int ef800_lcd_contrast_get(struct ef800_lcd_contrast *contrast,
			   enum ef800_lcd_contrast_mode *mode,
			   uint8_t *duty_percent);

int ef800_lcd_contrast_parse_duty(const char *text, size_t length,
				  uint8_t *duty_percent);
int ef800_lcd_contrast_calculate_counts(uint32_t timer_hz,
					uint8_t duty_percent,
					uint32_t *period_counts,
					uint32_t *pulse_counts);
size_t ef800_lcd_contrast_settings_count(enum ef800_lcd_contrast_mode mode,
					size_t always_visible_count);

#endif /* EF800_LCD_CONTRAST_H */
