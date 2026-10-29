/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_lcd_contrast_stm32.h"
#include "ef800_lcd_contrast.h"

#include <errno.h>

#include <soc.h>
#include <stm32_ll_tim.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>

#define LCD_CONTRAST_NODE DT_NODELABEL(lcd_contrast)
#define LCD_CONTRAST_TIMER_NODE DT_NODELABEL(timers4)

PINCTRL_DT_DEFINE(LCD_CONTRAST_NODE);

static const struct pinctrl_dev_config *const contrast_pinctrl =
	PINCTRL_DT_DEV_CONFIG_GET(LCD_CONTRAST_NODE);
static const struct gpio_dt_spec contrast_pin =
	GPIO_DT_SPEC_GET(LCD_CONTRAST_NODE, gpios);
static const struct device *const rcc_device =
	DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
static const struct stm32_pclken timer_clocks[] =
	STM32_DT_CLOCKS(LCD_CONTRAST_TIMER_NODE);
static const struct reset_dt_spec timer_reset =
	RESET_DT_SPEC_GET(LCD_CONTRAST_TIMER_NODE);

static K_MUTEX_DEFINE(contrast_lock);
static bool timer_clock_enabled;
static bool pwm_running;
static uint32_t active_period_counts;

static int timer_clock_start(uint32_t *clock_rate)
{
	int ret;

	if (clock_rate == NULL || ARRAY_SIZE(timer_clocks) < 2U ||
	    !device_is_ready(rcc_device) || !gpio_is_ready_dt(&contrast_pin)) {
		return -ENODEV;
	}
	ret = clock_control_on(rcc_device,
		(clock_control_subsys_t)&timer_clocks[0]);
	if (ret != 0) {
		return ret;
	}
	timer_clock_enabled = true;
	ret = clock_control_configure(rcc_device,
		(clock_control_subsys_t)&timer_clocks[1], NULL);
	if (ret != 0) {
		goto fail;
	}
	ret = clock_control_get_rate(rcc_device,
		(clock_control_subsys_t)&timer_clocks[1], clock_rate);
	if (ret != 0) {
		goto fail;
	}
	ret = reset_line_toggle_dt(&timer_reset);
	if (ret != 0) {
		goto fail;
	}
	return 0;

fail:
	(void)clock_control_off(rcc_device,
		(clock_control_subsys_t)&timer_clocks[0]);
	timer_clock_enabled = false;
	return ret;
}

static void timer_clock_stop(void)
{
	if (timer_clock_enabled) {
		(void)clock_control_off(rcc_device,
			(clock_control_subsys_t)&timer_clocks[0]);
		timer_clock_enabled = false;
	}
}

int ef800_lcd_contrast_stm32_set(uint8_t duty_percent)
{
	uint32_t timer_hz;
	uint32_t period_counts;
	uint32_t pulse_counts;
	int ret;

	if (duty_percent > 100U) {
		return -ERANGE;
	}
	k_mutex_lock(&contrast_lock, K_FOREVER);
	if (pwm_running) {
		pulse_counts = (uint32_t)(((uint64_t)active_period_counts *
			duty_percent + 50U) / 100U);
		LL_TIM_OC_SetCompareCH3(TIM4, pulse_counts);
		LL_TIM_GenerateEvent_UPDATE(TIM4);
		k_mutex_unlock(&contrast_lock);
		return 0;
	}
	ret = timer_clock_start(&timer_hz);
	if (ret != 0) {
		k_mutex_unlock(&contrast_lock);
		return ret;
	}
	ret = ef800_lcd_contrast_calculate_counts(timer_hz, duty_percent,
		&period_counts,
		&pulse_counts);
	if (ret != 0) {
		goto fail;
	}
	LL_TIM_DisableCounter(TIM4);
	LL_TIM_SetPrescaler(TIM4, 0U);
	LL_TIM_SetCounterMode(TIM4, LL_TIM_COUNTERMODE_UP);
	LL_TIM_SetAutoReload(TIM4, period_counts - 1U);
	LL_TIM_OC_SetMode(TIM4, LL_TIM_CHANNEL_CH3, LL_TIM_OCMODE_PWM1);
	LL_TIM_OC_SetPolarity(TIM4, LL_TIM_CHANNEL_CH3, LL_TIM_OCPOLARITY_HIGH);
	LL_TIM_OC_SetCompareCH3(TIM4, pulse_counts);
	LL_TIM_EnableARRPreload(TIM4);
	LL_TIM_OC_EnablePreload(TIM4, LL_TIM_CHANNEL_CH3);
	LL_TIM_CC_EnableChannel(TIM4, LL_TIM_CHANNEL_CH3);
	LL_TIM_GenerateEvent_UPDATE(TIM4);
	ret = pinctrl_apply_state(contrast_pinctrl, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		goto fail;
	}
	LL_TIM_EnableCounter(TIM4);
	pwm_running = true;
	active_period_counts = period_counts;
	k_mutex_unlock(&contrast_lock);
	return 0;

fail:
	LL_TIM_DisableCounter(TIM4);
	LL_TIM_CC_DisableChannel(TIM4, LL_TIM_CHANNEL_CH3);
	(void)gpio_pin_configure_dt(&contrast_pin, GPIO_INPUT);
	timer_clock_stop();
	k_mutex_unlock(&contrast_lock);
	return ret;
}

int ef800_lcd_contrast_stm32_stop(void)
{
	int ret = 0;

	k_mutex_lock(&contrast_lock, K_FOREVER);
	if (pwm_running) {
		LL_TIM_DisableCounter(TIM4);
		LL_TIM_CC_DisableChannel(TIM4, LL_TIM_CHANNEL_CH3);
		ret = gpio_pin_configure_dt(&contrast_pin, GPIO_INPUT);
		pwm_running = false;
		active_period_counts = 0U;
	}
	timer_clock_stop();
	k_mutex_unlock(&contrast_lock);
	return ret;
}
