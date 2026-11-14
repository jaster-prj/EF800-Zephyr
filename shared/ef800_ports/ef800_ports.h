/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Describe physical ports and persist device/plugin assignments. */
#ifndef EF800_PORTS_H
#define EF800_PORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "ef800_persistence.h"
#include "ef800_update.h"

#define EF800_PORT_COUNT 5U
#define EF800_PORT_DEVICE_COUNT 5U
#define EF800_PORT_ASSIGNMENT_VERSION 2U
#define EF800_PORT_ASSIGNMENT_RECORD_SIZE 16U
#define EF800_PORT_ASSIGNMENT_FILE_PREFIX "port_assignment_"

enum ef800_port_capability {
	EF800_PORT_CAP_SERIAL = BIT(0),
	EF800_PORT_CAP_SERIAL_FLOW_CONTROL = BIT(1),
	EF800_PORT_CAP_PARALLEL = BIT(2),
	EF800_PORT_CAP_USB = BIT(3),
	EF800_PORT_CAP_I2C = BIT(4),
	EF800_PORT_CAP_SPI = BIT(5),
};

struct ef800_port_connection_descriptor {
	uint32_t capability;
	const char *name;
};

struct ef800_port_descriptor {
	uint8_t id;
	const char *title;
	char shortcut;
	uint32_t capabilities;
	bool fixed;
};

struct ef800_port_device_descriptor {
	uint8_t builder_id;
	const char *title;
	char shortcut;
	uint32_t capabilities;
	bool fixed;
};

struct ef800_port_assignment_state {
	uint8_t port_id;
	uint8_t builder_id;
	uint32_t plugin_id;
	uint8_t connection;
	bool assigned;
};

struct ef800_port_assignment_service {
	struct ef800_persistence *storage;
	struct ef800_port_assignment_state states[EF800_PORT_COUNT];
};

/** @brief Get the number of physical ports.
 * @return Port count.
 */
size_t ef800_port_count(void);

/** @brief Get a physical-port descriptor by index.
 * @param index Zero-based descriptor index.
 * @return Descriptor pointer, or NULL when out of range.
 */
const struct ef800_port_descriptor *ef800_port_at(size_t index);

/** @brief Find a physical-port descriptor by port identifier.
 * @param port_id Port identifier.
 * @return Descriptor pointer, or NULL when unknown.
 */
const struct ef800_port_descriptor *ef800_port_get(uint8_t port_id);

/** @brief Get the number of built-in device descriptors.
 * @return Device count.
 */
size_t ef800_port_device_count(void);

/** @brief Get a built-in device descriptor by index.
 * @param index Zero-based descriptor index.
 * @return Descriptor pointer, or NULL when out of range.
 */
const struct ef800_port_device_descriptor *ef800_port_device_at(size_t index);

/** @brief Find a built-in device descriptor by builder ID.
 * @param builder_id Stable device/builder identifier.
 * @return Descriptor pointer, or NULL when unknown.
 */
const struct ef800_port_device_descriptor *ef800_port_device_get(uint8_t builder_id);

/** @brief Check port/device capability and fixed-assignment compatibility.
 * @param port Physical-port descriptor.
 * @param device Device descriptor.
 * @return True when the device can be assigned to that port.
 */
bool ef800_port_device_is_compatible(const struct ef800_port_descriptor *port,
					     const struct ef800_port_device_descriptor *device);

/** @brief Check whether a built-in device supports a connection capability.
 * @param builder_id Device builder identifier.
 * @param connection Connection capability identifier.
 * @return True when supported.
 */
bool ef800_port_device_supports_connection(uint8_t builder_id,
							   uint32_t connection);

/** @brief Check whether a connection is available on a physical port.
 * @param port_id Port identifier.
 * @param connection Connection capability identifier.
 * @return True when available.
 */
bool ef800_port_connection_is_valid(uint8_t port_id, uint32_t connection);

/** @brief Get the number of connection choices for a physical port.
 * @param port_id Port identifier.
 * @return Choice count, or zero for an unknown port.
 */
size_t ef800_port_connection_count(uint8_t port_id);

/** @brief Get a connection descriptor for a port and choice index.
 * @param port_id Port identifier.
 * @param index Zero-based connection index.
 * @return Descriptor pointer, or NULL when out of range.
 */
const struct ef800_port_connection_descriptor *ef800_port_connection_at(
	uint8_t port_id, size_t index);

/** @brief Encode an assignment state into its stable persistent record.
 * @param state Assignment to encode.
 * @param buffer Destination record buffer.
 * @param capacity Buffer capacity.
 * @param length Receives encoded size.
 * @return 0 on encode; negative errno for invalid fields or short buffer.
 */
int ef800_port_assignment_encode(const struct ef800_port_assignment_state *state,
					 uint8_t *buffer, size_t capacity, size_t *length);

/** @brief Decode and validate a persisted assignment record.
 * @param buffer Encoded record bytes.
 * @param length Record size.
 * @param state Receives decoded assignment.
 * @return 0 on decode; negative errno for malformed or unsupported records.
 */
int ef800_port_assignment_decode(const uint8_t *buffer, size_t length,
					 struct ef800_port_assignment_state *state);

/** @brief Initialize assignment service and bind persistence.
 * @param service Service state to initialize.
 * @param storage Persistence service.
 * @return 0 on initialization; negative errno for invalid arguments.
 */
int ef800_port_assignment_init(struct ef800_port_assignment_service *service,
				       struct ef800_persistence *storage);

