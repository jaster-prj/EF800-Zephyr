/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_port_uart.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>

static const struct ef800_port_uart_route routes[] = {
	{
		.uart_id = EF800_PORT_UART3,
		.port_id = 1U,
		.connection = EF800_PORT_CONNECTION_SERIAL,
		.name = "UART3",
		.tx_pins = "PD8",
		.rx_pins = "PD9",
		.has_rts = false,
		.has_cts = false,
	},
	{
		.uart_id = EF800_PORT_UART1,
		.port_id = 2U,
		.connection = EF800_PORT_CONNECTION_SERIAL,
		.name = "USART1",
		.tx_pins = "PB6",
		.rx_pins = "PB7",
		.has_rts = false,
		.has_cts = false,
	},
	{
		.uart_id = EF800_PORT_UART4,
		.port_id = 3U,
		.connection = EF800_PORT_CONNECTION_SERIAL,
		.name = "UART4",
		.tx_pins = "PC10",
		.rx_pins = "PC11",
		.has_rts = false,
		.has_cts = false,
	},
	{
		.uart_id = EF800_PORT_UART1,
		.port_id = 2U,
		.connection = EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL,
		.name = "USART1",
		.tx_pins = "PB6",
		.rx_pins = "PB7",
		.has_rts = true,
		.has_cts = true,
	},
	{
		.uart_id = EF800_PORT_UART4,
		.port_id = 3U,
		.connection = EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL,
		.name = "UART4",
		.tx_pins = "PC10",
		.rx_pins = "PC11",
		.has_rts = true,
		.has_cts = false,
	},
};

const struct ef800_port_uart_route *ef800_port_uart_for_port(uint8_t port_id)
{
	for (size_t index = 0U; index < ARRAY_SIZE(routes); ++index) {
		if (routes[index].port_id == port_id &&
			routes[index].connection == EF800_PORT_CONNECTION_SERIAL) {
			return &routes[index];
		}
	}
	return NULL;
}

const struct ef800_port_uart_route *ef800_port_uart_for_connection(
	uint8_t port_id, uint32_t connection)
{
	for (size_t index = 0U; index < ARRAY_SIZE(routes); ++index) {
		if (routes[index].port_id == port_id &&
			routes[index].connection == connection) {
			return &routes[index];
		}
	}
	return NULL;
}

const struct ef800_port_uart_route *ef800_port_uart_for_id(
	enum ef800_port_uart_id uart_id)
{
	if (uart_id == EF800_PORT_UART_NONE) {
		return NULL;
	}
	for (size_t index = 0U; index < ARRAY_SIZE(routes); ++index) {
		if (routes[index].uart_id == uart_id) {
			return &routes[index];
		}
	}
	return NULL;
}


static enum ef800_port_uart_owner *owner_for_route(
	struct ef800_port_uart_registry *registry,
	const struct ef800_port_uart_route *route)
{
	return registry == NULL || route == NULL ? NULL :
		&registry->owners[route->uart_id];
}

static const enum ef800_port_uart_owner *owner_for_route_const(
	const struct ef800_port_uart_registry *registry,
	const struct ef800_port_uart_route *route)
{
	return registry == NULL || route == NULL ? NULL :
		&registry->owners[route->uart_id];
}

static const struct ef800_port_uart_route *route_for_port_and_builder(
	uint8_t port_id, uint8_t builder_id, uint32_t connection)
{
	if (connection == 0U) {
		connection = EF800_PORT_CONNECTION_SERIAL;
	}
	ARG_UNUSED(builder_id);
	return ef800_port_uart_for_connection(port_id, connection);
}

static const struct ef800_port_uart_route *route_for_port(uint8_t port_id)
{
	return ef800_port_uart_for_connection(port_id,
						      EF800_PORT_CONNECTION_SERIAL);
}

static int validate_claim(const struct ef800_port_uart_registry *registry,
				  uint8_t port_id, uint8_t builder_id)
{
	const struct ef800_port_descriptor *port = ef800_port_get(port_id);
	const struct ef800_port_device_descriptor *device =
		ef800_port_device_get(builder_id);

	if (registry == NULL || port == NULL || device == NULL ||
		!ef800_port_device_is_compatible(port, device)) {
		return -EINVAL;
	}
	return 0;
}

int ef800_port_uart_registry_init(struct ef800_port_uart_registry *registry,
					  enum ef800_port_uart_id console_uart)
{
	const struct ef800_port_uart_route *route;

