/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_device_plugin.h"

#include <errno.h>
#include <stddef.h>

static const struct ef800_device_plugin device_plugins[] = {
	{
		.builder_id = EF800_PORT_BUILDER_ELTA3,
		.name = "Elta3",
		.default_serial_config = ef800_elta3_default_serial_config,
		.measure = ef800_elta3_measure,
	},
	{
		.builder_id = EF800_PORT_BUILDER_FTS3XX,
		.name = "FTS3XX (FTS3XX)",
		.default_serial_config = ef800_fts3xx_default_serial_config,
		.measure = ef800_fts3xx_measure,
	},
};

const struct ef800_device_plugin *ef800_device_plugin_find(uint8_t builder_id)
{
	for (size_t index = 0U; index < sizeof(device_plugins) /
		     sizeof(device_plugins[0]); ++index) {
		if (device_plugins[index].builder_id == builder_id) {
			return &device_plugins[index];
		}
	}
	return NULL;
}

int ef800_device_plugin_serial_config(
	uint8_t builder_id, uint32_t connection,
	struct ef800_measurement_serial_config *config)
{
	const struct ef800_device_plugin *plugin =
		ef800_device_plugin_find(builder_id);

	if (plugin == NULL || config == NULL ||
	    (connection != EF800_PORT_CONNECTION_SERIAL &&
	     connection != EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL)) {
		return -EINVAL;
	}
	plugin->default_serial_config(config);
	config->flow_control = connection ==
		EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL ?
		EF800_MEASUREMENT_FLOW_RTS_CTS : EF800_MEASUREMENT_FLOW_NONE;
	return 0;
}

int ef800_device_plugin_measure(
	uint8_t builder_id, const struct ef800_measurement_transport *transport,
	struct ef800_measurement_data *measurement)
{
	const struct ef800_device_plugin *plugin =
		ef800_device_plugin_find(builder_id);

	if (plugin == NULL || transport == NULL || measurement == NULL) {
		return -EINVAL;
	}
	return plugin->measure(transport, measurement);
}
