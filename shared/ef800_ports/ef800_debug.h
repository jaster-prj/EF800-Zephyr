/* SPDX-License-Identifier: Apache-2.0 */
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

int ef800_debug_router_init(struct ef800_debug_router *router,
				    ef800_debug_write_fn write, void *context);
int ef800_debug_router_set_target(struct ef800_debug_router *router,
					  uint8_t port_id);
void ef800_debug_router_clear_target(struct ef800_debug_router *router);
uint8_t ef800_debug_router_get_target(
	const struct ef800_debug_router *router);
int ef800_debug_router_write(struct ef800_debug_router *router,
				     const uint8_t *data, size_t length);

#endif /* EF800_DEBUG_H */
