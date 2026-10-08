/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_GPIO_EMUL_H
#define EF800_VIRTUAL_GPIO_EMUL_H

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

typedef void (*ef800_virtual_gpio_output_cb_t)(const struct device *port,
	gpio_port_pins_t changed, gpio_port_value_t values, void *context);

int ef800_virtual_gpio_output_callback_set(
	ef800_virtual_gpio_output_cb_t callback, void *context);

int ef800_virtual_gpio_input_set(const struct device *port,
	gpio_port_pins_t pins, gpio_port_value_t values);

#endif
