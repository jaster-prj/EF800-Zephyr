/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_ports.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

static const struct ef800_port_descriptor ports[] = {
	{
		.id = 1U,
		.title = "RS232 Port",
		.shortcut = '1',
		.capabilities = EF800_PORT_CAP_SERIAL,
	},
	{
		.id = 2U,
		.title = "RS232 Port",
		.shortcut = '2',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_SERIAL_FLOW_CONTROL,
	},
	{
		.id = 3U,
		.title = "Multi-function Port",
		.shortcut = '3',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_PARALLEL |
			EF800_PORT_CAP_I2C | EF800_PORT_CAP_SPI,
	},
	{
		.id = 4U,
		.title = "USB Port",
		.shortcut = '4',
		.capabilities = EF800_PORT_CAP_USB,
	},
	{
		.id = 5U,
		.title = "BlueNRG Port",
		.shortcut = '5',
		.capabilities = EF800_PORT_CAP_SPI,
		.fixed = true,
	},
};

static const struct ef800_port_device_descriptor devices[] = {
	{
		.builder_id = EF800_PORT_BUILDER_DEBUG,
		.title = "DebugDevice",
		.shortcut = 'd',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_SERIAL_FLOW_CONTROL | EF800_PORT_CAP_USB,
	},
	{
		.builder_id = EF800_PORT_BUILDER_MSD,
		.title = "USB MSD",
		.shortcut = 'u',
		.capabilities = EF800_PORT_CAP_USB,
	},
	{
		.builder_id = EF800_PORT_BUILDER_BLUENRG,
		.title = "BlueNRG",
		.shortcut = 'b',
		.capabilities = EF800_PORT_CAP_SPI,
	},
};

static const struct ef800_port_connection_descriptor port1_connections[] = {
	{ EF800_PORT_CONNECTION_SERIAL, "UART3 RS232" },
};

static const struct ef800_port_connection_descriptor port2_connections[] = {
	{ EF800_PORT_CONNECTION_SERIAL, "UART1" },
	{ EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL, "UART1 with flow control" },
};

static const struct ef800_port_connection_descriptor port3_connections[] = {
	{ EF800_PORT_CONNECTION_SERIAL, "UART4" },
	{ EF800_PORT_CONNECTION_PARALLEL, "Parallel" },
	{ EF800_PORT_CONNECTION_I2C, "I2C" },
	{ EF800_PORT_CONNECTION_SPI, "SPI" },
};

static const struct ef800_port_connection_descriptor port4_connections[] = {
	{ EF800_PORT_CONNECTION_USB_CDC, "USB CDC" },
	{ EF800_PORT_CONNECTION_USB_MSD, "USB MSD" },
};

static const struct ef800_port_connection_descriptor port5_connections[] = {
	{ EF800_PORT_CONNECTION_SPI, "SPI1 BlueNRG" },
};

size_t ef800_port_count(void)
{
	return ARRAY_SIZE(ports);
}

const struct ef800_port_descriptor *ef800_port_at(size_t index)
{
	return index < ARRAY_SIZE(ports) ? &ports[index] : NULL;
}

const struct ef800_port_descriptor *ef800_port_get(uint8_t port_id)
{
	for (size_t index = 0U; index < ARRAY_SIZE(ports); ++index) {
		if (ports[index].id == port_id) {
			return &ports[index];
		}
	}
	return NULL;
}

size_t ef800_port_device_count(void)
{
	return ARRAY_SIZE(devices);
}

const struct ef800_port_device_descriptor *ef800_port_device_at(size_t index)
{
	return index < ARRAY_SIZE(devices) ? &devices[index] : NULL;
}

const struct ef800_port_device_descriptor *ef800_port_device_get(uint8_t builder_id)
{
	for (size_t index = 0U; index < ARRAY_SIZE(devices); ++index) {
		if (devices[index].builder_id == builder_id) {
			return &devices[index];
		}
	}
	return NULL;
}

