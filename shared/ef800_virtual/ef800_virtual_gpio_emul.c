/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT jaster_ef800_gpio_emul

#include "ef800_virtual_gpio_emul.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

struct ef800_virtual_gpio_config {
	struct gpio_driver_config common;
	gpio_port_pins_t pin_mask;
};

struct ef800_virtual_gpio_data {
	struct gpio_driver_data common;
	gpio_flags_t flags[32];
	gpio_port_value_t input;
	gpio_port_value_t output;
	gpio_port_pins_t interrupt_enabled;
	gpio_port_pins_t pending;
	sys_slist_t callbacks;
	struct k_spinlock lock;
};

static ef800_virtual_gpio_output_cb_t output_callback;
static void *output_context;

int ef800_virtual_gpio_output_callback_set(
	ef800_virtual_gpio_output_cb_t callback, void *context)
{
	output_callback = callback;
	output_context = context;
	return 0;
}

static inline const struct ef800_virtual_gpio_config *gpio_config(
	const struct device *dev)
{
	return dev->config;
}

static inline struct ef800_virtual_gpio_data *gpio_data(const struct device *dev)
{
	return dev->data;
}

static int gpio_configure(const struct device *dev, gpio_pin_t pin,
	gpio_flags_t flags)
{
	struct ef800_virtual_gpio_data *data = gpio_data(dev);
	const struct ef800_virtual_gpio_config *config = gpio_config(dev);

	if (pin >= 32U || !(config->pin_mask & BIT(pin))) {
		return -EINVAL;
	}
	if ((flags & GPIO_INPUT) && (flags & GPIO_OUTPUT)) {
		return -ENOTSUP;
	}
	data->flags[pin] = flags;
	if (flags & GPIO_OUTPUT_INIT_HIGH) {
		data->output |= BIT(pin);
	} else if (flags & GPIO_OUTPUT_INIT_LOW) {
		data->output &= ~BIT(pin);
	}
	return 0;
}

static gpio_port_value_t port_value(const struct ef800_virtual_gpio_data *data)
{
	gpio_port_value_t value = 0U;

	for (uint8_t pin = 0U; pin < 32U; ++pin) {
		if (data->flags[pin] & GPIO_OUTPUT) {
			value |= data->output & BIT(pin);
		} else {
			value |= data->input & BIT(pin);
		}
	}
	return value;
}

static void notify_output(const struct device *dev, gpio_port_pins_t changed,
	gpio_port_value_t values)
{
	if (changed != 0U && output_callback != NULL) {
		output_callback(dev, changed, values, output_context);
	}
}

static int port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	if (value == NULL) {
		return -EINVAL;
	}
	*value = port_value(gpio_data(dev));
	return 0;
}

static int port_set_masked_raw(const struct device *dev,
	gpio_port_pins_t mask, gpio_port_value_t value)
{
	struct ef800_virtual_gpio_data *data = gpio_data(dev);
	gpio_port_value_t previous = data->output;

	data->output = (data->output & ~mask) | (value & mask);
	notify_output(dev, (previous ^ data->output) & mask, data->output);
	return 0;
}

static int port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return port_set_masked_raw(dev, pins, pins);
}

static int port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return port_set_masked_raw(dev, pins, 0U);
}

static int port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	struct ef800_virtual_gpio_data *data = gpio_data(dev);

	return port_set_masked_raw(dev, pins, data->output ^ pins);
}

