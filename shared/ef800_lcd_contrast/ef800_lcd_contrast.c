/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_lcd_contrast.h"

#include <errno.h>
#include <string.h>

static void report(struct ef800_lcd_contrast *contrast, const char *message,
		   int error)
{
	if (contrast->ops.diagnostic != NULL) {
		contrast->ops.diagnostic(contrast->context, message, error);
	}
}

static void publish_state(struct ef800_lcd_contrast *contrast)
{
	if (contrast->ops.state_changed != NULL) {
		contrast->ops.state_changed(contrast->context, contrast->mode,
			contrast->duty_percent);
	}
}

static bool duty_valid(uint8_t duty_percent)
{
	return duty_percent <= EF800_LCD_CONTRAST_MAX_PERCENT;
}

static int load_duty_or_default(struct ef800_lcd_contrast *contrast,
				uint8_t *duty_percent)
{
	int ret = contrast->ops.load_duty(contrast->context, duty_percent);

	if (ret == -ENOENT) {
		*duty_percent = EF800_LCD_CONTRAST_DEFAULT_PERCENT;
		ret = contrast->ops.save_duty(contrast->context, *duty_percent);
		if (ret != 0) {
			report(contrast, "default LCD contrast could not be persisted", ret);
		}
		return 0;
	}
	if (ret != 0 || !duty_valid(*duty_percent)) {
		int error = ret != 0 ? ret : -ERANGE;

		*duty_percent = EF800_LCD_CONTRAST_DEFAULT_PERCENT;
		report(contrast, "invalid LCD contrast value; using 78 percent", error);
		return 0;
	}
	return 0;
}

int ef800_lcd_contrast_parse_duty(const char *text, size_t length,
				  uint8_t *duty_percent)
{
	unsigned int parsed = 0U;

	if (text == NULL || duty_percent == NULL || length == 0U || length > 3U) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < length; ++index) {
		if (text[index] < '0' || text[index] > '9') {
			return -EBADMSG;
		}
		parsed = parsed * 10U + (unsigned int)(text[index] - '0');
	}
	if (parsed > EF800_LCD_CONTRAST_MAX_PERCENT) {
		return -ERANGE;
	}
	*duty_percent = (uint8_t)parsed;
	return 0;
}

int ef800_lcd_contrast_calculate_counts(uint32_t timer_hz,
					uint8_t duty_percent,
					uint32_t *period_counts,
					uint32_t *pulse_counts)
{
	uint64_t period;

	if (period_counts == NULL || pulse_counts == NULL ||
	    duty_percent > EF800_LCD_CONTRAST_MAX_PERCENT ||
	    timer_hz < EF800_LCD_CONTRAST_PWM_FREQUENCY_HZ ||
	    timer_hz % EF800_LCD_CONTRAST_PWM_FREQUENCY_HZ != 0U) {
		return -ERANGE;
	}
	period = timer_hz / EF800_LCD_CONTRAST_PWM_FREQUENCY_HZ;
	if (period < 2U || period > (uint64_t)UINT16_MAX + 1U) {
		return -ERANGE;
	}
	*period_counts = (uint32_t)period;
	*pulse_counts = (uint32_t)((period * duty_percent + 50U) / 100U);
	return 0;
}

size_t ef800_lcd_contrast_settings_count(enum ef800_lcd_contrast_mode mode,
					size_t always_visible_count)
{
	return always_visible_count + (mode == EF800_LCD_CONTRAST_PWM ? 1U : 0U);
}

int ef800_lcd_contrast_init(struct ef800_lcd_contrast *contrast,
			    enum ef800_lcd_contrast_mode mode,
			    const struct ef800_lcd_contrast_ops *ops,
			    void *context)
{
	int ret;

	if (contrast == NULL || ops == NULL || ops->load_duty == NULL ||
	    ops->save_duty == NULL || ops->set_pwm == NULL ||
	    (mode != EF800_LCD_CONTRAST_STATIC && mode != EF800_LCD_CONTRAST_PWM)) {
		return -EINVAL;
	}
	memset(contrast, 0, sizeof(*contrast));
	k_mutex_init(&contrast->lock);
	contrast->ops = *ops;
	contrast->context = context;
	contrast->mode = mode;
	contrast->duty_percent = EF800_LCD_CONTRAST_DEFAULT_PERCENT;
	contrast->initialized = true;

	if (mode == EF800_LCD_CONTRAST_PWM) {
		(void)load_duty_or_default(contrast, &contrast->duty_percent);
		ret = contrast->ops.set_pwm(contrast->context, contrast->duty_percent);
		if (ret != 0) {
			report(contrast, "LCD contrast PWM initialization failed", ret);
			publish_state(contrast);
			return ret;
		}
	}
	publish_state(contrast);
	return 0;
}

int ef800_lcd_contrast_set_duty(struct ef800_lcd_contrast *contrast,
				uint8_t duty_percent)
{
	uint8_t previous;
	int ret;

	if (contrast == NULL || !contrast->initialized || !duty_valid(duty_percent)) {
		return -EINVAL;
	}
	k_mutex_lock(&contrast->lock, K_FOREVER);
	if (contrast->mode != EF800_LCD_CONTRAST_PWM) {
		k_mutex_unlock(&contrast->lock);
		return -EACCES;
	}
	previous = contrast->duty_percent;
	ret = contrast->ops.set_pwm(contrast->context, duty_percent);
	if (ret != 0) {
		k_mutex_unlock(&contrast->lock);
		return ret;
	}
	ret = contrast->ops.save_duty(contrast->context, duty_percent);
	if (ret != 0) {
		int rollback = contrast->ops.set_pwm(contrast->context, previous);

		if (rollback != 0) {
			report(contrast, "LCD contrast PWM rollback failed", rollback);
		}
		k_mutex_unlock(&contrast->lock);
		return ret;
	}
	contrast->duty_percent = duty_percent;
	publish_state(contrast);
	k_mutex_unlock(&contrast->lock);
	return 0;
}

int ef800_lcd_contrast_get(struct ef800_lcd_contrast *contrast,
			   enum ef800_lcd_contrast_mode *mode,
			   uint8_t *duty_percent)
{
	if (contrast == NULL || !contrast->initialized || mode == NULL ||
	    duty_percent == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&contrast->lock, K_FOREVER);
	*mode = contrast->mode;
	*duty_percent = contrast->duty_percent;
	k_mutex_unlock(&contrast->lock);
	return 0;
}
