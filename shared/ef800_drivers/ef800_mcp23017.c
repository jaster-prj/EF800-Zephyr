/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_mcp23017.h"

#include <errno.h>

#include <zephyr/drivers/gpio.h>

int ef800_mcp23017_open(struct ef800_mcp23017 *mcp,
			const struct device *dev)
{
	if (mcp == NULL || dev == NULL || !device_is_ready(dev)) {
		return -ENODEV;
	}

	mcp->dev = dev;
	mcp->output_mask = 0U;
	k_mutex_init(&mcp->lock);
	return 0;
}

int ef800_mcp23017_probe(const struct device *dev, uint16_t *value)
{
	gpio_port_value_t port_value;
	int ret;

	if (dev == NULL || value == NULL || !device_is_ready(dev)) {
		return -ENODEV;
	}

	ret = gpio_port_get_raw(dev, &port_value);
	if (ret == 0) {
		*value = (uint16_t)port_value;
	}
	return ret;
}

int ef800_mcp23017_configure_outputs(struct ef800_mcp23017 *mcp,
					     uint16_t mask, uint16_t value)
{
	int ret = 0;

	if (mcp == NULL || mcp->dev == NULL) {
		return -ENODEV;
	}

	k_mutex_lock(&mcp->lock, K_FOREVER);
	for (uint8_t pin = 0U; pin < 16U; ++pin) {
		if ((mask & BIT(pin)) == 0U) {
			continue;
		}

		ret = gpio_pin_configure(mcp->dev, pin,
			GPIO_OUTPUT | ((value & BIT(pin)) != 0U ?
			GPIO_OUTPUT_INIT_HIGH : GPIO_OUTPUT_INIT_LOW));
		if (ret != 0) {
			break;
		}
	}
	if (ret == 0) {
		ret = gpio_port_set_masked_raw(mcp->dev, mask, value);
	}
	if (ret == 0) {
		mcp->output_mask |= mask;
	}
	k_mutex_unlock(&mcp->lock);

	return ret;
}

int ef800_mcp23017_read(struct ef800_mcp23017 *mcp, uint16_t *value)
{
	gpio_port_value_t port_value;
	int ret;

	if (mcp == NULL || mcp->dev == NULL || value == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&mcp->lock, K_FOREVER);
	ret = gpio_port_get_raw(mcp->dev, &port_value);
	if (ret == 0) {
		*value = (uint16_t)port_value;
	}
	k_mutex_unlock(&mcp->lock);

	return ret;
}

int ef800_mcp23017_write(struct ef800_mcp23017 *mcp,
				 uint16_t mask, uint16_t value)
{
	int ret;

	if (mcp == NULL || mcp->dev == NULL) {
		return -ENODEV;
	}

	k_mutex_lock(&mcp->lock, K_FOREVER);
	if ((mask & ~mcp->output_mask) != 0U) {
		ret = -EINVAL;
	} else {
		ret = gpio_port_set_masked_raw(mcp->dev, mask, value);
	}
	k_mutex_unlock(&mcp->lock);

	return ret;
}

int ef800_mcp23017_set_pin(struct ef800_mcp23017 *mcp,
				   uint8_t pin, bool value)
{
	if (pin >= 16U) {
		return -EINVAL;
	}
	return ef800_mcp23017_write(mcp, BIT(pin), value ? BIT(pin) : 0U);
}
