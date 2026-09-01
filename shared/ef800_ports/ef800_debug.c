/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_debug.h"

#include <errno.h>

int ef800_debug_router_init(struct ef800_debug_router *router,
				    ef800_debug_write_fn write, void *context)
{
	if (router == NULL || write == NULL) {
		return -EINVAL;
	}
	router->write = write;
	router->context = context;
	router->target_port_id = EF800_PORT_BUILDER_NONE;
	return 0;
}

int ef800_debug_router_set_target(struct ef800_debug_router *router,
					  uint8_t port_id)
{
	const struct ef800_port_descriptor *port = ef800_port_get(port_id);
	const struct ef800_port_device_descriptor *debug =
		ef800_port_device_get(EF800_PORT_BUILDER_DEBUG);

	if (router == NULL || router->write == NULL ||
		!ef800_port_device_is_compatible(port, debug)) {
		return -EINVAL;
	}
	router->target_port_id = port_id;
	return 0;
}

void ef800_debug_router_clear_target(struct ef800_debug_router *router)
{
	if (router != NULL) {
		router->target_port_id = EF800_PORT_BUILDER_NONE;
	}
}

uint8_t ef800_debug_router_get_target(
	const struct ef800_debug_router *router)
{
	return router == NULL ? EF800_PORT_BUILDER_NONE : router->target_port_id;
}

int ef800_debug_router_write(struct ef800_debug_router *router,
				     const uint8_t *data, size_t length)
{
	if (router == NULL || router->write == NULL ||
		(router->target_port_id == EF800_PORT_BUILDER_NONE) ||
		(data == NULL && length != 0U)) {
		return router != NULL && router->target_port_id ==
			EF800_PORT_BUILDER_NONE ? -ENODEV : -EINVAL;
	}
	return router->write(router->context, router->target_port_id, data, length);
}
