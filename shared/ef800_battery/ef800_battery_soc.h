/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_BATTERY_SOC_H_
#define EF800_BATTERY_SOC_H_

#include <stddef.h>
#include <stdint.h>

#define EF800_BATTERY_SOC_TABLE_VERSION 1U
#define EF800_BATTERY_SOC_TABLE_MIN_POINTS 2U
#define EF800_BATTERY_SOC_TABLE_MAX_POINTS 16U
#define EF800_BATTERY_SOC_ADC_MAX_MV 3300U
#define EF800_BATTERY_SOC_TABLE_MAX_BYTES \
	(3U + (EF800_BATTERY_SOC_TABLE_MAX_POINTS * 3U) + 4U)
#define EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH \
	(EF800_BATTERY_SOC_TABLE_MAX_BYTES * 2U)

enum ef800_battery_soc_source {
	EF800_BATTERY_SOC_SOURCE_NONE = 0U,
	EF800_BATTERY_SOC_SOURCE_ADC = 1U,
	EF800_BATTERY_SOC_SOURCE_MAX17048 = 2U,
};

enum ef800_battery_soc_chemistry {
	EF800_BATTERY_SOC_CHEMISTRY_CUSTOM = 0U,
	EF800_BATTERY_SOC_CHEMISTRY_LI_ION = 1U,
	EF800_BATTERY_SOC_CHEMISTRY_LIFEPO4 = 2U,
};

struct ef800_battery_soc_point {
	uint16_t pin_mv;
	uint8_t soc_percent;
};

struct ef800_battery_soc_table {
	uint8_t version;
	enum ef800_battery_soc_chemistry chemistry;
	uint8_t point_count;
	struct ef800_battery_soc_point points[EF800_BATTERY_SOC_TABLE_MAX_POINTS];
};

int ef800_battery_soc_table_validate(
	const struct ef800_battery_soc_table *table);
uint32_t ef800_battery_soc_crc32(const uint8_t *data, size_t length);
int ef800_battery_soc_table_decode_hex(const char *hex, size_t hex_length,
	struct ef800_battery_soc_table *table);
int ef800_battery_soc_table_encode_hex(
	const struct ef800_battery_soc_table *table, char *hex, size_t capacity,
	size_t *hex_length);
int ef800_battery_soc_from_pin_mv(
	const struct ef800_battery_soc_table *table, uint16_t pin_mv,
	uint8_t *soc_percent);
int ef800_battery_pin_mv_from_soc(
	const struct ef800_battery_soc_table *table, uint8_t soc_percent,
	uint16_t *pin_mv);

#endif /* EF800_BATTERY_SOC_H_ */
