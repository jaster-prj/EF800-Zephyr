/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_application_port_workflow.h"

#include <errno.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ef800_application_port_workflow, CONFIG_LOG_DEFAULT_LEVEL);

uint8_t ef800_application_port_workflow_uart_owner(
	const struct ef800_application_port_workflow *workflow, uint32_t plugin_id)
{
	if (workflow == NULL || workflow->plugins == NULL) {
		return (uint8_t)plugin_id;
	}
	const struct ef800_plugin_registry_entry *entry =
		ef800_plugin_registry_find(workflow->plugins, plugin_id);

	return entry != NULL && entry->info.kind == EF800_PLUGIN_KIND_MEASUREMENT ?
		EF800_PORT_BUILDER_DEBUG : (uint8_t)plugin_id;
}

/** @brief Remove an unavailable plugin assignment from runtime and persistence.
 * @param workflow Workflow holding the assignment service.
 * @param port_id Port whose stale plugin assignment is cleared.
 * @return 0 on removal; persistence/runtime error otherwise.
 */
static int clear_stale_plugin_assignment(
	struct ef800_application_port_workflow *workflow, uint8_t port_id)
{
	int result = ef800_port_assignment_remove(workflow->assignments, port_id);
	int runtime_result;

	if (result == 0) {
		return 0;
	}
	/* Startup must not expose an unavailable plugin if persistence cleanup
	 * fails. Clear the in-memory assignment and retry persistence next boot. */
	runtime_result = ef800_port_assignment_import_with_connection(
		workflow->assignments, port_id, EF800_PORT_BUILDER_NONE,
		EF800_PORT_CONNECTION_NONE);
	if (runtime_result != 0) {
		LOG_ERR("Cannot clear stale plugin assignment on port %u: remove=%d runtime=%d",
			port_id, result, runtime_result);
		return runtime_result;
	}
	LOG_WRN("Port %u stale plugin assignment cleared for this boot; persistence failed: %d",
		port_id, result);
	return result;
}

int ef800_application_port_workflow_reconcile_plugins(
	struct ef800_application_port_workflow *workflow)
{
	int first_error = 0;

	if (workflow == NULL || workflow->assignments == NULL ||
	    workflow->plugins == NULL) {
		return -EINVAL;
	}
	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		uint32_t plugin_id = ef800_port_assignment_get_plugin_id(
			workflow->assignments, port_id);
		uint8_t builder_id = ef800_port_assignment_get_builder(
			workflow->assignments, port_id);
		const struct ef800_port_device_descriptor *device =
			ef800_port_device_get(builder_id);
		const struct ef800_plugin_registry_entry *entry;
		bool keep;

		if (!ef800_port_assignment_is_assigned(workflow->assignments,
			port_id) || plugin_id == EF800_PORT_BUILDER_NONE || device != NULL) {
			continue;
		}
		entry = ef800_plugin_registry_find(workflow->plugins, plugin_id);
		keep = entry != NULL && entry->state == EF800_PLUGIN_REGISTRY_READY;
		if (keep && entry->info.kind == EF800_PLUGIN_KIND_MEASUREMENT) {
			keep = entry->info.required_hardware == EF800_PLUGIN_HARDWARE_SERIAL &&
				ef800_port_connection_is_valid(port_id,
					ef800_port_assignment_get_connection(
						workflow->assignments, port_id));
		}
		if (keep) {
			continue;
		}
		LOG_WRN("Clearing missing or unavailable plugin %u on port %u",
			(unsigned int)plugin_id, port_id);
		{
			int result = clear_stale_plugin_assignment(workflow, port_id);

			if (result != 0 && first_error == 0) {
				first_error = result;
			}
		}
	}
	return first_error;
}

int ef800_application_port_workflow_load_measurement_ports(
	struct ef800_application_port_workflow *workflow)
{
	if (workflow == NULL || workflow->assignments == NULL ||
	    workflow->plugins == NULL || workflow->measurement_transports == NULL) {
		return -EINVAL;
	}
	workflow->measurement_port_count = 0U;
	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		uint32_t plugin_id;
		enum ef800_plugin_registry_state state;

		/* Built-in assignments store a builder ID in plugin_id too. They are
		 * deliberately excluded from this package-only measurement menu. */
		if (ef800_port_assignment_get_builder(workflow->assignments, port_id) !=
		    EF800_PORT_BUILDER_NONE) {
			continue;
		}
		plugin_id = ef800_port_assignment_get_plugin_id(workflow->assignments,
			port_id);
		state = ef800_plugin_registry_state_for(workflow->plugins, plugin_id);
		if (ef800_plugin_registry_find(workflow->plugins, plugin_id) != NULL &&
		    state == EF800_PLUGIN_REGISTRY_READY &&
		    workflow->measurement_transports[port_id - 1U].uart != NULL) {
			workflow->measurement_port_ids[
				workflow->measurement_port_count++] = port_id;
		} else if (ef800_port_assignment_is_assigned(workflow->assignments,
			port_id) && plugin_id != EF800_PORT_BUILDER_NONE) {
			LOG_WRN("Measurement assignment not ready: port=%u plugin=%u registry=%u transport=%u",
				port_id, (unsigned int)plugin_id, state,
				workflow->measurement_transports[port_id - 1U].uart != NULL);
		}
	}
	return workflow->measurement_port_count == 0U ? -ENODEV : 0;
}

