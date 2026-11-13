/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_boot_probe.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "ef800_keyboard_boot.h"
#include "ef800_watchdog.h"

#define UPDATER_I2C_ADDRESS 0x48U

static const struct device *const i2c_target =
	DEVICE_DT_GET(DT_NODELABEL(i2c1));
static const struct gpio_dt_spec updater_request =
	GPIO_DT_SPEC_GET_OR(DT_PATH(zephyr_user), updater_request_gpios, {0});
static atomic_t updater_probe_received;
static bool i2c_target_registered;

static int i2c_write_requested(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	/* Address match alone is the probe; the peer need not send data or STOP. */
	if (ef800_keyboard_boot_i2c_probe(true, false)) {
		atomic_set(&updater_probe_received, 1);
	}
	return 0;
}

static int i2c_write_received(struct i2c_target_config *config, uint8_t byte)
{
	ARG_UNUSED(config);
	ARG_UNUSED(byte);
	return 0;
}

static int i2c_stop(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	return 0;
}

static const struct i2c_target_callbacks i2c_callbacks = {
	.write_requested = i2c_write_requested,
	.write_received = i2c_write_received,
	.stop = i2c_stop,
};

static struct i2c_target_config i2c_config = {
	.address = UPDATER_I2C_ADDRESS,
	.callbacks = &i2c_callbacks,
};

int ef800_keyboard_boot_probe_gpio(bool *requested)
{
	int result;

	if (requested == NULL) {
		return -EINVAL;
	}
	*requested = false;
	if (updater_request.port == NULL ||
	    !device_is_ready(updater_request.port)) {
		return -ENODEV;
	}
	result = gpio_pin_configure_dt(&updater_request, GPIO_INPUT);
	if (result != 0) {
		return result;
	}
	result = gpio_pin_get_dt(&updater_request);
	if (result < 0) {
		return result;
	}
	*requested = result == 0;
	return 0;
}

int ef800_keyboard_boot_probe_window(bool *received)
{
	uint32_t started_ms;
	int result;

	if (received == NULL) {
		return -EINVAL;
	}
	*received = false;
	if (!device_is_ready(i2c_target)) {
		return -ENODEV;
	}
	result = i2c_target_register(i2c_target, &i2c_config);
	if (result != 0) {
		return result;
	}
	i2c_target_registered = true;
	started_ms = k_uptime_get_32();
	while (!ef800_keyboard_boot_updater_window_expired(started_ms,
							 k_uptime_get_32())) {
		if (atomic_cas(&updater_probe_received, 1, 0)) {
			*received = true;
			return 0;
		}
		(void)ef800_watchdog_feed();
		k_sleep(K_MSEC(1));
	}
	(void)ef800_keyboard_boot_probe_unregister();
	return 0;
}

int ef800_keyboard_boot_probe_unregister(void)
{
	int result;

	if (!i2c_target_registered) {
		return 0;
	}
	result = i2c_target_unregister(i2c_target, &i2c_config);
	if (result == 0) {
		i2c_target_registered = false;
	}
	return result;
}
