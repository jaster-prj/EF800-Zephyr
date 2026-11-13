/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Route ports to UARTs and arbitrate console/device ownership. */
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

/** @brief Get the default UART route for a physical port.
 * @param port_id Port identifier.
 * @return Route pointer, or NULL when the port has no UART route.
 */
const struct ef800_port_uart_route *ef800_port_uart_for_port(uint8_t port_id);

/** @brief Resolve a UART route for a port and connection choice.
 * @param port_id Port identifier.
 * @param connection Connection capability.
 * @return Route pointer, or NULL when that combination has no UART.
 */
const struct ef800_port_uart_route *ef800_port_uart_for_connection(
	uint8_t port_id, uint32_t connection);

/** @brief Find a physical UART route by UART identifier.
 * @param uart_id UART identifier.
 * @return Route pointer, or NULL when unknown.
 */
const struct ef800_port_uart_route *ef800_port_uart_for_id(
	enum ef800_port_uart_id uart_id);


/** @brief Initialize UART ownership state and reserve the startup console.
 * @param registry Registry to initialize.
 * @param console_uart UART reserved for the console, or NONE.
 * @return 0 on initialization; negative errno for invalid input.
 */
int ef800_port_uart_registry_init(struct ef800_port_uart_registry *registry,
					  enum ef800_port_uart_id console_uart);

/** @brief Release startup-only console reservation after boot logging ends.
 * @param registry UART registry to update.
 * @return None.
 */
void ef800_port_uart_startup_complete(
	struct ef800_port_uart_registry *registry);

/** @brief Check whether an assignment can claim its resolved UART route.
 * @param registry Current ownership registry.
 * @param port_id Port identifier.
 * @param builder_id Assignment owner/builder identifier.
 * @param connection Connection choice.
 * @return 0 when claim is allowed; negative errno when unavailable/conflicting.
 */
int ef800_port_uart_can_claim(
	const struct ef800_port_uart_registry *registry, uint8_t port_id,
	uint8_t builder_id, uint32_t connection);

/** @brief Claim the UART route for one port assignment.
 * @param registry Registry to update.
 * @param port_id Port identifier.
 * @param builder_id Assignment owner/builder identifier.
 * @param connection Connection choice.
 * @return 0 on claim; negative errno when route is invalid or already owned.
 */
int ef800_port_uart_claim(struct ef800_port_uart_registry *registry,
				 uint8_t port_id, uint8_t builder_id, uint32_t connection);

/** @brief Release a UART claim owned by the specified assignment.
 * @param registry Registry to update.
 * @param port_id Port identifier.
 * @param builder_id Assignment owner/builder identifier.
 * @param connection Connection choice used for the claim.
 * @return 0 on release; negative errno when the claim does not match.
 */
int ef800_port_uart_release(struct ef800_port_uart_registry *registry,
				   uint8_t port_id, uint8_t builder_id, uint32_t connection);

/** @brief Check whether a port currently owns a UART route.
 * @param registry Registry to inspect.
 * @param port_id Port identifier.
 * @return True when the port has a claim.
 */
bool ef800_port_uart_is_claimed(
	const struct ef800_port_uart_registry *registry, uint8_t port_id);

#endif /* EF800_PORT_UART_H */
