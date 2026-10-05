/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_HOST_H
#define EF800_PLUGIN_HOST_H

#include <stdbool.h>
#include <stddef.h>

#include "ef800_plugin_registry.h"
#include "ef800_measurement.h"
#include "ef800_workspace.h"

int ef800_plugin_host_init(struct ef800_plugin_registry *registry);
bool ef800_plugin_host_is_ready(void);
size_t ef800_plugin_host_available_count(void);
int ef800_plugin_host_measure(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	struct ef800_measurement_data *measurement);
int ef800_plugin_host_input(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport, uint32_t input);
int ef800_plugin_host_serial_config(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	struct ef800_measurement_serial_config *config);
int ef800_plugin_host_calculate(uint32_t plugin_id,
	const struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length);
int ef800_plugin_host_calculate_confirmed(uint32_t plugin_id,
	struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length);

#endif
