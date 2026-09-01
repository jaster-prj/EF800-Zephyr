/* SPDX-License-Identifier: Apache-2.0 */
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
#define EF800_PORT_ASSIGNMENT_VERSION 1U
#define EF800_PORT_ASSIGNMENT_RECORD_SIZE 10U
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
	uint8_t connection;
	bool assigned;
};

struct ef800_port_assignment_service {
	struct ef800_persistence *storage;
	struct ef800_port_assignment_state states[EF800_PORT_COUNT];
};

size_t ef800_port_count(void);
const struct ef800_port_descriptor *ef800_port_at(size_t index);
const struct ef800_port_descriptor *ef800_port_get(uint8_t port_id);

size_t ef800_port_device_count(void);
const struct ef800_port_device_descriptor *ef800_port_device_at(size_t index);
const struct ef800_port_device_descriptor *ef800_port_device_get(uint8_t builder_id);
bool ef800_port_device_is_compatible(const struct ef800_port_descriptor *port,
					     const struct ef800_port_device_descriptor *device);
bool ef800_port_device_supports_connection(uint8_t builder_id,
							   uint32_t connection);
bool ef800_port_connection_is_valid(uint8_t port_id, uint32_t connection);
size_t ef800_port_connection_count(uint8_t port_id);
const struct ef800_port_connection_descriptor *ef800_port_connection_at(
	uint8_t port_id, size_t index);

int ef800_port_assignment_encode(const struct ef800_port_assignment_state *state,
					 uint8_t *buffer, size_t capacity, size_t *length);
int ef800_port_assignment_decode(const uint8_t *buffer, size_t length,
					 struct ef800_port_assignment_state *state);

int ef800_port_assignment_init(struct ef800_port_assignment_service *service,
				       struct ef800_persistence *storage);
int ef800_port_assignment_load(struct ef800_port_assignment_service *service,
				       uint8_t port_id);
int ef800_port_assignment_load_all(struct ef800_port_assignment_service *service);
int ef800_port_assignment_import(struct ef800_port_assignment_service *service,
					 uint8_t port_id, uint8_t builder_id);
int ef800_port_assignment_import_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint8_t builder_id, uint32_t connection);
int ef800_port_assignment_set_with_connection(
	struct ef800_port_assignment_service *service, uint8_t port_id,
	uint8_t builder_id, uint32_t connection);
uint32_t ef800_port_assignment_default_connection(uint8_t port_id,
								 uint8_t builder_id);
int ef800_port_assignment_set(struct ef800_port_assignment_service *service,
				      uint8_t port_id, uint8_t builder_id);
int ef800_port_assignment_remove(struct ef800_port_assignment_service *service,
					 uint8_t port_id);
bool ef800_port_assignment_is_assigned(
	const struct ef800_port_assignment_service *service, uint8_t port_id);
uint8_t ef800_port_assignment_get_builder(
	const struct ef800_port_assignment_service *service, uint8_t port_id);
uint32_t ef800_port_assignment_get_connection(
	const struct ef800_port_assignment_service *service, uint8_t port_id);

#endif /* EF800_PORTS_H */
