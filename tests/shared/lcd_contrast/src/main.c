/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include "ef800_lcd_contrast.h"

struct contrast_fixture {
	int load_result;
	uint8_t stored_duty;
	int save_result;
	int pwm_result;
	uint8_t applied_duty;
	unsigned int pwm_calls;
	unsigned int save_calls;
	unsigned int state_calls;
	unsigned int diagnostic_calls;
	enum ef800_lcd_contrast_mode published_mode;
	uint8_t published_duty;
};

static struct ef800_lcd_contrast contrast;
static struct contrast_fixture fixture;

static int fake_load_duty(void *context, uint8_t *duty_percent)
{
	struct contrast_fixture *fake = context;

	if (fake->load_result == 0) {
		*duty_percent = fake->stored_duty;
	}
	return fake->load_result;
}

static int fake_save_duty(void *context, uint8_t duty_percent)
{
	struct contrast_fixture *fake = context;

	++fake->save_calls;
	if (fake->save_result != 0) {
		return fake->save_result;
	}
	fake->stored_duty = duty_percent;
	fake->load_result = 0;
	return 0;
}

static int fake_set_pwm(void *context, uint8_t duty_percent)
{
	struct contrast_fixture *fake = context;

	++fake->pwm_calls;
	if (fake->pwm_result != 0) {
		return fake->pwm_result;
	}
	fake->applied_duty = duty_percent;
	return 0;
}

static void fake_state_changed(void *context,
			       enum ef800_lcd_contrast_mode mode,
			       uint8_t duty_percent)
{
	struct contrast_fixture *fake = context;

	++fake->state_calls;
	fake->published_mode = mode;
	fake->published_duty = duty_percent;
}

static void fake_diagnostic(void *context, const char *message, int error)
{
	struct contrast_fixture *fake = context;

	ARG_UNUSED(message);
	ARG_UNUSED(error);
	++fake->diagnostic_calls;
}

static const struct ef800_lcd_contrast_ops ops = {
	.load_duty = fake_load_duty,
	.save_duty = fake_save_duty,
	.set_pwm = fake_set_pwm,
	.state_changed = fake_state_changed,
	.diagnostic = fake_diagnostic,
};

static void *contrast_setup(void)
{
	return &fixture;
}

static void contrast_before(void *data)
{
	ARG_UNUSED(data);
	fixture = (struct contrast_fixture){
		.load_result = -ENOENT,
		.stored_duty = 0U,
	};
}

ZTEST_SUITE(lcd_contrast, NULL, contrast_setup, contrast_before, NULL, NULL);

ZTEST(lcd_contrast, test_parse_duty_bounds_and_reject_malformed_values)
{
	uint8_t duty = 0U;

	zassert_ok(ef800_lcd_contrast_parse_duty("0", 1U, &duty), NULL);
	zassert_equal(duty, 0U, NULL);
	zassert_ok(ef800_lcd_contrast_parse_duty("100", 3U, &duty), NULL);
	zassert_equal(duty, 100U, NULL);
	zassert_equal(ef800_lcd_contrast_parse_duty("101", 3U, &duty), -ERANGE, NULL);
	zassert_equal(ef800_lcd_contrast_parse_duty("7x", 2U, &duty), -EBADMSG, NULL);
	zassert_equal(ef800_lcd_contrast_parse_duty("", 0U, &duty), -EINVAL, NULL);
}

ZTEST(lcd_contrast, test_40khz_counts_match_the_ef800_timer_clock)
{
	uint32_t period_counts;
	uint32_t pulse_counts;

	zassert_ok(ef800_lcd_contrast_calculate_counts(80000000U, 78U,
		&period_counts, &pulse_counts), NULL);
	zassert_equal(period_counts, 2000U, NULL);
	zassert_equal(pulse_counts, 1560U, NULL);
	zassert_ok(ef800_lcd_contrast_calculate_counts(80000000U, 0U,
		&period_counts, &pulse_counts), NULL);
	zassert_equal(pulse_counts, 0U, NULL);
	zassert_ok(ef800_lcd_contrast_calculate_counts(80000000U, 100U,
		&period_counts, &pulse_counts), NULL);
	zassert_equal(pulse_counts, period_counts, NULL);
}

