/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Adapt built-in instrument drivers to a common device-plugin API. */
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

/** @brief Find a built-in measurement driver by builder ID.
 * @param builder_id Device builder identifier.
 * @return Plugin descriptor, or NULL when no built-in driver matches.
 */
const struct ef800_device_plugin *ef800_device_plugin_find(uint8_t builder_id);

/** @brief Get a driver's default serial configuration for a connection.
 * @param builder_id Device builder identifier.
 * @param connection Connection capability.
 * @param config Receives serial settings.
 * @return 0 on success; negative errno when no serial setup is supported.
 */
int ef800_device_plugin_serial_config(
	uint8_t builder_id, uint32_t connection,
	struct ef800_measurement_serial_config *config);

/** @brief Invoke the selected built-in driver's measurement operation.
 * @param builder_id Device builder identifier.
 * @param transport Instrument transport.
 * @param measurement Receives observation data.
 * @return 0 on measurement; negative errno for unknown driver or I/O failure.
 */
int ef800_device_plugin_measure(
	uint8_t builder_id, const struct ef800_measurement_transport *transport,
	struct ef800_measurement_data *measurement);

#endif /* EF800_DEVICE_PLUGIN_H */