bool ef800_port_device_is_compatible(
	const struct ef800_port_descriptor *port,
	const struct ef800_port_device_descriptor *device)
{
	return port != NULL && device != NULL &&
	       ef800_port_builder_allowed_on_port(port->id, device->builder_id) &&
	       (!port->fixed || device->builder_id == EF800_PORT_BUILDER_BLUENRG) &&
	       (port->capabilities & device->capabilities) != 0U;
}

bool ef800_port_device_supports_connection(uint8_t builder_id,
						   uint32_t connection)
{
	const struct ef800_port_device_descriptor *device =
		ef800_port_device_get(builder_id);
	uint32_t capability;

	if (device == NULL || connection == EF800_PORT_CONNECTION_NONE) {
		return false;
	}
	switch (connection) {
	case EF800_PORT_CONNECTION_SERIAL:
		capability = EF800_PORT_CAP_SERIAL;
		break;
	case EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL:
		capability = EF800_PORT_CAP_SERIAL_FLOW_CONTROL;
		break;
	case EF800_PORT_CONNECTION_PARALLEL:
		capability = EF800_PORT_CAP_PARALLEL;
		break;
	case EF800_PORT_CONNECTION_USB_CDC:
	case EF800_PORT_CONNECTION_USB_MSD:
		capability = EF800_PORT_CAP_USB;
		break;
	case EF800_PORT_CONNECTION_I2C:
		capability = EF800_PORT_CAP_I2C;
		break;
	case EF800_PORT_CONNECTION_SPI:
		capability = EF800_PORT_CAP_SPI;
		break;
	default:
		return false;
	}
	if ((device->capabilities & capability) == 0U) {
		return false;
	}
	if (!ef800_port_builder_connection_valid(builder_id, connection)) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_MSD &&
		connection != EF800_PORT_CONNECTION_USB_MSD) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_BLUENRG &&
		connection != EF800_PORT_CONNECTION_SPI) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_DEBUG &&
		connection == EF800_PORT_CONNECTION_USB_MSD) {
		return false;
	}
	if (builder_id != EF800_PORT_BUILDER_DEBUG &&
		builder_id != EF800_PORT_BUILDER_MSD &&
		(connection == EF800_PORT_CONNECTION_USB_CDC ||
		 connection == EF800_PORT_CONNECTION_USB_MSD)) {
		return false;
	}
	return true;
}

bool ef800_port_connection_is_valid(uint8_t port_id, uint32_t connection)
{
	for (size_t index = 0U; index < ef800_port_connection_count(port_id); ++index) {
		const struct ef800_port_connection_descriptor *descriptor =
			ef800_port_connection_at(port_id, index);
		if (descriptor != NULL && descriptor->capability == connection) {
			return true;
		}
	}
	return false;
}

size_t ef800_port_connection_count(uint8_t port_id)
{
	switch (port_id) {
	case 1U:
		return ARRAY_SIZE(port1_connections);
	case 2U:
		return ARRAY_SIZE(port2_connections);
	case 3U:
		return ARRAY_SIZE(port3_connections);
	case 4U:
		return ARRAY_SIZE(port4_connections);
	case 5U:
		return ARRAY_SIZE(port5_connections);
	default:
		return 0U;
	}
}

const struct ef800_port_connection_descriptor *ef800_port_connection_at(
	uint8_t port_id, size_t index)
{
	const struct ef800_port_connection_descriptor *connections = NULL;
	size_t count = 0U;

	switch (port_id) {
	case 1U:
		connections = port1_connections;
		count = ARRAY_SIZE(port1_connections);
		break;
	case 2U:
		connections = port2_connections;
		count = ARRAY_SIZE(port2_connections);
		break;
	case 3U:
		connections = port3_connections;
		count = ARRAY_SIZE(port3_connections);
		break;
	case 4U:
		connections = port4_connections;
		count = ARRAY_SIZE(port4_connections);
		break;
	case 5U:
		connections = port5_connections;
		count = ARRAY_SIZE(port5_connections);
		break;
	default:
		break;
	}
	return index < count ? &connections[index] : NULL;
}

