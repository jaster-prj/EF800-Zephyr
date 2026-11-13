/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT microchip_mcp23017

#include "virtual_ef800_mcp23017.h"
#include "virtual_ef800_profile.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(virtual_ef800_mcp23017, LOG_LEVEL_INF);

static virtual_ef800_mcp23017_output_cb_t output_callback;
static void *output_context;

int virtual_ef800_mcp23017_output_callback_set(
	virtual_ef800_mcp23017_output_cb_t callback, void *context)
{
	output_callback = callback;
	output_context = context;
	return 0;
}

struct mcp23017_emul_data {
	uint8_t registers[0x16];
};

static uint16_t gpio_value(const struct mcp23017_emul_data *data)
{
	return (uint16_t)data->registers[0x12] |
		((uint16_t)data->registers[0x13] << 8);
}

static void output_update(struct mcp23017_emul_data *data)
{
	uint16_t value = gpio_value(data);

	if (output_callback != NULL) {
		output_callback(value, output_context);
	}
}

static int mcp23017_transfer(const struct emul *target, struct i2c_msg *msgs,
	int num_msgs, int addr)
{
	struct mcp23017_emul_data *data = target->data;
	uint8_t reg;

	ARG_UNUSED(addr);
	if (msgs == NULL || num_msgs == 0) {
		return -EINVAL;
	}
	if (!virtual_ef800_profile_available(7U)) {
		return -EIO;
	}

	if (num_msgs == 2 && !(msgs[0].flags & I2C_MSG_READ) &&
		(msgs[1].flags & I2C_MSG_READ)) {
		if (msgs[0].len != 1U || msgs[1].len == 0U) {
			return -EINVAL;
		}
		reg = msgs[0].buf[0];
		for (size_t index = 0U; index < msgs[1].len; ++index) {
			uint8_t offset = (uint8_t)(reg + index);
			msgs[1].buf[index] = offset < ARRAY_SIZE(data->registers) ?
				data->registers[offset] : 0U;
		}
		return 0;
	}

	if (num_msgs == 1 && !(msgs[0].flags & I2C_MSG_READ) &&
		msgs[0].len >= 2U) {
		reg = msgs[0].buf[0];
		for (size_t index = 1U; index < msgs[0].len; ++index) {
			uint8_t offset = (uint8_t)(reg + index - 1U);
			if (offset >= ARRAY_SIZE(data->registers)) {
				return -EINVAL;
			}
			data->registers[offset] = msgs[0].buf[index];
		}
		if (reg <= 0x15U && reg + msgs[0].len - 1U >= 0x12U) {
			output_update(data);
		}
		return 0;
	}

	return -ENOTSUP;
}

static const struct i2c_emul_api mcp23017_api = {
	.transfer = mcp23017_transfer,
};

static int mcp23017_init(const struct emul *target, const struct device *parent)
{
	struct mcp23017_emul_data *data = target->data;

	ARG_UNUSED(parent);
	memset(data, 0, sizeof(*data));
	data->registers[0x00] = 0xffU;
	data->registers[0x01] = 0xffU;
	return 0;
}

#define MCP23017_EMUL(inst) \
	static struct mcp23017_emul_data mcp23017_data_##inst; \
	EMUL_DT_INST_DEFINE(inst, mcp23017_init, &mcp23017_data_##inst, NULL, \
		&mcp23017_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(MCP23017_EMUL)