	if (registry == NULL || console_uart >= EF800_PORT_UART_REGISTRY_SIZE) {
		return -EINVAL;
	}
	memset(registry, 0, sizeof(*registry));
	registry->startup_console_active = console_uart != EF800_PORT_UART_NONE;
	if (console_uart == EF800_PORT_UART_NONE) {
		return 0;
	}
	route = ef800_port_uart_for_id(console_uart);
	if (route == NULL) {
		return -EINVAL;
	}
	registry->console_reserved[console_uart] = true;
	return 0;
}

void ef800_port_uart_startup_complete(
	struct ef800_port_uart_registry *registry)
{
	if (registry != NULL) {
		registry->startup_console_active = false;
		memset(registry->console_reserved, 0,
		       sizeof(registry->console_reserved));
	}
}

int ef800_port_uart_can_claim(const struct ef800_port_uart_registry *registry,
				       uint8_t port_id, uint8_t builder_id,
				       uint32_t connection)
{
	const struct ef800_port_uart_route *route;
	const enum ef800_port_uart_owner *owner;

	if (validate_claim(registry, port_id, builder_id) != 0) {
		return -EINVAL;
	}
	if (!ef800_port_connection_is_valid(port_id, connection)) {
		return -EINVAL;
	}
	if (!ef800_port_device_supports_connection(builder_id, connection)) {
		return -EINVAL;
	}
	route = route_for_port_and_builder(port_id, builder_id, connection);
	if (route == NULL) {
		/* Port 4 is USB-only and therefore does not claim a UART. */
		return 0;
	}
	owner = owner_for_route_const(registry, route);
	if (owner == NULL) {
		return -EBUSY;
	}
	if (*owner == EF800_PORT_UART_OWNER_ASSIGNMENT &&
		registry->owner_ports[route->uart_id] != port_id) {
		return -EBUSY;
	}
	/* The fixed startup console is allowed to coexist with the application
	 * DebugDevice; all other active users still need an unclaimed UART. */
	if (registry->console_reserved[route->uart_id] &&
		builder_id != EF800_PORT_BUILDER_DEBUG) {
		return -EBUSY;
	}
	return 0;
}

int ef800_port_uart_claim(struct ef800_port_uart_registry *registry,
				 uint8_t port_id, uint8_t builder_id,
				 uint32_t connection)
{
	const struct ef800_port_uart_route *route;
	enum ef800_port_uart_owner *owner;
	int ret;

	ret = ef800_port_uart_can_claim(registry, port_id, builder_id, connection);
	if (ret != 0) {
		return ret;
	}
	route = route_for_port_and_builder(port_id, builder_id, connection);
	if (route == NULL) {
		return 0;
	}
	owner = owner_for_route(registry, route);
	*owner = EF800_PORT_UART_OWNER_ASSIGNMENT;
	registry->owner_ports[route->uart_id] = port_id;
	registry->owner_builders[route->uart_id] = builder_id;
	return 0;
}

int ef800_port_uart_release(struct ef800_port_uart_registry *registry,
				   uint8_t port_id, uint8_t builder_id,
				   uint32_t connection)
{
	const struct ef800_port_uart_route *route;
	enum ef800_port_uart_owner *owner;

	if (validate_claim(registry, port_id, builder_id) != 0) {
		return -EINVAL;
	}
	if (!ef800_port_connection_is_valid(port_id, connection)) {
		return -EINVAL;
	}
	if (!ef800_port_device_supports_connection(builder_id, connection)) {
		return -EINVAL;
	}
	route = route_for_port_and_builder(port_id, builder_id, connection);
	if (route == NULL) {
		return 0;
	}
	owner = owner_for_route(registry, route);
	if (owner == NULL) {
		return -EBUSY;
	}
	if (*owner == EF800_PORT_UART_OWNER_ASSIGNMENT &&
		registry->owner_builders[route->uart_id] != builder_id) {
		return -EINVAL;
	}
	if (*owner == EF800_PORT_UART_OWNER_ASSIGNMENT &&
		registry->owner_ports[route->uart_id] == port_id) {
		*owner = EF800_PORT_UART_OWNER_AVAILABLE;
		registry->owner_ports[route->uart_id] = 0U;
		registry->owner_builders[route->uart_id] = 0U;
	}
	return 0;
}

bool ef800_port_uart_is_claimed(
	const struct ef800_port_uart_registry *registry, uint8_t port_id)
{
	const struct ef800_port_uart_route *route = route_for_port(port_id);
	const enum ef800_port_uart_owner *owner =
		owner_for_route_const(registry, route);

	return owner != NULL &&
	       (registry->console_reserved[route->uart_id] ||
		*owner != EF800_PORT_UART_OWNER_AVAILABLE);
}
