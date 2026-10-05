/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MEASUREMENT_ZEPHYR_H
#define EF800_MEASUREMENT_ZEPHYR_H

#include <stdint.h>

#include <zephyr/device.h>

#include "ef800_measurement.h"

struct ef800_measurement_zephyr_transport {
	struct ef800_measurement_transport transport;
	const struct device *uart;
};

int ef800_measurement_zephyr_init_with_config(
	struct ef800_measurement_zephyr_transport *transport,
	const struct device *uart,
	const struct ef800_measurement_serial_config *config);

#endif /* EF800_MEASUREMENT_ZEPHYR_H */
