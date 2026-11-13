/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Persist versioned battery SOC calibration with redundant slots. */
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

/** @brief Load the newest valid calibration record and initialize locking.
 * @param store Store state to initialize.
 * @param persistence Persistence backend used for redundant records.
 * @return 0 on initialization; negative errno when backend/setup fails.
 */
int ef800_battery_soc_store_init(struct ef800_battery_soc_store *store,
	struct ef800_persistence *persistence);

/** @brief Check whether a valid calibration table is available.
 * @param store Store to inspect.
 * @return True when a table has been loaded or committed.
 */
bool ef800_battery_soc_store_has_table(
	struct ef800_battery_soc_store *store);

/** @brief Copy the active table while holding the store lock.
 * @param store Initialized store.
 * @param table Receives the table.
 * @return 0 on copy; negative errno when no valid table is available.
 */
int ef800_battery_soc_store_get_table(
	struct ef800_battery_soc_store *store,
	struct ef800_battery_soc_table *table);

/** @brief Encode the active table into a hexadecimal string.
 * @param store Initialized store.
 * @param hex Destination buffer.
 * @param capacity Buffer capacity including terminator.
 * @param hex_length Receives encoded character count.
 * @return 0 on encode; negative errno when unavailable or buffer is too small.
 */
int ef800_battery_soc_store_get_hex(
	struct ef800_battery_soc_store *store, char *hex, size_t capacity,
	size_t *hex_length);

/** @brief Validate and atomically persist a new table from hexadecimal text.
 * @param store Initialized store.
 * @param hex Encoded table string.
 * @param hex_length Character count.
 * @return 0 on commit; negative errno for invalid data or persistence failure.
 */
int ef800_battery_soc_store_set_hex(
	struct ef800_battery_soc_store *store, const char *hex, size_t hex_length);

#endif /* EF800_BATTERY_SOC_STORE_H_ */
