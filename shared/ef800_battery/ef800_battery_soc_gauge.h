/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Adapt optional fuel-gauge callbacks to the SOC reader interface. */
#ifndef EF800_BATTERY_SOC_GAUGE_H_
#define EF800_BATTERY_SOC_GAUGE_H_

#include <stdbool.h>

#include "ef800_max17043.h"

typedef int (*ef800_battery_gauge_probe_fn)(void *context);
typedef int (*ef800_battery_gauge_read_fn)(void *context,
	struct ef800_max17043_status *status);

struct ef800_battery_soc_gauge {
	ef800_battery_gauge_probe_fn probe;
	ef800_battery_gauge_read_fn read;
	void *context;
	int probe_error;
	bool probe_attempted;
	bool ready;
};

/** @brief Initialize and probe a gauge adapter.
 * @param gauge Adapter state to initialize.
 * @param probe Optional readiness/probe callback.
 * @param read Required callback that fills gauge status.
 * @param context Opaque argument passed to callbacks.
 * @return 0 when initialized; negative errno for invalid callback/state.
 */
int ef800_battery_soc_gauge_init(struct ef800_battery_soc_gauge *gauge,
	ef800_battery_gauge_probe_fn probe, ef800_battery_gauge_read_fn read,
	void *context);

/** @brief Read a status sample through an initialized gauge adapter.
 * @param context Gauge adapter state.
 * @param status Receives voltage/SOC status.
 * @return 0 on read; negative errno if probe or read fails.
 */
int ef800_battery_soc_gauge_read(void *context,
	struct ef800_max17043_status *status);

#endif /* EF800_BATTERY_SOC_GAUGE_H_ */
