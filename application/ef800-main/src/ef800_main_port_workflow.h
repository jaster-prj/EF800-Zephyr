/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief MainApplication port assignment, reconciliation, and UART-claim policy.
 */
#ifndef EF800_MAIN_PORT_WORKFLOW_H
#define EF800_MAIN_PORT_WORKFLOW_H

#include <stddef.h>
#include <stdint.h>

#include "ef800_measurement_zephyr.h"
#include "ef800_plugin_registry.h"
#include "ef800_port_uart.h"
#include "ef800_ports.h"

struct ef800_main_port_workflow {
	struct ef800_port_assignment_service *assignments;
	struct ef800_plugin_registry *plugins;
	struct ef800_port_uart_registry *uart_registry;
	struct ef800_measurement_zephyr_transport *measurement_transports;
	/* Hardware adapters: readiness/transport callbacks return 0 or -errno. */
	int (*port_uart_check_ready)(void *context, uint8_t port_id,
		uint32_t connection);
	int (*initialize_measurement_transport)(void *context, uint8_t port_id,
		uint32_t plugin_id, uint32_t connection);
	/* Reports whether USB mass storage currently owns shared storage. */
	bool (*usb_msd_enabled)(void *context);
	/* Opaque argument passed to the adapter callbacks above. */
	void *adapter_context;
	uint8_t measurement_port_ids[EF800_PORT_COUNT];
	size_t measurement_port_count;
	uint8_t selected_port_index;
	uint8_t selected_builder_id;
	uint32_t selected_plugin_id;
	uint8_t selected_measurement_port;
};

/** @brief Remove persisted assignments that refer to unavailable plugins.
 * @param workflow Workflow with assignment and plugin registries.
 * @return 0 if consistent; first negative errno encountered otherwise.
 */
int ef800_main_port_workflow_reconcile_plugins(
	struct ef800_main_port_workflow *workflow);
/** @brief Collect ready, assigned measurement-plugin ports for the UI.
 * @param workflow Workflow state to update.
 * @return 0 when at least one port is ready; otherwise negative errno.
 */
int ef800_main_port_workflow_load_measurement_ports(
	struct ef800_main_port_workflow *workflow);
/** @brief Restore UART claims and initialize loaded measurement transports.
 * @param workflow Workflow state and hardware adapter callbacks.
 * @return 0 if all claims succeeded; first negative errno otherwise.
 */
int ef800_main_port_workflow_claim_loaded_assignments(
	struct ef800_main_port_workflow *workflow);
/** @brief Map a plugin ID to the UART owner used by the port registry.
 * @param workflow Workflow and plugin registry; may be NULL for legacy mapping.
 * @param plugin_id Assigned plugin/builder ID.
 * @return Debug builder ID for measurement plugins, otherwise plugin_id.
 */
uint8_t ef800_main_port_workflow_uart_owner(
	const struct ef800_main_port_workflow *workflow, uint32_t plugin_id);
/** @brief Persist a built-in device assignment, applying builder-specific policy.
 * @param workflow Assignment service and optional USB-MSD adapter.
 * @param port_id Port to assign.
 * @param builder_id Built-in device builder ID.
 * @param connection Selected connection capability.
 * @return 0 on success or a negative errno on rejection/failure.
 */
int ef800_main_port_workflow_set_assignment(
	struct ef800_main_port_workflow *workflow, uint8_t port_id,
	uint8_t builder_id, uint32_t connection);
/** @brief Persist an assignment to a registered plugin.
 * @param workflow Assignment and plugin registries.
 * @param port_id Port to assign.
 * @param plugin_id Registered plugin ID.
 * @param connection Selected connection capability.
 * @return 0 on success or a negative errno on invalid input/failure.
 */
int ef800_main_port_workflow_set_plugin_assignment(
	struct ef800_main_port_workflow *workflow, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection);
/** @brief Remove a port assignment, clearing runtime state if persistence is absent.
 * @param workflow Workflow with assignment service.
 * @param port_id Port whose assignment is removed.
 * @return 0 on success or a negative errno on failure.
 */
int ef800_main_port_workflow_remove_assignment(
	struct ef800_main_port_workflow *workflow, uint8_t port_id);
/** @brief Restore a prior assignment or clear the port if it was unassigned.
 * @param workflow Workflow with assignment service.
 * @param port_id Port to restore.
 * @param builder_id Prior builder ID, or NONE to remove the assignment.
 * @param connection Prior connection capability.
 * @return 0 on success or a negative errno on failure.
 */
int ef800_main_port_workflow_restore_assignment(
	struct ef800_main_port_workflow *workflow, uint8_t port_id,
	uint8_t builder_id, uint32_t connection);

#endif /* EF800_MAIN_PORT_WORKFLOW_H */
