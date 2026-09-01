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
		.title = "Serial Port",
		.shortcut = '2',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_SERIAL_FLOW_CONTROL,
	},
	{
		.id = 3U,
		.title = "Multi-function Port",
		.shortcut = '3',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_SERIAL_FLOW_CONTROL | EF800_PORT_CAP_PARALLEL |
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
		.builder_id = EF800_PORT_BUILDER_ELTA3,
		.title = "Elta3",
		.shortcut = 'e',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_SERIAL_FLOW_CONTROL,
	},
	{
		.builder_id = EF800_PORT_BUILDER_FTS3XX,
		.title = "FTS3XX",
		.shortcut = 'f',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_SERIAL_FLOW_CONTROL,
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
	{ EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL, "UART4 with flow control" },
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

static bool builder_allowed_on_port(uint8_t port_id, uint8_t builder_id)
{
	if (builder_id == EF800_PORT_BUILDER_NONE) {
		return true;
	}
	if (builder_id == EF800_PORT_BUILDER_MSD && port_id != 4U) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_BLUENRG && port_id != 5U) {
		return false;
	}
	if ((builder_id == EF800_PORT_BUILDER_ELTA3 ||
	     builder_id == EF800_PORT_BUILDER_FTS3XX) && port_id >= 4U) {
		return false;
	}
	return true;
}

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
	       builder_allowed_on_port(port->id, device->builder_id) &&
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

	for (size_t index = 0U; index < 8U; ++index) {
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
	length = snprintf(name, capacity, "port_%u", port_id);
	return length < 0 || (size_t)length >= capacity ? -ENAMETOOLONG : 0;
}

int ef800_port_assignment_encode(const struct ef800_port_assignment_state *state,
					 uint8_t *buffer, size_t capacity, size_t *length)
{
	const struct ef800_port_descriptor *port;
	const struct ef800_port_device_descriptor *device;

	if (state == NULL || buffer == NULL || length == NULL ||
		capacity < EF800_PORT_ASSIGNMENT_RECORD_SIZE || !state->assigned) {
		return -EINVAL;
	}
	port = ef800_port_get(state->port_id);
	device = ef800_port_device_get(state->builder_id);
	if (!ef800_port_device_is_compatible(port, device) ||
		!ef800_port_connection_is_valid(state->port_id, state->connection) ||
		!ef800_port_device_supports_connection(state->builder_id,
								 state->connection)) {
		return -EINVAL;
	}

	memset(buffer, 0, EF800_PORT_ASSIGNMENT_RECORD_SIZE);
	buffer[0] = 'P';
	buffer[1] = 'A';
	buffer[2] = EF800_PORT_ASSIGNMENT_VERSION;
	buffer[3] = EF800_PORT_ASSIGNMENT_RECORD_SIZE;
	buffer[4] = state->port_id;
	buffer[5] = state->builder_id;
	buffer[6] = state->connection;
	/* Byte 7 is reserved and must remain zero. */
	sys_put_le16(assignment_checksum(buffer), &buffer[8]);
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
		buffer[7] != 0U ||
		sys_get_le16(&buffer[8]) != assignment_checksum(buffer)) {
		return -EINVAL;
	}

	decoded.port_id = buffer[4];
	decoded.builder_id = buffer[5];
	decoded.connection = buffer[6];
	decoded.assigned = true;
	if (!ef800_port_device_is_compatible(ef800_port_get(decoded.port_id),
								     ef800_port_device_get(decoded.builder_id)) ||
		!ef800_port_connection_is_valid(decoded.port_id, decoded.connection) ||
		!ef800_port_device_supports_connection(decoded.builder_id,
									 decoded.connection)) {
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
	char name[16];
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
			current->connection = EF800_PORT_CONNECTION_SPI;
			current->assigned = true;
			return 0;
		}
		current->assigned = false;
		current->builder_id = EF800_PORT_BUILDER_NONE;
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
		!builder_allowed_on_port(port_id, builder_id) ||
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

int ef800_port_assignment_set_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint8_t builder_id, uint32_t connection)
{
	struct ef800_port_assignment_state *current;
	const struct ef800_port_descriptor *port;
	struct ef800_port_assignment_state next = {
		.port_id = port_id,
		.builder_id = builder_id,
		.connection = connection,
		.assigned = true,
	};
	uint8_t buffer[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	char name[16];
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
	if (!builder_allowed_on_port(port_id, builder_id) ||
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

int ef800_port_assignment_set(struct ef800_port_assignment_service *service,
				      uint8_t port_id, uint8_t builder_id)
{
	uint32_t connection = ef800_port_assignment_default_connection(port_id,
										 builder_id);
	return ef800_port_assignment_set_with_connection(service, port_id,
							 builder_id, connection);
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
	char name[16];
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

uint32_t ef800_port_assignment_get_connection(
	const struct ef800_port_assignment_service *service, uint8_t port_id)
{
	const struct ef800_port_assignment_state *state =
		assignment_state_const(service, port_id);

	return state != NULL && state->assigned ? state->connection : 0U;
}