static int pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
	enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	struct ef800_virtual_gpio_data *data = gpio_data(dev);

	if (pin >= 32U) {
		return -EINVAL;
	}
	if (mode == GPIO_INT_MODE_DISABLED) {
		data->interrupt_enabled &= ~BIT(pin);
		return 0;
	}
	if (mode != GPIO_INT_MODE_EDGE ||
		(trig != GPIO_INT_TRIG_LOW && trig != GPIO_INT_TRIG_HIGH &&
		 trig != GPIO_INT_TRIG_BOTH)) {
		return -ENOTSUP;
	}
	data->flags[pin] &= ~(GPIO_INT_EDGE_RISING | GPIO_INT_EDGE_FALLING |
		GPIO_INT_EDGE_BOTH);
	data->flags[pin] |= trig == GPIO_INT_TRIG_BOTH ? GPIO_INT_EDGE_BOTH :
		(trig == GPIO_INT_TRIG_HIGH ? GPIO_INT_EDGE_RISING : GPIO_INT_EDGE_FALLING);
	data->interrupt_enabled |= BIT(pin);
	return 0;
}

static int manage_callback(const struct device *dev, struct gpio_callback *callback,
	bool set)
{
	return gpio_manage_callback(&gpio_data(dev)->callbacks, callback, set);
}

static uint32_t get_pending_int(const struct device *dev)
{
	struct ef800_virtual_gpio_data *data = gpio_data(dev);
	uint32_t pending = data->pending;

	data->pending = 0U;
	return pending;
}

static DEVICE_API(gpio, ef800_virtual_gpio_api) = {
	.pin_configure = gpio_configure,
	.port_get_raw = port_get_raw,
	.port_set_masked_raw = port_set_masked_raw,
	.port_set_bits_raw = port_set_bits_raw,
	.port_clear_bits_raw = port_clear_bits_raw,
	.port_toggle_bits = port_toggle_bits,
	.pin_interrupt_configure = pin_interrupt_configure,
	.manage_callback = manage_callback,
	.get_pending_int = get_pending_int,
};

int ef800_virtual_gpio_input_set(const struct device *dev,
	gpio_port_pins_t pins, gpio_port_value_t values)
{
	struct ef800_virtual_gpio_data *data = gpio_data(dev);
	gpio_port_value_t previous = data->input;
	gpio_port_pins_t changed = (previous ^ values) & pins;

	data->input = (data->input & ~pins) | (values & pins);
	for (uint8_t pin = 0U; pin < 32U; ++pin) {
		if (!(changed & BIT(pin)) || !(data->interrupt_enabled & BIT(pin))) {
			continue;
		}
		bool rising = !(previous & BIT(pin)) && (values & BIT(pin));
		bool falling = (previous & BIT(pin)) && !(values & BIT(pin));
		gpio_flags_t trigger = data->flags[pin] & GPIO_INT_EDGE_BOTH;
		if ((rising && (trigger == GPIO_INT_EDGE_RISING || trigger == GPIO_INT_EDGE_BOTH)) ||
			(falling && (trigger == GPIO_INT_EDGE_FALLING || trigger == GPIO_INT_EDGE_BOTH))) {
			data->pending |= BIT(pin);
			gpio_fire_callbacks(&data->callbacks, dev, BIT(pin));
		}
	}
	return 0;
}

static int gpio_init(const struct device *dev)
{
	sys_slist_init(&gpio_data(dev)->callbacks);
	return 0;
}

#define EF800_GPIO_EMUL_DEFINE(inst) \
	static struct ef800_virtual_gpio_data ef800_gpio_data_##inst; \
	static const struct ef800_virtual_gpio_config ef800_gpio_config_##inst = { \
		.common = { .port_pin_mask = DT_INST_PROP(inst, ngpios) >= 32 ? \
			UINT32_MAX : BIT_MASK(DT_INST_PROP(inst, ngpios)) }, \
		.pin_mask = DT_INST_PROP(inst, ngpios) >= 32 ? UINT32_MAX : \
			BIT_MASK(DT_INST_PROP(inst, ngpios)), \
	}; \
	DEVICE_DT_INST_DEFINE(inst, gpio_init, NULL, &ef800_gpio_data_##inst, \
		&ef800_gpio_config_##inst, POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, \
		&ef800_virtual_gpio_api)

DT_INST_FOREACH_STATUS_OKAY(EF800_GPIO_EMUL_DEFINE)
