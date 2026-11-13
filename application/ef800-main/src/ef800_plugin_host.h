/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Manage plugin lifecycle, isolated worker calls, and measurement sessions.
 */
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

/** @brief Initialize host state against the registered plugin catalog.
 * @param registry Registry containing validated plugin descriptors.
 * @return 0 on success; negative errno if setup or worker initialization fails.
 */
int ef800_plugin_host_init(struct ef800_plugin_registry *registry);

/** @brief Attach a worker thread to the protected plugin execution domain.
 * @param worker Worker thread to attach.
 * @param generation Receives its new session generation.
 * @return 0 on attach; negative errno on invalid worker/domain setup.
 */
int ef800_plugin_host_worker_domain_attach(struct k_thread *worker,
	uint32_t *generation);

/** @brief Detach a worker from its protected execution domain.
 * @param worker Worker thread to detach.
 * @param generation Generation expected for this attachment.
 * @return 0 on detach; negative errno for stale generation or domain failure.
 */
int ef800_plugin_host_worker_domain_detach(struct k_thread *worker,
	uint32_t generation);

/** @brief Get the supervisor-owned worker mailbox.
 * @return Mailbox pointer, or NULL when worker support is unavailable.
 */
struct ef800_plugin_worker_mailbox *ef800_plugin_host_worker_mailbox(void);

/** @brief Check whether plugin host initialization completed.
 * @return True when the host is ready.
 */
bool ef800_plugin_host_is_ready(void);

/** @brief Count plugins currently available for execution.
 * @return Number of available registered plugins.
 */
size_t ef800_plugin_host_available_count(void);

/** @brief Invoke a measurement plugin synchronously.
 * @param plugin_id Plugin identifier.
 * @param transport Assigned instrument transport.
 * @param measurement Receives measured data.
 * @return 0 on success; negative errno on plugin failure, fault, or timeout.
 */
int ef800_plugin_host_measure(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	struct ef800_measurement_data *measurement);

/** @brief Send a legacy directional/input action to a plugin.
 * @param plugin_id Plugin identifier.
 * @param transport Assigned instrument transport.
 * @param input Input action value.
 * @return 0 on success; negative errno on plugin failure or timeout.
 */
int ef800_plugin_host_input(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport, uint32_t input);

/** @brief Get the plugin's default serial-port configuration.
 * @param plugin_id Plugin identifier.
 * @param transport Assigned instrument transport.
 * @param config Receives UART settings.
 * @return 0 on success; negative errno if unsupported or plugin fails.
 */
int ef800_plugin_host_serial_config(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	struct ef800_measurement_serial_config *config);

/** @brief Create a stateful measurement-plugin session and claim its route.
 * @param session Session state to initialize.
 * @param plugin_id Plugin identifier.
 * @param transport Assigned instrument transport.
 * @return 0 on open; negative errno if unavailable, route conflict, or callback fails.
 */
int ef800_plugin_host_measurement_session_open(
	struct ef800_measurement_plugin_session *session, uint32_t plugin_id,
	const struct ef800_measurement_transport *transport);

/** @brief Update point/code context for an active measurement session.
 * @param session Active session.
 * @param context Current measurement context.
 * @return 0 on update; negative errno if session/plugin rejects the context.
 */
int ef800_plugin_host_measurement_session_set_context(
	struct ef800_measurement_plugin_session *session,
	const struct ef800_measurement_context *context);

/** @brief Request one measurement from an active session.
 * @param session Active session.
 * @param measurement Receives measured data.
 * @return 0 on success; negative errno on callback, timeout, or recovery error.
 */
int ef800_plugin_host_measurement_session_measure(
	struct ef800_measurement_plugin_session *session,
	struct ef800_measurement_data *measurement);

/** @brief Send a supported action to an active measurement session.
 * @param session Active session.
 * @param input Action value.
 * @return 0 on success; negative errno on invalid action or plugin failure.
 */
int ef800_plugin_host_measurement_session_input(
	struct ef800_measurement_plugin_session *session, uint32_t input);

/** @brief Deliver a key event to a session and report whether it was consumed.
 * @param session Active session.
 * @param key Key code.
 * @param consumed Receives whether the plugin consumed the key.
 * @return 0 on dispatch; negative errno on callback or timeout failure.
 */
int ef800_plugin_host_measurement_session_key(
	struct ef800_measurement_plugin_session *session, uint8_t key,
	bool *consumed);

/** @brief Request the plugin's bounded display model.
 * @param session Active session.
 * @param display Receives validated display content.
 * @return 0 on render; negative errno on invalid model or plugin failure.
 */
