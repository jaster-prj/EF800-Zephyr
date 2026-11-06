/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT maxim_ds3231_mfd

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

#include <native_rtc.h>

#include "ef800_time.h"
#include "ef800_virtual_profile.h"

LOG_MODULE_REGISTER(ef800_virtual_ds3231, LOG_LEVEL_INF);

struct ds3231_emul_data {
	uint8_t registers[0x13];
	int64_t utc_offset_seconds;
};

static uint8_t to_bcd(int value)
{
	return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static int from_bcd(uint8_t value)
{
	return (int)((value >> 4) * 10U + (value & 0x0fU));
}

static int epoch_to_utc_tm(int64_t epoch, struct tm *utc)
{
	static const uint16_t cumulative_days[] = {
		0U, 31U, 59U, 90U, 120U, 151U, 181U,
		212U, 243U, 273U, 304U, 334U,
	};
	int64_t days;
	int64_t seconds;
	int64_t shifted_days;
	int64_t era;
	int64_t day_of_era;
	int64_t year_of_era;
	int64_t year;
	int64_t day_of_year;
	int64_t month_part;
	int month;

	if (utc == NULL || epoch < 0) {
		return -EINVAL;
	}
	days = epoch / 86400;
	seconds = epoch % 86400;
	shifted_days = days + 719468;
	era = shifted_days / 146097;
	day_of_era = shifted_days - era * 146097;
	year_of_era = (day_of_era - day_of_era / 1460 +
		day_of_era / 36524 - day_of_era / 146096) / 365;
	year = year_of_era + era * 400;
	day_of_year = day_of_era -
		(365 * year_of_era + year_of_era / 4 - year_of_era / 100);
	month_part = (5 * day_of_year + 2) / 153;
	utc->tm_mday = (int)(day_of_year - (153 * month_part + 2) / 5 + 1);
	month = (int)(month_part + (month_part < 10 ? 3 : -9));
	year += month <= 2;

	utc->tm_sec = (int)(seconds % 60);
	utc->tm_min = (int)((seconds / 60) % 60);
	utc->tm_hour = (int)(seconds / 3600);
	utc->tm_mon = month - 1;
	utc->tm_year = (int)year - 1900;
	utc->tm_wday = (int)((days + 4) % 7);
	utc->tm_yday = (int)cumulative_days[month - 1] + utc->tm_mday - 1;
	if (month > 2 &&
	    ((year % 400) == 0 || ((year % 100) != 0 && (year % 4) == 0))) {
		++utc->tm_yday;
	}
	utc->tm_isdst = 0;
	return utc->tm_year >= 100 && utc->tm_year <= 199 ? 0 : -ERANGE;
}

static int get_native_utc_seconds(int64_t *seconds)
{
	uint32_t nanoseconds;
	uint64_t native_seconds;

	if (seconds == NULL) {
		return -EINVAL;
	}
	native_rtc_gettime(RTC_CLOCK_REALTIME, &nanoseconds, &native_seconds);
	ARG_UNUSED(nanoseconds);
	if (native_seconds > INT64_MAX) {
		return -ERANGE;
	}
	*seconds = (int64_t)native_seconds;
	return 0;
}

static int update_time_registers(struct ds3231_emul_data *data)
{
	int64_t seconds;
	struct tm utc;
	int result;

	result = get_native_utc_seconds(&seconds);
	if (result != 0 ||
	    (data->utc_offset_seconds > 0 && seconds > INT64_MAX - data->utc_offset_seconds) ||
	    (data->utc_offset_seconds < 0 && seconds < INT64_MIN - data->utc_offset_seconds)) {
		return result != 0 ? result : -ERANGE;
	}
	seconds += data->utc_offset_seconds;
	result = epoch_to_utc_tm(seconds, &utc);
	if (result != 0) {
		return result;
	}

	data->registers[0x00] = to_bcd(utc.tm_sec);
	data->registers[0x01] = to_bcd(utc.tm_min);
	data->registers[0x02] = to_bcd(utc.tm_hour);
	data->registers[0x03] = to_bcd(utc.tm_wday + 1);
	data->registers[0x04] = to_bcd(utc.tm_mday);
	data->registers[0x05] = to_bcd(utc.tm_mon + 1);
	data->registers[0x06] = to_bcd((utc.tm_year + 1900) % 100);
	return 0;
}

static void capture_time_register_write(struct ds3231_emul_data *data)
{
	struct rtc_time rtc = {
		.tm_sec = from_bcd(data->registers[0x00] & 0x7fU),
		.tm_min = from_bcd(data->registers[0x01] & 0x7fU),
		.tm_hour = from_bcd(data->registers[0x02] & 0x3fU),
		.tm_wday = from_bcd(data->registers[0x03] & 0x07U) - 1,
		.tm_mday = from_bcd(data->registers[0x04] & 0x3fU),
		.tm_mon = from_bcd(data->registers[0x05] & 0x1fU) - 1,
		.tm_year = 100 + from_bcd(data->registers[0x06]),
		.tm_yday = -1,
		.tm_isdst = -1,
	};
	struct tm standard = {
		.tm_sec = rtc.tm_sec,
		.tm_min = rtc.tm_min,
		.tm_hour = rtc.tm_hour,
		.tm_mday = rtc.tm_mday,
		.tm_mon = rtc.tm_mon,
		.tm_year = rtc.tm_year,
	};
	int64_t native_seconds;
	int64_t requested_seconds;

	if (!ef800_rtc_time_valid(&rtc) || get_native_utc_seconds(&native_seconds) != 0) {
		return;
	}
	requested_seconds = timeutil_timegm64(&standard);
	if ((requested_seconds > 0 && native_seconds < INT64_MIN + requested_seconds) ||
	    (requested_seconds < 0 && native_seconds > INT64_MAX + requested_seconds)) {
		return;
	}
	data->utc_offset_seconds = requested_seconds - native_seconds;
}

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
		if (reg == 0U && update_time_registers(data) != 0) {
			return -EIO;
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
		if (reg == 0U && msgs[1].len >= 7U) {
			capture_time_register_write(data);
		}
		return 0;
	}

	if (num_msgs == 1 && !(msgs[0].flags & I2C_MSG_READ) &&
		msgs[0].len >= 2U) {
		reg = msgs[0].buf[0];
		if ((size_t)reg + msgs[0].len - 1U > ARRAY_SIZE(data->registers)) {
			return -EINVAL;
		}
		memcpy(&data->registers[reg], &msgs[0].buf[1], msgs[0].len - 1U);
		if (reg == 0U && msgs[0].len >= 8U) {
			capture_time_register_write(data);
		}
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
	int result;

	ARG_UNUSED(parent);
	memset(data, 0, sizeof(*data));
	data->registers[0x0e] = 0x00;
	data->registers[0x0f] = 0x00;
	result = update_time_registers(data);
	if (result != 0) {
		LOG_ERR("Unable to initialize virtual DS3231 from native UTC: %d", result);
	}
	return result;
}

#define DS3231_EMUL(inst) \
	static struct ds3231_emul_data ds3231_data_##inst; \
	EMUL_DT_INST_DEFINE(inst, ds3231_init, &ds3231_data_##inst, NULL, \
		&ds3231_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(DS3231_EMUL)
