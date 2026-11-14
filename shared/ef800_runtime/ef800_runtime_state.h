/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Sample RTC and battery inputs into a synchronized runtime snapshot. */
#ifndef EF800_RUNTIME_STATE_H
#define EF800_RUNTIME_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>

#include "ef800_max17043.h"

enum ef800_runtime_sample_status {
	EF800_RUNTIME_SAMPLE_NEVER = 0,
	EF800_RUNTIME_SAMPLE_VALID,
	EF800_RUNTIME_SAMPLE_ERROR,
	EF800_RUNTIME_SAMPLE_UNAVAILABLE,
};

typedef int (*ef800_runtime_battery_reader_t)(void *context,
	struct ef800_max17043_status *status);

struct ef800_runtime_snapshot {
	struct rtc_time local_time;
	uint8_t battery_soc_percent;
	uint32_t battery_voltage_uv;
	int rtc_error;
	int battery_error;
	uint32_t rtc_updated_ms;
	uint32_t battery_updated_ms;
	uint32_t sequence;
	enum ef800_runtime_sample_status rtc_status;
	enum ef800_runtime_sample_status battery_status;
	bool rtc_has_value;
	bool battery_has_value;
};

struct ef800_runtime_state {
	struct k_mutex lock;
	struct k_work_delayable sample_work;
	struct ef800_runtime_snapshot snapshot;
	ef800_runtime_battery_reader_t battery_reader;
	void *battery_reader_context;
	uint8_t battery_consecutive_failures;
	uint32_t next_battery_sample_ms;
	bool started;
};

/** @brief Initialize snapshot state, work scheduling, and battery reader.
 * @param state State object to initialize.
 * @param battery_reader Callback that reads battery status.
 * @param battery_reader_context Opaque argument passed to the reader.
 * @return None.
 */
void ef800_runtime_state_init(struct ef800_runtime_state *state,
			      ef800_runtime_battery_reader_t battery_reader,
			      void *battery_reader_context);

/** @brief Start periodic runtime sampling.
 * @param state Initialized runtime state.
 * @return None.
 */
void ef800_runtime_state_start(struct ef800_runtime_state *state);

/** @brief Stop periodic sampling and cancel pending work.
 * @param state Runtime state to stop.
 * @return None.
 */
void ef800_runtime_state_stop(struct ef800_runtime_state *state);

/** @brief Refresh the RTC portion of the current snapshot.
 * @param state Runtime state to update.
 * @return None; status/error and timestamp are stored in the snapshot.
 */
void ef800_runtime_state_refresh_rtc(struct ef800_runtime_state *state);

/** @brief Refresh the battery portion of the current snapshot.
 * @param state Runtime state to update.
 * @return None; status/error and timestamp are stored in the snapshot.
 */
void ef800_runtime_state_refresh_battery(struct ef800_runtime_state *state);

/** @brief Copy a consistent runtime snapshot under the state lock.
 * @param state Runtime state to read.
 * @param snapshot Receives the snapshot copy.
 * @return 0 on copy; negative errno for invalid arguments.
 */
int ef800_runtime_state_snapshot(struct ef800_runtime_state *state,
				 struct ef800_runtime_snapshot *snapshot);

#endif /* EF800_RUNTIME_STATE_H */
