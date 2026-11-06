/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_battery_soc_gauge.h"

#include <errno.h>

int ef800_battery_soc_gauge_init(struct ef800_battery_soc_gauge *gauge,
	ef800_battery_gauge_probe_fn probe, ef800_battery_gauge_read_fn read,
	void *context)
{
	if (gauge == NULL || probe == NULL || read == NULL) {
		return -EINVAL;
	}
	*gauge = (struct ef800_battery_soc_gauge){
		.probe = probe,
		.read = read,
		.context = context,
	};
	return 0;
}

int ef800_battery_soc_gauge_read(void *context,
	struct ef800_max17043_status *status)
{
	struct ef800_battery_soc_gauge *gauge = context;
	int ret;

	if (gauge == NULL || status == NULL) {
		return -EINVAL;
	}
	if (!gauge->probe_attempted) {
		gauge->probe_attempted = true;
		gauge->probe_error = gauge->probe(gauge->context);
		gauge->ready = gauge->probe_error == 0;
	}
	if (!gauge->ready) {
		return gauge->probe_error != 0 ? gauge->probe_error : -ENODEV;
	}
	ret = gauge->read(gauge->context, status);
	return ret;
}