static uint16_t assignment_checksum(const uint8_t *buffer)
{
	uint16_t checksum = 0xFFFFU;

	for (size_t index = 0U; index < 14U; ++index) {
		checksum ^= (uint16_t)buffer[index] << 8;
		for (uint8_t bit = 0U; bit < 8U; ++bit) {
			checksum = (checksum & 0x8000U) != 0U ?
				(uint16_t)((checksum << 1) ^ 0x1021U) :
				(uint16_t)(checksum << 1);
		}
	}
	return checksum;
}

static int assignment_file_name(uint8_t port_id, char *name, size_t capacity)
{
	int length;

	if (ef800_port_get(port_id) == NULL || name == NULL || capacity == 0U) {
		return -EINVAL;
	}
	length = snprintf(name, capacity, "%s%u", EF800_PORT_ASSIGNMENT_FILE_PREFIX,
				 port_id);
	return length < 0 || (size_t)length >= capacity ? -ENAMETOOLONG : 0;
}

int ef800_port_assignment_encode(const struct ef800_port_assignment_state *state,
					 uint8_t *buffer, size_t capacity, size_t *length)
{
	const struct ef800_port_descriptor *port;
	const struct ef800_port_device_descriptor *device;
	uint32_t plugin_id;

	if (state == NULL || buffer == NULL || length == NULL ||
		capacity < EF800_PORT_ASSIGNMENT_RECORD_SIZE || !state->assigned) {
		return -EINVAL;
	}
	plugin_id = state->plugin_id != 0U ? state->plugin_id : state->builder_id;
	port = ef800_port_get(state->port_id);
	device = ef800_port_device_get(state->builder_id);
	if (plugin_id == EF800_PORT_BUILDER_NONE ||
		(state->builder_id != 0U && !ef800_port_device_is_compatible(port, device)) ||
		!ef800_port_connection_is_valid(state->port_id, state->connection) ||
		(state->builder_id != 0U &&
			(!ef800_port_assignment_record_valid(state->port_id, state->builder_id,
											 state->connection) ||
			 !ef800_port_device_supports_connection(state->builder_id,
											 state->connection)))) {
		return -EINVAL;
	}

	memset(buffer, 0, EF800_PORT_ASSIGNMENT_RECORD_SIZE);
	buffer[0] = 'P';
	buffer[1] = 'A';
	buffer[2] = EF800_PORT_ASSIGNMENT_VERSION;
	buffer[3] = EF800_PORT_ASSIGNMENT_RECORD_SIZE;
	buffer[4] = state->port_id;
	buffer[5] = 0U;
	buffer[6] = 0U;
	buffer[7] = state->builder_id == EF800_PORT_BUILDER_NONE ? 0xA5U : 0U;
	sys_put_le32(plugin_id, &buffer[8]);
	sys_put_le32(state->connection, &buffer[12]);
	/* The record is self-describing; the full record is covered by its checksum. */
	sys_put_le16(assignment_checksum(buffer), &buffer[14]);
	*length = EF800_PORT_ASSIGNMENT_RECORD_SIZE;
	return 0;
}

int ef800_port_assignment_decode(const uint8_t *buffer, size_t length,
					 struct ef800_port_assignment_state *state)
{
	struct ef800_port_assignment_state decoded;

	if (buffer == NULL || state == NULL ||
		length != EF800_PORT_ASSIGNMENT_RECORD_SIZE ||
		buffer[0] != 'P' || buffer[1] != 'A' ||
		buffer[2] != EF800_PORT_ASSIGNMENT_VERSION ||
		buffer[3] != EF800_PORT_ASSIGNMENT_RECORD_SIZE ||
		buffer[5] != 0U || buffer[6] != 0U ||
		(buffer[7] != 0U && buffer[7] != 0xA5U) ||
		sys_get_le16(&buffer[14]) != assignment_checksum(buffer)) {
		return -EINVAL;
	}

