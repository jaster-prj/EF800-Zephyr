/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Provide debug-device port operations and serial settings. */
#ifndef EF800_DEBUG_H
#define EF800_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "ef800_ports.h"

typedef int (*ef800_debug_write_fn)(void *context, uint8_t port_id,
					    const uint8_t *data, size_t length);

struct ef800_debug_router {
	ef800_debug_write_fn write;
	void *context;
	uint8_t target_port_id;
};

/** @brief Initialize a debug router with its port-write callback.
 * @param router Router state to initialize.
 * @param write Callback used to transmit to a selected port.
 * @param context Opaque callback argument.
 * @return 0 on init; negative errno for invalid callback/state.
 */
int ef800_debug_router_init(struct ef800_debug_router *router,
				    ef800_debug_write_fn write, void *context);

/** @brief Select the physical port receiving debug output.
 * @param router Router state.
 * @param port_id Port identifier.
 * @return 0 on selection; negative errno for invalid port/state.
 */
int ef800_debug_router_set_target(struct ef800_debug_router *router,
					  uint8_t port_id);

/** @brief Clear the selected debug output port.
 * @param router Router state.
 * @return None.
 */
void ef800_debug_router_clear_target(struct ef800_debug_router *router);

/** @brief Get the currently selected debug output port.
 * @param router Router state.
 * @return Port identifier, or the no-target sentinel.
 */
uint8_t ef800_debug_router_get_target(
	const struct ef800_debug_router *router);

/** @brief Send bytes through the selected port callback.
 * @param router Router state.
 * @param data Bytes to transmit.
 * @param length Byte count.
 * @return 0 on write; negative errno when no target or callback fails.
 */
int ef800_debug_router_write(struct ef800_debug_router *router,
				     const uint8_t *data, size_t length);

#endif /* EF800_DEBUG_H */