/** @brief Load one port assignment from persistence.
 * @param service Assignment service.
 * @param port_id Port to load.
 * @return 0 on load/no record; negative errno on storage or malformed record.
 */
int ef800_port_assignment_load(struct ef800_port_assignment_service *service,
				       uint8_t port_id);

/** @brief Load assignments for every configured physical port.
 * @param service Assignment service.
 * @return 0 when loaded; negative errno on storage or validation failure.
 */
int ef800_port_assignment_load_all(struct ef800_port_assignment_service *service);

/** @brief Import a legacy builder assignment using its default connection.
 * @param service Assignment service.
 * @param port_id Destination port.
 * @param builder_id Legacy builder ID.
 * @return 0 on import; negative errno for incompatible or invalid assignment.
 */
int ef800_port_assignment_import(struct ef800_port_assignment_service *service,
					 uint8_t port_id, uint8_t builder_id);

/** @brief Import a legacy built-in assignment with explicit connection.
 * @param service Assignment service.
 * @param port_id Destination port.
 * @param builder_id Built-in builder identifier.
 * @param connection Connection capability.
 * @return 0 on import; negative errno for invalid/incompatible assignment.
 */
int ef800_port_assignment_import_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint8_t builder_id, uint32_t connection);

/** @brief Import a plugin assignment with explicit connection.
 * @param service Assignment service.
 * @param port_id Destination port.
 * @param plugin_id Plugin identifier.
 * @param connection Connection capability.
 * @return 0 on import; negative errno for invalid assignment.
 */
int ef800_port_assignment_import_plugin_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection);

/** @brief Persist a built-in assignment and update runtime state.
 * @param service Assignment service.
 * @param port_id Destination port.
 * @param builder_id Built-in builder identifier.
 * @param connection Connection capability.
 * @return 0 on commit; negative errno for invalid, incompatible, or I/O failure.
 */
int ef800_port_assignment_set_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint8_t builder_id, uint32_t connection);

/** @brief Persist a plugin assignment and update runtime state.
 * @param service Assignment service.
 * @param port_id Destination port.
 * @param plugin_id Plugin identifier.
 * @param connection Connection capability.
 * @return 0 on commit; negative errno for invalid or I/O failure.
 */
int ef800_port_assignment_set_plugin_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection);

/** @brief Choose the default connection supported by a port and builder.
 * @param port_id Port identifier.
 * @param builder_id Builder identifier.
 * @return Default connection ID, or NONE if no connection is compatible.
 */
uint32_t ef800_port_assignment_default_connection(uint8_t port_id,
								 uint8_t builder_id);

/** @brief Persist a built-in assignment using the default connection.
 * @param service Assignment service.
 * @param port_id Destination port.
 * @param builder_id Built-in builder identifier.
 * @return 0 on commit; negative errno for invalid assignment or I/O failure.
 */
int ef800_port_assignment_set(struct ef800_port_assignment_service *service,
				      uint8_t port_id, uint8_t builder_id);
/* Move the unique DebugDevice assignment to a new port. */

/** @brief Move the unique debug-device assignment to another port.
 * @param service Assignment service.
 * @param port_id Destination port.
 * @param connection Requested connection.
 * @return 0 on move; negative errno if unsupported or persistence fails.
 */
int ef800_port_assignment_move_debug(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint32_t connection);

/** @brief Remove a port assignment from runtime state and persistence.
 * @param service Assignment service.
 * @param port_id Port to clear.
 * @return 0 on removal; negative errno on invalid port or storage failure.
 */
int ef800_port_assignment_remove(struct ef800_port_assignment_service *service,
						 uint8_t port_id);
/* Clear a stale Port 4 MSD assignment even when its removal cannot be persisted. */

/** @brief Clear stale Port 4 MSC assignment when MSC is unsupported.
 * @param service Assignment service.
 * @param msd_supported Whether this target supports the MSC device.
 * @param cleared Receives whether in-memory assignment was cleared.
 * @param persisted Receives whether that change was persisted.
 * @return 0 when policy applied; negative errno on service errors.
 */
int ef800_port_assignment_enforce_msd_policy(
	struct ef800_port_assignment_service *service, bool msd_supported,
	bool *cleared, bool *persisted);

/** @brief Check whether a port currently has an assignment.
 * @param service Assignment service.
 * @param port_id Port to inspect.
 * @return True when assigned.
 */
bool ef800_port_assignment_is_assigned(
	const struct ef800_port_assignment_service *service, uint8_t port_id);

/** @brief Get the assigned built-in builder ID for a port.
 * @param service Assignment service.
 * @param port_id Port to inspect.
 * @return Builder ID, or NONE when absent/invalid.
 */
uint8_t ef800_port_assignment_get_builder(
	const struct ef800_port_assignment_service *service, uint8_t port_id);

/** @brief Get the assigned plugin ID for a port.
 * @param service Assignment service.
 * @param port_id Port to inspect.
 * @return Plugin ID, or zero when absent/invalid.
 */
uint32_t ef800_port_assignment_get_plugin_id(
	const struct ef800_port_assignment_service *service, uint8_t port_id);

/** @brief Get the active connection ID for a port assignment.
 * @param service Assignment service.
 * @param port_id Port to inspect.
 * @return Connection ID, or NONE when absent/invalid.
 */
uint32_t ef800_port_assignment_get_connection(
	const struct ef800_port_assignment_service *service, uint8_t port_id);

#endif /* EF800_PORTS_H */
