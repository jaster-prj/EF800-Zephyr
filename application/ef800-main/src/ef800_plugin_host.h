/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_HOST_H
#define EF800_PLUGIN_HOST_H

#include <stdbool.h>
#include <stddef.h>

#include "ef800_plugin_registry.h"
#include "ef800_measurement.h"
#include "ef800_workspace.h"
#include "ef800_measurement_plugin.h"

enum ef800_measurement_session_action {
	EF800_MEASUREMENT_SESSION_MEASURE = 1u << 0,
	EF800_MEASUREMENT_SESSION_UP = 1u << 1,
	EF800_MEASUREMENT_SESSION_DOWN = 1u << 2,
	EF800_MEASUREMENT_SESSION_ENTER = 1u << 3,
};

struct ef800_measurement_plugin_session {
	uint32_t plugin_id;
	uint32_t loaded_id;
	uint32_t actions;
	void *instance;
	const struct ef800_measurement_transport *transport;
	uint8_t state;
};

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
int ef800_plugin_host_measurement_session_open(
	struct ef800_measurement_plugin_session *session, uint32_t plugin_id,
	const struct ef800_measurement_transport *transport);
int ef800_plugin_host_measurement_session_set_context(
	struct ef800_measurement_plugin_session *session,
	const struct ef800_measurement_context *context);
int ef800_plugin_host_measurement_session_measure(
	struct ef800_measurement_plugin_session *session,
	struct ef800_measurement_data *measurement);
int ef800_plugin_host_measurement_session_input(
	struct ef800_measurement_plugin_session *session, uint32_t input);
int ef800_plugin_host_measurement_session_key(
	struct ef800_measurement_plugin_session *session, uint8_t key,
	bool *consumed);
int ef800_plugin_host_measurement_session_render(
	struct ef800_measurement_plugin_session *session,
	struct ef800_measurement_display *display);
int ef800_plugin_host_measurement_session_workspace_record(
	struct ef800_measurement_plugin_session *session, char *buffer,
	size_t capacity, size_t *length);
int ef800_plugin_host_measurement_session_workspace_result(
	struct ef800_measurement_plugin_session *session, int status);
void ef800_plugin_host_measurement_session_close(
	struct ef800_measurement_plugin_session *session);
int ef800_plugin_host_calculate(uint32_t plugin_id,
	const struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length);
int ef800_plugin_host_calculate_confirmed(uint32_t plugin_id,
	struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length);

#endif
