/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_DEVICE_PLUGIN_H
#define EF800_DEVICE_PLUGIN_H

#include <stdint.h>

#include "ef800_measurement.h"

struct ef800_device_plugin {
	uint8_t builder_id;
	const char *name;
	void (*default_serial_config)(
		struct ef800_measurement_serial_config *config);
	int (*measure)(const struct ef800_measurement_transport *transport,
		       struct ef800_measurement_data *measurement);
};

const struct ef800_device_plugin *ef800_device_plugin_find(uint8_t builder_id);

int ef800_device_plugin_serial_config(
	uint8_t builder_id, uint32_t connection,
	struct ef800_measurement_serial_config *config);

int ef800_device_plugin_measure(
	uint8_t builder_id, const struct ef800_measurement_transport *transport,
	struct ef800_measurement_data *measurement);

#endif /* EF800_DEVICE_PLUGIN_H */
