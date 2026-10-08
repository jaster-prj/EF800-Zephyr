/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_max17043.h"

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(ef800_max17043, LOG_LEVEL_INF);

__weak bool ef800_virtual_battery_available(void)
{
	return true;
}

__weak uint8_t ef800_battery_profile_soc(void)
{
	return 0U;
}

__weak uint32_t ef800_battery_profile_voltage_uv(void)
{
	return 0U;
}

#define MAX17043_REG_VCELL   0x02U
#define MAX17043_REG_SOC     0x04U
#define MAX17043_REG_VERSION 0x08U

static int read_register(struct ef800_max17043 *gauge,
			 uint8_t reg, uint16_t *value)
{
	uint8_t buffer[2];
	int ret;

	ret = i2c_write_read_dt(&gauge->i2c, &reg, sizeof(reg),
				       buffer, sizeof(buffer));
	if (ret != 0) {
		return ret;
	}
	*value = sys_get_be16(buffer);
	return 0;
}

uint8_t ef800_max17043_soc_percent(uint16_t raw_soc)
{
	uint32_t percent = raw_soc >> 8;

	if (percent > 100U) {
		percent = 100U;
	}
	return (uint8_t)percent;
}

uint32_t ef800_max17043_voltage_uv(uint16_t raw_vcell)
{
	/* MAX17043 VCELL is a 12-bit value in bits 15:4 at 1.25 mV/LSB. */
	return ((uint32_t)(raw_vcell >> 4U) * 1250U);
}

int ef800_max17043_init(struct ef800_max17043 *gauge,
			const struct i2c_dt_spec *spec)
{
	uint16_t version;
	int ret;

	if (gauge == NULL || spec == NULL || !i2c_is_ready_dt(spec)) {
		return -ENODEV;
	}
	if (!ef800_virtual_battery_available()) {
		return -ENODEV;
	}

	gauge->i2c = *spec;
	k_mutex_init(&gauge->lock);
	ret = read_register(gauge, MAX17043_REG_VERSION, &version);
	if (ret != 0) {
		LOG_WRN("MAX17043 at 0x%02x is not responding: %d",
			spec->addr, ret);
		return ret;
	}

	gauge->ready = true;
	LOG_INF("MAX17043 ready at 0x%02x (version 0x%04x)",
		spec->addr, version);
	return 0;
}

int ef800_max17043_read(struct ef800_max17043 *gauge,
			struct ef800_max17043_status *status)
{
	uint16_t raw_soc;
	uint16_t raw_vcell;
	int ret;

	if (gauge == NULL || status == NULL || !gauge->ready) {
		return -ENODEV;
	}

	k_mutex_lock(&gauge->lock, K_FOREVER);
	ret = read_register(gauge, MAX17043_REG_SOC, &raw_soc);
	if (ret == 0) {
		ret = read_register(gauge, MAX17043_REG_VCELL, &raw_vcell);
	}
	if (ret == 0) {
		status->soc_percent = ef800_max17043_soc_percent(raw_soc);
		status->voltage_uv = ef800_max17043_voltage_uv(raw_vcell);
		if (ef800_battery_profile_soc() != 0U) {
			status->soc_percent = ef800_battery_profile_soc();
			status->voltage_uv = ef800_battery_profile_voltage_uv();
		}
	}
	k_mutex_unlock(&gauge->lock);

	if (ret != 0) {
		LOG_WRN("MAX17043 status read failed: %d", ret);
	}
	return ret;
}