int ef800_application_port_workflow_claim_loaded_assignments(
	struct ef800_application_port_workflow *workflow)
{
	int first_error = 0;

	if (workflow == NULL || workflow->assignments == NULL ||
	    workflow->plugins == NULL || workflow->uart_registry == NULL ||
	    workflow->measurement_transports == NULL ||
	    workflow->port_uart_check_ready == NULL ||
	    workflow->initialize_measurement_transport == NULL) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < ef800_port_count(); ++index) {
		const struct ef800_port_descriptor *port = ef800_port_at(index);
		uint32_t plugin_id;
		uint32_t connection;
		const struct ef800_plugin_registry_entry *entry;
		int result;

		if (port == NULL) {
			continue;
		}
		plugin_id = ef800_port_assignment_get_plugin_id(workflow->assignments,
			port->id);
		if (plugin_id == EF800_PORT_BUILDER_NONE) {
			continue;
		}
		connection = ef800_port_assignment_get_connection(workflow->assignments,
			port->id);
		LOG_INF("Restoring port assignment: port=%u builder=%u plugin=%u connection=%u",
			port->id,
			ef800_port_assignment_get_builder(workflow->assignments, port->id),
			(unsigned int)plugin_id, (unsigned int)connection);
		result = workflow->port_uart_check_ready(workflow->adapter_context,
			port->id, connection);
		if (result != 0) {
			LOG_WRN("Port %u UART is not ready: %d", port->id, result);
			if (first_error == 0) {
				first_error = result;
			}
			continue;
		}
		result = ef800_port_uart_claim(workflow->uart_registry, port->id,
			ef800_application_port_workflow_uart_owner(workflow, plugin_id), connection);
		if (result != 0) {
			LOG_WRN("Port %u assignment cannot claim UART: %d", port->id,
				result);
			if (first_error == 0) {
				first_error = result;
			}
			continue;
		}
		entry = ef800_plugin_registry_find(workflow->plugins, plugin_id);
		if (entry != NULL && entry->info.kind == EF800_PLUGIN_KIND_MEASUREMENT) {
			result = workflow->initialize_measurement_transport(
				workflow->adapter_context, port->id, plugin_id, connection);
			LOG_INF("Port %u measurement transport init result=%d", port->id,
				result);
			if (result != 0 && first_error == 0) {
				first_error = result;
			}
		}
	}
	return first_error;
}

int ef800_application_port_workflow_set_assignment(
	struct ef800_application_port_workflow *workflow, uint8_t port_id,
	uint8_t builder_id, uint32_t connection)
{
	if (workflow == NULL || workflow->assignments == NULL) {
		return -EINVAL;
	}
	if (builder_id == EF800_PORT_BUILDER_MSD &&
	    (workflow->usb_msd_enabled == NULL ||
	     !workflow->usb_msd_enabled(workflow->adapter_context))) {
		return -ENOTSUP;
	}
	if (builder_id == EF800_PORT_BUILDER_DEBUG) {
		return ef800_port_assignment_move_debug(workflow->assignments,
			port_id, connection);
	}
	return ef800_port_assignment_set_with_connection(workflow->assignments,
		port_id, builder_id, connection);
}

int ef800_application_port_workflow_set_plugin_assignment(
	struct ef800_application_port_workflow *workflow, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection)
{
	if (workflow == NULL || workflow->assignments == NULL ||
	    workflow->plugins == NULL ||
	    ef800_plugin_registry_find(workflow->plugins, plugin_id) == NULL) {
		return -EINVAL;
	}
	return ef800_port_assignment_set_plugin_with_connection(
		workflow->assignments, port_id, plugin_id, connection);
}

int ef800_application_port_workflow_remove_assignment(
	struct ef800_application_port_workflow *workflow, uint8_t port_id)
{
	int result;

	if (workflow == NULL || workflow->assignments == NULL) {
		return -EINVAL;
	}
	result = ef800_port_assignment_remove(workflow->assignments, port_id);
	if (result == -ENOTSUP) {
		result = ef800_port_assignment_import_with_connection(
			workflow->assignments, port_id, EF800_PORT_BUILDER_NONE,
			EF800_PORT_CONNECTION_NONE);
	}
	return result;
}

int ef800_application_port_workflow_restore_assignment(
	struct ef800_application_port_workflow *workflow, uint8_t port_id,
	uint8_t builder_id, uint32_t connection)
{
	if (workflow == NULL) {
		return -EINVAL;
	}
	return builder_id == EF800_PORT_BUILDER_NONE ?
		ef800_application_port_workflow_remove_assignment(workflow, port_id) :
		ef800_application_port_workflow_set_assignment(workflow, port_id,
			builder_id, connection);
}
