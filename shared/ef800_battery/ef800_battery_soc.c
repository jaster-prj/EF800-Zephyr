/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_battery_soc.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

uint32_t ef800_battery_soc_crc32(const uint8_t *data, size_t length)
{
	uint32_t crc = UINT32_MAX;

	for (size_t index = 0U; index < length; ++index) {
		crc ^= data[index];
		for (unsigned int bit = 0U; bit < 8U; ++bit) {
			crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
		}
	}
	return crc ^ UINT32_MAX;
}

static int hex_nibble(char value)
{
	if (value >= '0' && value <= '9') {
		return value - '0';
	}
	if (value >= 'A' && value <= 'F') {
		return value - 'A' + 10;
	}
	if (value >= 'a' && value <= 'f') {
		return value - 'a' + 10;
	}
	return -1;
}

static char hex_digit(uint8_t value)
{
	static const char digits[] = "0123456789ABCDEF";

	return digits[value & 0x0FU];
}

static uint32_t read_u32_be(const uint8_t *data)
{
	return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
		((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

int ef800_battery_soc_table_validate(const struct ef800_battery_soc_table *table)
{
	if (table == NULL || table->version != EF800_BATTERY_SOC_TABLE_VERSION ||
	    (uint8_t)table->chemistry > EF800_BATTERY_SOC_CHEMISTRY_LIFEPO4 ||
	    table->point_count < EF800_BATTERY_SOC_TABLE_MIN_POINTS ||
	    table->point_count > EF800_BATTERY_SOC_TABLE_MAX_POINTS) {
		return -EINVAL;
	}
	if (table->points[0].soc_percent != 0U ||
	    table->points[table->point_count - 1U].soc_percent != 100U) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < table->point_count; ++index) {
		if (table->points[index].pin_mv > EF800_BATTERY_SOC_ADC_MAX_MV ||
		    table->points[index].soc_percent > 100U) {
			return -ERANGE;
		}
		if (index > 0U &&
		    (table->points[index].pin_mv <= table->points[index - 1U].pin_mv ||
		     table->points[index].soc_percent <=
		     table->points[index - 1U].soc_percent)) {
			return -EINVAL;
		}
	}
	return 0;
}

int ef800_battery_soc_table_decode_hex(const char *hex, size_t hex_length,
	struct ef800_battery_soc_table *table)
{
	uint8_t bytes[EF800_BATTERY_SOC_TABLE_MAX_BYTES];
	struct ef800_battery_soc_table decoded = {0};
	size_t byte_length;
	size_t expected_length;
	uint32_t stored_crc;
	int ret;

	if (hex == NULL || table == NULL || hex_length < 2U ||
	    (hex_length & 1U) != 0U ||
	    hex_length > EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH) {
		return -EINVAL;
	}
	byte_length = hex_length / 2U;
	for (size_t index = 0U; index < byte_length; ++index) {
		int high = hex_nibble(hex[index * 2U]);
		int low = hex_nibble(hex[index * 2U + 1U]);

		if (high < 0 || low < 0) {
			return -EINVAL;
		}
		bytes[index] = (uint8_t)(((unsigned int)high << 4U) | (unsigned int)low);
	}
	if (byte_length < 3U + EF800_BATTERY_SOC_TABLE_MIN_POINTS * 3U + 4U) {
		return -EINVAL;
	}
	if (bytes[0] != EF800_BATTERY_SOC_TABLE_VERSION) {
		return -ENOTSUP;
	}
	if (bytes[2] < EF800_BATTERY_SOC_TABLE_MIN_POINTS ||
	    bytes[2] > EF800_BATTERY_SOC_TABLE_MAX_POINTS) {
		return -EINVAL;
	}
	expected_length = 3U + (size_t)bytes[2] * 3U + 4U;
	if (byte_length != expected_length) {
		return -EINVAL;
	}
	stored_crc = read_u32_be(&bytes[expected_length - 4U]);
	if (stored_crc != ef800_battery_soc_crc32(bytes, expected_length - 4U)) {
		return -EBADMSG;
	}

	decoded.version = bytes[0];
	decoded.chemistry = (enum ef800_battery_soc_chemistry)bytes[1];
	decoded.point_count = bytes[2];
	for (size_t index = 0U; index < decoded.point_count; ++index) {
		size_t offset = 3U + index * 3U;

		decoded.points[index].pin_mv =
			(uint16_t)(((uint16_t)bytes[offset] << 8U) | bytes[offset + 1U]);
		decoded.points[index].soc_percent = bytes[offset + 2U];
	}
	ret = ef800_battery_soc_table_validate(&decoded);
	if (ret != 0) {
		return ret;
	}
	*table = decoded;
	return 0;
}

int ef800_battery_soc_table_encode_hex(
	const struct ef800_battery_soc_table *table, char *hex, size_t capacity,
	size_t *hex_length)
{
	uint8_t bytes[EF800_BATTERY_SOC_TABLE_MAX_BYTES];
	size_t byte_length;
	uint32_t crc;
	int ret;

	if (hex == NULL || hex_length == NULL) {
		return -EINVAL;
	}
	ret = ef800_battery_soc_table_validate(table);
	if (ret != 0) {
		return ret;
	}
	byte_length = 3U + (size_t)table->point_count * 3U + 4U;
	if (capacity < byte_length * 2U + 1U) {
		return -ENOMEM;
	}
	bytes[0] = table->version;
	bytes[1] = (uint8_t)table->chemistry;
	bytes[2] = table->point_count;
	for (size_t index = 0U; index < table->point_count; ++index) {
		size_t offset = 3U + index * 3U;

		bytes[offset] = (uint8_t)(table->points[index].pin_mv >> 8U);
		bytes[offset + 1U] = (uint8_t)table->points[index].pin_mv;
		bytes[offset + 2U] = table->points[index].soc_percent;
	}
	crc = ef800_battery_soc_crc32(bytes, byte_length - 4U);
	bytes[byte_length - 4U] = (uint8_t)(crc >> 24U);
	bytes[byte_length - 3U] = (uint8_t)(crc >> 16U);
	bytes[byte_length - 2U] = (uint8_t)(crc >> 8U);
	bytes[byte_length - 1U] = (uint8_t)crc;
	for (size_t index = 0U; index < byte_length; ++index) {
		hex[index * 2U] = hex_digit(bytes[index] >> 4U);
		hex[index * 2U + 1U] = hex_digit(bytes[index]);
	}
	hex[byte_length * 2U] = '\0';
	*hex_length = byte_length * 2U;
	return 0;
}

static uint32_t interpolate(uint32_t value, uint32_t low_value,
	uint32_t high_value, uint32_t low_result, uint32_t high_result)
{
	uint32_t value_span = high_value - low_value;
	uint32_t result_span = high_result - low_result;
	uint32_t numerator = (value - low_value) * result_span;

	return low_result + (numerator + value_span / 2U) / value_span;
}

int ef800_battery_soc_from_pin_mv(
	const struct ef800_battery_soc_table *table, uint16_t pin_mv,
	uint8_t *soc_percent)
{
	int ret = ef800_battery_soc_table_validate(table);

	if (ret != 0 || soc_percent == NULL) {
		return ret != 0 ? ret : -EINVAL;
	}
	if (pin_mv <= table->points[0].pin_mv) {
		*soc_percent = table->points[0].soc_percent;
		return 0;
	}
	if (pin_mv >= table->points[table->point_count - 1U].pin_mv) {
		*soc_percent = table->points[table->point_count - 1U].soc_percent;
		return 0;
	}
	for (size_t index = 1U; index < table->point_count; ++index) {
		const struct ef800_battery_soc_point *low = &table->points[index - 1U];
		const struct ef800_battery_soc_point *high = &table->points[index];

		if (pin_mv <= high->pin_mv) {
			*soc_percent = (uint8_t)interpolate(pin_mv, low->pin_mv,
				high->pin_mv, low->soc_percent, high->soc_percent);
			return 0;
		}
	}
	return -ERANGE;
}

int ef800_battery_pin_mv_from_soc(
	const struct ef800_battery_soc_table *table, uint8_t soc_percent,
	uint16_t *pin_mv)
{
	int ret = ef800_battery_soc_table_validate(table);

	if (ret != 0 || pin_mv == NULL || soc_percent > 100U) {
		return ret != 0 ? ret : -EINVAL;
	}
	if (soc_percent <= table->points[0].soc_percent) {
		*pin_mv = table->points[0].pin_mv;
		return 0;
	}
	if (soc_percent >= table->points[table->point_count - 1U].soc_percent) {
		*pin_mv = table->points[table->point_count - 1U].pin_mv;
		return 0;
	}
	for (size_t index = 1U; index < table->point_count; ++index) {
		const struct ef800_battery_soc_point *low = &table->points[index - 1U];
		const struct ef800_battery_soc_point *high = &table->points[index];

		if (soc_percent <= high->soc_percent) {
			*pin_mv = (uint16_t)interpolate(soc_percent,
				low->soc_percent, high->soc_percent,
				low->pin_mv, high->pin_mv);
			return 0;
		}
	}
	return -ERANGE;
}