ZTEST(lcd_contrast, test_settings_entry_is_counted_only_when_composition_enables_it)
{
	zassert_equal(ef800_lcd_contrast_settings_count(
		EF800_LCD_CONTRAST_STATIC, 3U), 3U, NULL);
	zassert_equal(ef800_lcd_contrast_settings_count(
		EF800_LCD_CONTRAST_PWM, 3U), 4U, NULL);
}

ZTEST(lcd_contrast, test_static_composition_never_initializes_pwm_or_reads_duty)
{
	fixture.load_result = 0;
	fixture.stored_duty = 63U;

	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_STATIC, &ops, &fixture), NULL);
	zassert_equal(fixture.pwm_calls, 0U, NULL);
	zassert_equal(fixture.save_calls, 0U, NULL);
	zassert_equal(fixture.published_mode, EF800_LCD_CONTRAST_STATIC, NULL);
	zassert_equal(fixture.stored_duty, 63U, NULL);
}

ZTEST(lcd_contrast, test_missing_duty_creates_and_applies_78_percent)
{
	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_PWM, &ops, &fixture), NULL);
	zassert_equal(fixture.stored_duty, 78U, NULL);
	zassert_equal(fixture.save_calls, 1U, NULL);
	zassert_equal(fixture.applied_duty, 78U, NULL);
	zassert_equal(fixture.published_duty, 78U, NULL);
}

ZTEST(lcd_contrast, test_saved_duty_overrides_78_percent)
{
	fixture.load_result = 0;
	fixture.stored_duty = 42U;

	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_PWM, &ops, &fixture), NULL);
	zassert_equal(fixture.applied_duty, 42U, NULL);
	zassert_equal(fixture.save_calls, 0U, NULL);
	/* Reinitializing in static mode leaves the saved duty untouched. */
	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_STATIC, &ops, &fixture), NULL);
	zassert_equal(fixture.stored_duty, 42U, NULL);
	zassert_equal(fixture.save_calls, 0U, NULL);
}

ZTEST(lcd_contrast, test_invalid_saved_duty_falls_back_to_78_percent)
{
	fixture.load_result = -EBADMSG;
	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_PWM, &ops, &fixture), NULL);
	zassert_equal(fixture.applied_duty, 78U, NULL);
	zassert_equal(fixture.diagnostic_calls, 1U, NULL);

	fixture = (struct contrast_fixture){ .load_result = 0U, .stored_duty = 101U };
	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_PWM, &ops, &fixture), NULL);
	zassert_equal(fixture.applied_duty, 78U, NULL);
	zassert_equal(fixture.diagnostic_calls, 1U, NULL);
}

ZTEST(lcd_contrast, test_duty_set_requires_active_composition_and_persists)
{
	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_STATIC, &ops, &fixture), NULL);
	zassert_equal(ef800_lcd_contrast_set_duty(&contrast, 55U), -EACCES, NULL);
	zassert_equal(fixture.pwm_calls, 0U, NULL);

	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_PWM, &ops, &fixture), NULL);
	zassert_ok(ef800_lcd_contrast_set_duty(&contrast, 55U), NULL);
	zassert_equal(fixture.applied_duty, 55U, NULL);
	zassert_equal(fixture.stored_duty, 55U, NULL);
	zassert_equal(fixture.published_duty, 55U, NULL);
	zassert_equal(ef800_lcd_contrast_set_duty(&contrast, 101U), -EINVAL, NULL);
}

ZTEST(lcd_contrast, test_persistence_failure_rolls_pwm_back)
{
	fixture.load_result = 0;
	fixture.stored_duty = 40U;
	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_PWM, &ops, &fixture), NULL);
	fixture.save_result = -EIO;
	zassert_equal(ef800_lcd_contrast_set_duty(&contrast, 60U), -EIO, NULL);
	zassert_equal(fixture.applied_duty, 40U, NULL);
}

ZTEST(lcd_contrast, test_default_persistence_failure_does_not_block_pwm_startup)
{
	fixture.save_result = -EROFS;
	zassert_ok(ef800_lcd_contrast_init(&contrast,
		EF800_LCD_CONTRAST_PWM, &ops, &fixture), NULL);
	zassert_equal(fixture.applied_duty, 78U, NULL);
	zassert_equal(fixture.diagnostic_calls, 1U, NULL);
}
