/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_BATTERY_SOC_STORE_H_
#define EF800_BATTERY_SOC_STORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "ef800_battery_soc.h"
#include "ef800_persistence.h"

#define EF800_BATTERY_SOC_STORE_SLOT_A "battery_soc_a"
#define EF800_BATTERY_SOC_STORE_SLOT_B "battery_soc_b"
#define EF800_BATTERY_SOC_STORE_RECORD_MAX \
	(10U + EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 4U)

struct ef800_battery_soc_store {
	struct k_mutex lock;
	struct ef800_persistence *persistence;
	struct ef800_battery_soc_table table;
	uint32_t generation;
	int active_slot;
	int load_error;
	bool table_available;
};

int ef800_battery_soc_store_init(struct ef800_battery_soc_store *store,
	struct ef800_persistence *persistence);
bool ef800_battery_soc_store_has_table(
	struct ef800_battery_soc_store *store);
int ef800_battery_soc_store_get_table(
	struct ef800_battery_soc_store *store,
	struct ef800_battery_soc_table *table);
int ef800_battery_soc_store_get_hex(
	struct ef800_battery_soc_store *store, char *hex, size_t capacity,
	size_t *hex_length);
int ef800_battery_soc_store_set_hex(
	struct ef800_battery_soc_store *store, const char *hex, size_t hex_length);

#endif /* EF800_BATTERY_SOC_STORE_H_ */
