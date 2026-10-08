/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT maxim_ds3231_mfd

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include "ef800_virtual_profile.h"

LOG_MODULE_REGISTER(ef800_virtual_ds3231, LOG_LEVEL_INF);

struct ds3231_emul_data {
	uint8_t registers[0x13];
};

static int ds3231_transfer(const struct emul *target, struct i2c_msg *msgs,
	int num_msgs, int addr)
{
	struct ds3231_emul_data *data = target->data;
	uint8_t reg;

	ARG_UNUSED(addr);
	if (msgs == NULL || num_msgs == 0) {
		return -EINVAL;
	}
	if (!ef800_virtual_profile_available(3U)) {
		return -EIO;
	}

	if (num_msgs == 2 && !(msgs[0].flags & I2C_MSG_READ) &&
		(msgs[1].flags & I2C_MSG_READ)) {
		if (msgs[0].len != 1U || msgs[1].len == 0U) {
			return -EINVAL;
		}
		reg = msgs[0].buf[0];
		if ((size_t)reg + msgs[1].len > ARRAY_SIZE(data->registers)) {
			return -EINVAL;
		}
		memcpy(msgs[1].buf, &data->registers[reg], msgs[1].len);
		return 0;
	}

	if (num_msgs == 2 && !(msgs[0].flags & I2C_MSG_READ) &&
		!(msgs[1].flags & I2C_MSG_READ)) {
		if (msgs[0].len != 1U || msgs[1].len == 0U) {
			return -EINVAL;
		}
		reg = msgs[0].buf[0];
		if ((size_t)reg + msgs[1].len > ARRAY_SIZE(data->registers)) {
			return -EINVAL;
		}
		memcpy(&data->registers[reg], msgs[1].buf, msgs[1].len);
		return 0;
	}

	if (num_msgs == 1 && !(msgs[0].flags & I2C_MSG_READ) &&
		msgs[0].len >= 2U) {
		reg = msgs[0].buf[0];
		if ((size_t)reg + msgs[0].len - 1U > ARRAY_SIZE(data->registers)) {
			return -EINVAL;
		}
		memcpy(&data->registers[reg], &msgs[0].buf[1], msgs[0].len - 1U);
		return 0;
	}

	return -ENOTSUP;
}

static const struct i2c_emul_api ds3231_api = {
	.transfer = ds3231_transfer,
};

static int ds3231_init(const struct emul *target, const struct device *parent)
{
	struct ds3231_emul_data *data = target->data;

	ARG_UNUSED(parent);
	memset(data, 0, sizeof(*data));
	/* 2026-01-01 12:00:00, Thursday, in DS3231 BCD format. */
	data->registers[0x00] = 0x00;
	data->registers[0x01] = 0x00;
	data->registers[0x02] = 0x12;
	data->registers[0x03] = 0x05;
	data->registers[0x04] = 0x01;
	data->registers[0x05] = 0x01;
	data->registers[0x06] = 0x26;
	data->registers[0x0e] = 0x00;
	data->registers[0x0f] = 0x00;
	return 0;
}

#define DS3231_EMUL(inst) \
	static struct ds3231_emul_data ds3231_data_##inst; \
	EMUL_DT_INST_DEFINE(inst, ds3231_init, &ds3231_data_##inst, NULL, \
		&ds3231_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(DS3231_EMUL)
