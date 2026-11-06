/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_HOST_H
#define EF800_PLUGIN_HOST_H

#include <stdbool.h>
#include <stddef.h>

#include "ef800_plugin_registry.h"
#include "ef800_measurement.h"
#include "ef800_workspace.h"
#include "ef800_measurement_plugin.h"

struct k_thread;
struct ef800_plugin_worker_mailbox;

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
	uint8_t route_port_id;
	uint32_t route_connection;
	bool route_claimed;
	void *instance;
	const struct ef800_measurement_transport *transport;
	uint8_t state;
};

enum ef800_plugin_async_operation {
	EF800_PLUGIN_ASYNC_SESSION_OPEN = 1,
	EF800_PLUGIN_ASYNC_SESSION_KEY,
	EF800_PLUGIN_ASYNC_SESSION_INPUT,
	EF800_PLUGIN_ASYNC_SESSION_RENDER,
	EF800_PLUGIN_ASYNC_SESSION_MEASURE,
	EF800_PLUGIN_ASYNC_WORKSPACE_RECORD,
	EF800_PLUGIN_ASYNC_WORKSPACE_RESULT,
	EF800_PLUGIN_ASYNC_SESSION_CLOSE,
	EF800_PLUGIN_ASYNC_MEASURE,
	EF800_PLUGIN_ASYNC_INPUT,
	EF800_PLUGIN_ASYNC_CALCULATE,
};

enum ef800_plugin_failure_class {
	EF800_PLUGIN_FAILURE_NONE = 0,
	EF800_PLUGIN_FAILURE_CALLBACK,
	EF800_PLUGIN_FAILURE_TIMEOUT,
	EF800_PLUGIN_FAILURE_FAULT,
	EF800_PLUGIN_FAILURE_RECOVERY,
};

struct ef800_plugin_async_request {
	enum ef800_plugin_async_operation operation;
	struct ef800_measurement_plugin_session *session;
	uint32_t plugin_id;
	uint8_t route_port_id;
	uint32_t route_connection;
	bool route_claimed;
	const struct ef800_measurement_transport *transport;
	const struct ef800_workspace *workspace;
	const char *snapshot;
	size_t snapshot_length;
	size_t output_capacity;
	struct ef800_measurement_context context;
	uint32_t input;
	uint8_t key;
	int workspace_status;
};

/* Privileged-only recovery snapshot; never mapped into the worker domain. */
struct ef800_plugin_recovery_ledger {
	uint32_t plugin_id;
	uint32_t manager_plugin_id;
	uint32_t operation;
	uint32_t generation;
	uint32_t pending_service_operation;
	uint32_t output_capacity;
	uint32_t cleanup_progress;
	uint8_t route_port_id;
	uint32_t route_connection;
	const struct ef800_measurement_transport *route_transport;
	uint64_t invocation_deadline_ms;
	int32_t last_status;
	bool active;
	bool manager_loaded;
	bool instance_active;
	bool route_claimed;
	bool worker_mapped;
	bool invocation_pending;
	bool service_pending;
	bool output_pending;
	bool cleanup_in_progress;
};

struct ef800_plugin_async_result {
	uint32_t token;
	uint32_t generation;
	uint32_t plugin_id;
	uint32_t worker_generation;
	uint32_t fault_reason;
	uint32_t fault_pc;
	enum ef800_plugin_async_operation operation;
	enum ef800_plugin_failure_class failure_class;
	int status;
	bool recovery_required;
	int recovery_status;
	union {
		struct ef800_measurement_display display;
		struct ef800_measurement_data measurement;
		struct {
			bool consumed;
		} key;
		struct {
			char data[256];
			size_t length;
		} workspace_record;
		struct {
			const char *data;
			size_t length;
		} calculation;
	} value;
};

typedef int (*ef800_plugin_route_recovery_fn)(void *context,
	const struct ef800_plugin_recovery_ledger *ledger);

enum ef800_plugin_recovery_cleanup_step {
	EF800_PLUGIN_RECOVERY_WORKER_STOPPED = 1U << 0,
	EF800_PLUGIN_RECOVERY_GENERATION_INVALIDATED = 1U << 1,
	EF800_PLUGIN_RECOVERY_SERVICES_CANCELLED = 1U << 2,
	EF800_PLUGIN_RECOVERY_ROUTE_RELEASED = 1U << 3,
	EF800_PLUGIN_RECOVERY_OUTPUT_DISCARDED = 1U << 4,
	EF800_PLUGIN_RECOVERY_INSTANCE_DESTROYED = 1U << 5,
	EF800_PLUGIN_RECOVERY_INSTANCE_ABANDONED = 1U << 6,
	EF800_PLUGIN_RECOVERY_MAPPING_REMOVED = 1U << 7,
	EF800_PLUGIN_RECOVERY_MEMORY_ZEROED = 1U << 8,
};

int ef800_plugin_host_init(struct ef800_plugin_registry *registry);
int ef800_plugin_host_worker_domain_attach(struct k_thread *worker,
	uint32_t *generation);
int ef800_plugin_host_worker_domain_detach(struct k_thread *worker,
	uint32_t generation);
struct ef800_plugin_worker_mailbox *ef800_plugin_host_worker_mailbox(void);
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
int ef800_plugin_host_async_submit(
	const struct ef800_plugin_async_request *request, uint32_t *token);
int ef800_plugin_host_async_poll(uint32_t token,
	struct ef800_plugin_async_result *result);
int ef800_plugin_host_async_release(uint32_t token);
int ef800_plugin_host_recovery_snapshot(
	struct ef800_plugin_recovery_ledger *snapshot);
int ef800_plugin_host_set_route_recovery_handler(
	ef800_plugin_route_recovery_fn handler, void *context);
void ef800_plugin_host_recovery_route_released(uint8_t port_id,
	uint32_t connection);
void ef800_plugin_host_recovery_service_begin(uint32_t generation,
	uint32_t operation);
void ef800_plugin_host_recovery_service_end(uint32_t generation,
	uint32_t operation);
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
