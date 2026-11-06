/* SPDX-License-Identifier: Apache-2.0 */
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

int ef800_battery_soc_gauge_init(struct ef800_battery_soc_gauge *gauge,
	ef800_battery_gauge_probe_fn probe, ef800_battery_gauge_read_fn read,
	void *context);
int ef800_battery_soc_gauge_read(void *context,
	struct ef800_max17043_status *status);

#endif /* EF800_BATTERY_SOC_GAUGE_H_ */