	decoded.port_id = buffer[4];
	decoded.plugin_id = sys_get_le32(&buffer[8]);
	/* Preserve legacy builder records, but mark external plugin records so a
	 * native package ID cannot collide with a built-in builder ID. */
	decoded.builder_id = buffer[7] == 0xA5U ? EF800_PORT_BUILDER_NONE :
		(decoded.plugin_id <= UINT8_MAX ? (uint8_t)decoded.plugin_id :
		 EF800_PORT_BUILDER_NONE);
	decoded.connection = (uint8_t)sys_get_le32(&buffer[12]);
	decoded.assigned = true;
	if (decoded.plugin_id == 0U ||
		!ef800_port_connection_is_valid(decoded.port_id, decoded.connection)) {
		return -EINVAL;
	}
	*state = decoded;
	return 0;
}

static struct ef800_port_assignment_state *assignment_state(
	struct ef800_port_assignment_service *service, uint8_t port_id)
{
	if (service == NULL || ef800_port_get(port_id) == NULL) {
		return NULL;
	}
	return &service->states[port_id - 1U];
}

static const struct ef800_port_assignment_state *assignment_state_const(
	const struct ef800_port_assignment_service *service, uint8_t port_id)
{
	if (service == NULL || ef800_port_get(port_id) == NULL) {
		return NULL;
	}
	return &service->states[port_id - 1U];
}

int ef800_port_assignment_init(struct ef800_port_assignment_service *service,
				       struct ef800_persistence *storage)
{
	if (service == NULL || storage == NULL || storage->backend == NULL) {
		return -EINVAL;
	}
	memset(service, 0, sizeof(*service));
	service->storage = storage;
	for (size_t index = 0U; index < ARRAY_SIZE(service->states); ++index) {
		service->states[index].port_id = ports[index].id;
	}
	service->states[4].builder_id = EF800_PORT_BUILDER_BLUENRG;
	service->states[4].plugin_id = EF800_PORT_BUILDER_BLUENRG;
	service->states[4].connection = EF800_PORT_CONNECTION_SPI;
	service->states[4].assigned = true;
	return 0;
}

int ef800_port_assignment_load(struct ef800_port_assignment_service *service,
				       uint8_t port_id)
{
	struct ef800_port_assignment_state *current;
	struct ef800_port_assignment_state decoded;
	uint8_t buffer[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	char name[32];
	size_t length;
	int ret;

	current = assignment_state(service, port_id);
	if (current == NULL || service->storage == NULL) {
		return -EINVAL;
	}
	ret = assignment_file_name(port_id, name, sizeof(name));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_read(service->storage,
					     EF800_PERSISTENCE_CONFIGURATION, name,
					     buffer, sizeof(buffer), &length);
	if (ret == -ENOENT) {
		if (port_id == 5U) {
			current->builder_id = EF800_PORT_BUILDER_BLUENRG;
			current->plugin_id = EF800_PORT_BUILDER_BLUENRG;
			current->connection = EF800_PORT_CONNECTION_SPI;
			current->assigned = true;
			return 0;
		}
		current->assigned = false;
		current->builder_id = EF800_PORT_BUILDER_NONE;
		current->plugin_id = EF800_PORT_BUILDER_NONE;
		current->connection = 0U;
		return 0;
	}
	if (ret != 0) {
		return ret;
	}
	ret = ef800_port_assignment_decode(buffer, length, &decoded);
	if (ret != 0 || decoded.port_id != port_id ||
		(port_id == 5U && (decoded.builder_id != EF800_PORT_BUILDER_BLUENRG ||
			decoded.connection != EF800_PORT_CONNECTION_SPI))) {
		if (port_id == 5U) {
			current->builder_id = EF800_PORT_BUILDER_BLUENRG;
			current->connection = EF800_PORT_CONNECTION_SPI;
			current->assigned = true;
			return ret != 0 ? ret : -EINVAL;
		}
		current->assigned = false;
		current->builder_id = EF800_PORT_BUILDER_NONE;
		current->connection = EF800_PORT_CONNECTION_NONE;
		return ret != 0 ? ret : -EINVAL;
	}
	*current = decoded;
	return 0;
}

int ef800_port_assignment_load_all(struct ef800_port_assignment_service *service)
{
	int first_error = 0;

	if (service == NULL || service->storage == NULL) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < ARRAY_SIZE(ports); ++index) {
		int ret = ef800_port_assignment_load(service, ports[index].id);
		if (ret != 0 && first_error == 0) {
			first_error = ret;
		}
	}
	return first_error;
}

