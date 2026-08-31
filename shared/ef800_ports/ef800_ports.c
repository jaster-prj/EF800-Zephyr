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
		.title = "Parallel Port",
		.shortcut = '1',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_SERIAL_FLOW_CONTROL | EF800_PORT_CAP_PARALLEL,
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
		.title = "Serial Port",
		.shortcut = '3',
		.capabilities = EF800_PORT_CAP_SERIAL |
			EF800_PORT_CAP_SERIAL_FLOW_CONTROL,
	},
	{
		.id = 4U,
		.title = "USB Port",
		.shortcut = '4',
		.capabilities = EF800_PORT_CAP_USB,
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
	       (port->capabilities & device->capabilities) != 0U;
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
	if (!ef800_port_device_is_compatible(port, device)) {
		return -EINVAL;
	}

	memset(buffer, 0, EF800_PORT_ASSIGNMENT_RECORD_SIZE);
	buffer[0] = 'P';
	buffer[1] = 'A';
	buffer[2] = EF800_PORT_ASSIGNMENT_VERSION;
	buffer[3] = EF800_PORT_ASSIGNMENT_RECORD_SIZE;
	buffer[4] = state->port_id;
	buffer[5] = state->builder_id;
	/* Bytes 6 and 7 are reserved and must remain zero. */
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
		buffer[6] != 0U || buffer[7] != 0U ||
		sys_get_le16(&buffer[8]) != assignment_checksum(buffer)) {
		return -EINVAL;
	}

	decoded.port_id = buffer[4];
	decoded.builder_id = buffer[5];
	decoded.assigned = true;
	if (!ef800_port_device_is_compatible(ef800_port_get(decoded.port_id),
						     ef800_port_device_get(decoded.builder_id))) {
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
		current->assigned = false;
		current->builder_id = EF800_PORT_BUILDER_NONE;
		return 0;
	}
	if (ret != 0) {
		return ret;
	}
	ret = ef800_port_assignment_decode(buffer, length, &decoded);
	if (ret != 0 || decoded.port_id != port_id) {
		current->assigned = false;
		current->builder_id = EF800_PORT_BUILDER_NONE;
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

int ef800_port_assignment_set(struct ef800_port_assignment_service *service,
				      uint8_t port_id, uint8_t builder_id)
{
	struct ef800_port_assignment_state *current;
	struct ef800_port_assignment_state next = {
		.port_id = port_id,
		.builder_id = builder_id,
		.assigned = true,
	};
	uint8_t buffer[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	char name[16];
	size_t length;
	int ret;

	current = assignment_state(service, port_id);
	if (current == NULL || service->storage == NULL) {
		return -EINVAL;
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