int ef800_plugin_host_measurement_session_render(
	struct ef800_measurement_plugin_session *session,
	struct ef800_measurement_display *display);

/** @brief Request a workspace record generated by the active plugin.
 * @param session Active session.
 * @param buffer Destination buffer.
 * @param capacity Buffer capacity.
 * @param length Receives record byte count.
 * @return 0 on record; negative errno if unsupported, oversized, or failed.
 */
int ef800_plugin_host_measurement_session_workspace_record(
	struct ef800_measurement_plugin_session *session, char *buffer,
	size_t capacity, size_t *length);

/** @brief Report whether the host accepted a plugin-generated workspace record.
 * @param session Active session.
 * @param status Host commit result.
 * @return 0 when reported; negative errno on callback failure.
 */
int ef800_plugin_host_measurement_session_workspace_result(
	struct ef800_measurement_plugin_session *session, int status);

/** @brief Submit an asynchronous plugin operation and allocate a result token.
 * @param request Operation and input state to copy for execution.
 * @param token Receives the poll/release token.
 * @return 0 on submission; negative errno when queueing or validation fails.
 */
int ef800_plugin_host_async_submit(
	const struct ef800_plugin_async_request *request, uint32_t *token);

/** @brief Poll an asynchronous operation result.
 * @param token Token returned at submission.
 * @param result Receives completed result when ready.
 * @return 0 when complete; -EAGAIN while pending; other negative errno on failure.
 */
int ef800_plugin_host_async_poll(uint32_t token,
	struct ef800_plugin_async_result *result);

/** @brief Release storage associated with a completed async operation.
 * @param token Operation token.
 * @return 0 on release; negative errno for unknown or active token.
 */
int ef800_plugin_host_async_release(uint32_t token);

/** @brief Copy the privileged recovery ledger for diagnostics/recovery.
 * @param snapshot Receives a consistent ledger snapshot.
 * @return 0 on copy; negative errno if no valid snapshot is available.
 */
int ef800_plugin_host_recovery_snapshot(
	struct ef800_plugin_recovery_ledger *snapshot);

/** @brief Register the callback used to restore or release a failed route.
 * @param handler Route recovery callback, or NULL to clear it.
 * @param context Opaque callback argument.
 * @return 0 on registration; negative errno if host state is unavailable.
 */
int ef800_plugin_host_set_route_recovery_handler(
	ef800_plugin_route_recovery_fn handler, void *context);

/** @brief Record that recovery released a port route.
 * @param port_id Released port.
 * @param connection Connection identifier that was released.
 * @return None.
 */
void ef800_plugin_host_recovery_route_released(uint8_t port_id,
	uint32_t connection);

/** @brief Mark a worker service request as in progress for recovery tracking.
 * @param generation Worker generation.
 * @param operation Service operation identifier.
 * @return None.
 */
void ef800_plugin_host_recovery_service_begin(uint32_t generation,
	uint32_t operation);

/** @brief Clear the matching in-progress service request from recovery state.
 * @param generation Worker generation.
 * @param operation Service operation identifier.
 * @return None.
 */
void ef800_plugin_host_recovery_service_end(uint32_t generation,
	uint32_t operation);

/** @brief Destroy a session and release its claimed route.
 * @param session Session to close; inactive sessions are ignored.
 * @return None.
 */
void ef800_plugin_host_measurement_session_close(
	struct ef800_measurement_plugin_session *session);

/** @brief Run a calculation plugin over a workspace snapshot.
 * @param plugin_id Calculation plugin identifier.
 * @param workspace Source workspace.
 * @param snapshot Snapshot buffer.
 * @param snapshot_capacity Snapshot buffer capacity.
 * @param output Result buffer.
 * @param output_capacity Result buffer capacity.
 * @param output_length Receives result length.
 * @return 0 on calculation; negative errno on worker/plugin/bounds failure.
 */
int ef800_plugin_host_calculate(uint32_t plugin_id,
	const struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length);

/** @brief Calculate and append the confirmed result to the workspace.
 * @param plugin_id Calculation plugin identifier.
 * @param workspace Workspace to snapshot and update.
 * @param snapshot Snapshot buffer.
 * @param snapshot_capacity Snapshot buffer capacity.
 * @param output Result buffer.
 * @param output_capacity Result buffer capacity.
 * @param output_length Receives result length.
 * @return 0 on calculation and append; negative errno on worker or storage failure.
 */
int ef800_plugin_host_calculate_confirmed(uint32_t plugin_id,
	struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length);

#endif