int ef800_port_assignment_import_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint8_t builder_id, uint32_t connection)
{
	struct ef800_port_assignment_state *current;
	const struct ef800_port_descriptor *port;
	const struct ef800_port_device_descriptor *device;

	current = assignment_state(service, port_id);
	port = ef800_port_get(port_id);
	device = ef800_port_device_get(builder_id);
	if (current == NULL || port == NULL ||
		(builder_id == EF800_PORT_BUILDER_NONE &&
		 connection != EF800_PORT_CONNECTION_NONE) ||
		(builder_id != EF800_PORT_BUILDER_NONE &&
		 (!ef800_port_device_is_compatible(port, device) ||
		  !ef800_port_connection_is_valid(port_id, connection) ||
		  !ef800_port_device_supports_connection(builder_id, connection))) ||
		!ef800_port_builder_allowed_on_port(port_id, builder_id) ||
		(port->fixed && (builder_id != EF800_PORT_BUILDER_BLUENRG ||
			connection != EF800_PORT_CONNECTION_SPI))) {
		return -EINVAL;
	}
	if (builder_id == EF800_PORT_BUILDER_DEBUG) {
		for (size_t index = 0U; index < EF800_PORT_COUNT; ++index) {
			if (service->states[index].assigned &&
				service->states[index].builder_id ==
				EF800_PORT_BUILDER_DEBUG &&
				service->states[index].port_id != port_id) {
				return -EADDRINUSE;
			}
		}
	}
	current->builder_id = builder_id;
	current->connection = builder_id == EF800_PORT_BUILDER_NONE ?
		EF800_PORT_CONNECTION_NONE : connection;
	current->assigned = builder_id != EF800_PORT_BUILDER_NONE;
	return 0;
}

int ef800_port_assignment_import(struct ef800_port_assignment_service *service,
					 uint8_t port_id, uint8_t builder_id)
{
	uint32_t connection = builder_id == EF800_PORT_BUILDER_NONE ?
		EF800_PORT_CONNECTION_NONE :
		ef800_port_assignment_default_connection(port_id, builder_id);

	return ef800_port_assignment_import_with_connection(service, port_id,
									builder_id, connection);
}

int ef800_port_assignment_import_plugin_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection)
{
	struct ef800_port_assignment_state *current = assignment_state(service, port_id);
	const struct ef800_port_descriptor *port = ef800_port_get(port_id);

	if (current == NULL || port == NULL || plugin_id == 0U ||
		!ef800_port_connection_is_valid(port_id, connection) ||
		(port->fixed && plugin_id != EF800_PORT_BUILDER_BLUENRG)) {
		return -EINVAL;
	}
	current->builder_id = plugin_id <= UINT8_MAX ? (uint8_t)plugin_id : 0U;
	current->plugin_id = plugin_id;
	current->connection = (uint8_t)connection;
	current->assigned = true;
	return 0;
}

