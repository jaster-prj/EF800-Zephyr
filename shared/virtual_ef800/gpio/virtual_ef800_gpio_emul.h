/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Emulate GPIO pin configuration and values in native tests. */
#ifndef VIRTUAL_EF800_GPIO_EMUL_H
#define VIRTUAL_EF800_GPIO_EMUL_H

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

typedef void (*virtual_ef800_gpio_output_cb_t)(const struct device *port,
	gpio_port_pins_t changed, gpio_port_value_t values, void *context);

/** @brief Register a callback for changes to emulated GPIO outputs.
 * @param callback Invoked with changed pins and values, or NULL to clear.
 * @param context Opaque callback argument.
 * @return 0 on update; negative errno if callback registration is unsupported.
 */
int virtual_ef800_gpio_output_callback_set(
	virtual_ef800_gpio_output_cb_t callback, void *context);

/** @brief Drive selected emulated GPIO input pins to supplied values.
 * @param port Emulated GPIO device.
 * @param pins Pin mask to update.
 * @param values New levels for the selected pins.
 * @return 0 on update; negative errno for invalid device/pins.
 */
int virtual_ef800_gpio_input_set(const struct device *port,
	gpio_port_pins_t pins, gpio_port_value_t values);

#endif
