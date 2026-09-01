/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PORT_UART_H
#define EF800_PORT_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "ef800_ports.h"

enum ef800_port_uart_id {
	EF800_PORT_UART_NONE = 0U,
	EF800_PORT_UART3,
	EF800_PORT_UART1,
	EF800_PORT_UART4,
};

enum ef800_port_uart_owner {
	EF800_PORT_UART_OWNER_AVAILABLE = 0U,
	EF800_PORT_UART_OWNER_CONSOLE,
	EF800_PORT_UART_OWNER_ASSIGNMENT,
};

struct ef800_port_uart_route {
	enum ef800_port_uart_id uart_id;
	uint8_t port_id;
	uint32_t connection;
	const char *name;
	const char *tx_pins;
	const char *rx_pins;
	bool has_rts;
	bool has_cts;
};

#define EF800_PORT_UART_REGISTRY_SIZE 4U

struct ef800_port_uart_registry {
	enum ef800_port_uart_owner owners[EF800_PORT_UART_REGISTRY_SIZE];
	uint8_t owner_ports[EF800_PORT_UART_REGISTRY_SIZE];
	uint8_t owner_builders[EF800_PORT_UART_REGISTRY_SIZE];
	bool console_reserved[EF800_PORT_UART_REGISTRY_SIZE];
	bool startup_console_active;
};

const struct ef800_port_uart_route *ef800_port_uart_for_port(uint8_t port_id);
const struct ef800_port_uart_route *ef800_port_uart_for_connection(
	uint8_t port_id, uint32_t connection);
const struct ef800_port_uart_route *ef800_port_uart_for_id(
	enum ef800_port_uart_id uart_id);

int ef800_port_uart_registry_init(struct ef800_port_uart_registry *registry,
					  enum ef800_port_uart_id console_uart);
void ef800_port_uart_startup_complete(
	struct ef800_port_uart_registry *registry);
int ef800_port_uart_can_claim(
	const struct ef800_port_uart_registry *registry, uint8_t port_id,
	uint8_t builder_id, uint32_t connection);
int ef800_port_uart_claim(struct ef800_port_uart_registry *registry,
				 uint8_t port_id, uint8_t builder_id, uint32_t connection);
int ef800_port_uart_release(struct ef800_port_uart_registry *registry,
				   uint8_t port_id, uint8_t builder_id, uint32_t connection);
bool ef800_port_uart_is_claimed(
	const struct ef800_port_uart_registry *registry, uint8_t port_id);

#endif /* EF800_PORT_UART_H */