int ef800_port_assignment_set_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint8_t builder_id, uint32_t connection)
{
	struct ef800_port_assignment_state *current;
	const struct ef800_port_descriptor *port;
	struct ef800_port_assignment_state next = {
		.port_id = port_id,
		.builder_id = builder_id,
		.plugin_id = builder_id,
		.connection = connection,
		.assigned = true,
	};
	uint8_t buffer[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	char name[32];
	size_t length;
	int ret;

	current = assignment_state(service, port_id);
	port = ef800_port_get(port_id);
	if (current == NULL || service->storage == NULL) {
		return -EINVAL;
	}
	if (builder_id == EF800_PORT_BUILDER_NONE ||
		!ef800_port_connection_is_valid(port_id, connection)) {
		return -EINVAL;
	}
	if (!ef800_port_builder_allowed_on_port(port_id, builder_id) ||
		!ef800_port_device_supports_connection(builder_id, connection) ||
		(port->fixed && (builder_id != EF800_PORT_BUILDER_BLUENRG ||
		 connection != EF800_PORT_CONNECTION_SPI))) {
		return -EINVAL;
	}
	if (builder_id == EF800_PORT_BUILDER_DEBUG) {
		for (size_t index = 0U; index < EF800_PORT_COUNT; ++index) {
			if (service->states[index].assigned &&
				service->states[index].builder_id ==
				EF800_PORT_BUILDER_DEBUG &&
				service->states[index].port_id != port_id) {
				return -EADDRINUSE;
			}
		}
	}
	ret = ef800_port_assignment_encode(&next, buffer, sizeof(buffer), &length);
	if (ret != 0) {
		return ret;
	}
	ret = assignment_file_name(port_id, name, sizeof(name));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_write(service->storage,
					      EF800_PERSISTENCE_CONFIGURATION, name,
					      buffer, length);
	if (ret == 0) {
		*current = next;
	}
	return ret;
}

int ef800_port_assignment_set_plugin_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection)
{
	struct ef800_port_assignment_state *current;
	struct ef800_port_assignment_state next = {
		.port_id = port_id,
		/* External plugin assignments must not be validated as legacy device
		 * builders.  This also keeps native package IDs separate from the
		 * one-byte built-in builder namespace. */
		.builder_id = EF800_PORT_BUILDER_NONE,
		.plugin_id = plugin_id,
		.connection = (uint8_t)connection,
		.assigned = true,
	};
	uint8_t buffer[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	char name[32];
	size_t length;
	int ret;

	current = assignment_state(service, port_id);
	if (current == NULL || service->storage == NULL || plugin_id == 0U ||
		!ef800_port_connection_is_valid(port_id, connection)) {
		return -EINVAL;
	}
	ret = ef800_port_assignment_encode(&next, buffer, sizeof(buffer), &length);
	if (ret != 0 || assignment_file_name(port_id, name, sizeof(name)) != 0) {
		return ret != 0 ? ret : -ENAMETOOLONG;
	}
	ret = ef800_persistence_write(service->storage,
		EF800_PERSISTENCE_CONFIGURATION, name, buffer, length);
	if (ret == 0) {
		*current = next;
	}
	return ret;
}

int ef800_port_assignment_set(struct ef800_port_assignment_service *service,
				      uint8_t port_id, uint8_t builder_id)
{
	uint32_t connection = ef800_port_assignment_default_connection(port_id,
										 builder_id);
	return ef800_port_assignment_set_with_connection(service, port_id,
							 builder_id, connection);
}

int ef800_port_assignment_move_debug(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint32_t connection)
{
	struct ef800_port_assignment_state old_state = {0};
	struct ef800_port_assignment_state destination_state = {0};
	bool have_old = false;
	bool have_destination = false;
	int result;

	if (service == NULL ||
		port_id == 0U || port_id > EF800_PORT_COUNT ||
		!ef800_port_connection_is_valid(port_id, connection) ||
		!ef800_port_device_supports_connection(EF800_PORT_BUILDER_DEBUG,
									connection) ||
		!ef800_port_builder_allowed_on_port(port_id,
									EF800_PORT_BUILDER_DEBUG)) {
		return -EINVAL;
	}

	for (size_t index = 0U; index < EF800_PORT_COUNT; ++index) {
		const struct ef800_port_assignment_state *state =
			&service->states[index];

		if (state->assigned && state->builder_id == EF800_PORT_BUILDER_DEBUG) {
			old_state = *state;
			have_old = true;
		}
		if (state->port_id == port_id && state->assigned) {
			destination_state = *state;
			have_destination = true;
		}
	}

	if (have_old && old_state.port_id == port_id &&
		old_state.connection == connection) {
		return 0;
	}
	if (have_old) {
		service->states[old_state.port_id - 1U].assigned = false;
		service->states[old_state.port_id - 1U].builder_id =
			EF800_PORT_BUILDER_NONE;
		service->states[old_state.port_id - 1U].connection =
			EF800_PORT_CONNECTION_NONE;
	}
	result = ef800_port_assignment_set_with_connection(service, port_id,
									EF800_PORT_BUILDER_DEBUG,
									connection);
	if (result == -ENOTSUP) {
		if (have_destination && destination_state.port_id != port_id) {
			service->states[port_id - 1U] = destination_state;
		}
		service->states[port_id - 1U].builder_id = EF800_PORT_BUILDER_DEBUG;
		service->states[port_id - 1U].connection = connection;
		service->states[port_id - 1U].assigned = true;
		return 0;
	}
	if (result != 0) {
		if (have_old) {
			service->states[old_state.port_id - 1U] = old_state;
		}
		if (have_destination) {
			service->states[destination_state.port_id - 1U] = destination_state;
		}
		return result;
	}

	if (have_old && old_state.port_id != port_id) {
		result = ef800_port_assignment_remove(service, old_state.port_id);
		if (result != 0) {
			(void)ef800_port_assignment_remove(service, port_id);
			if (have_destination) {
				(void)ef800_port_assignment_set_with_connection(service,
					destination_state.port_id, destination_state.builder_id,
					destination_state.connection);
			}
			(void)ef800_port_assignment_set_with_connection(service,
				old_state.port_id, old_state.builder_id, old_state.connection);
			return result;
		}
	}
	return 0;
}

uint32_t ef800_port_assignment_default_connection(uint8_t port_id,
								  uint8_t builder_id)
{
	if (builder_id == EF800_PORT_BUILDER_NONE) {
		return EF800_PORT_CONNECTION_NONE;
	}
	if (port_id == 4U) {
		return builder_id == EF800_PORT_BUILDER_MSD ?
			EF800_PORT_CONNECTION_USB_MSD : EF800_PORT_CONNECTION_USB_CDC;
	}
	if (port_id == 5U) {
		return EF800_PORT_CONNECTION_SPI;
	}
	return EF800_PORT_CONNECTION_SERIAL;
}

int ef800_port_assignment_remove(struct ef800_port_assignment_service *service,
					 uint8_t port_id)
{
	struct ef800_port_assignment_state *current;
	char name[32];
	int ret;

	current = assignment_state(service, port_id);
	if (current == NULL || service->storage == NULL) {
		return -EINVAL;
	}
	if (ef800_port_get(port_id)->fixed) {
		return -EPERM;
	}
	ret = assignment_file_name(port_id, name, sizeof(name));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_remove(service->storage,
					       EF800_PERSISTENCE_CONFIGURATION, name);
	if (ret == -ENOENT) {
		ret = 0;
	}
	if (ret == 0) {
		current->assigned = false;
		current->builder_id = EF800_PORT_BUILDER_NONE;
		current->plugin_id = EF800_PORT_BUILDER_NONE;
		current->connection = 0U;
	}
	return ret;
}

bool ef800_port_assignment_is_assigned(
	const struct ef800_port_assignment_service *service, uint8_t port_id)
{
	const struct ef800_port_assignment_state *state =
		assignment_state_const(service, port_id);

	return state != NULL && state->assigned;
}

uint8_t ef800_port_assignment_get_builder(
	const struct ef800_port_assignment_service *service, uint8_t port_id)
{
	const struct ef800_port_assignment_state *state =
		assignment_state_const(service, port_id);

	return state != NULL && state->assigned ? state->builder_id :
		EF800_PORT_BUILDER_NONE;
}

uint32_t ef800_port_assignment_get_plugin_id(
	const struct ef800_port_assignment_service *service, uint8_t port_id)
{
	const struct ef800_port_assignment_state *state =
		assignment_state_const(service, port_id);

	return state != NULL && state->assigned ?
		(state->plugin_id != 0U ? state->plugin_id : state->builder_id) :
		EF800_PORT_BUILDER_NONE;
}

uint32_t ef800_port_assignment_get_connection(
	const struct ef800_port_assignment_service *service, uint8_t port_id)
{
	const struct ef800_port_assignment_state *state =
		assignment_state_const(service, port_id);

	return state != NULL && state->assigned ? state->connection : 0U;
}
