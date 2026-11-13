/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Expose the plugin's target-specific port binding. */
#ifndef EF800_PORT_SERVICE_H
#define EF800_PORT_SERVICE_H

#include "ef800_plugin_metadata.h"

#if EF800_PLUGIN_EXECUTION_MODEL == EF800_PLUGIN_EXECUTION_LEGACY_DIRECT
#include "ef800_legacy_port_service.h"
struct ef800_port_binding {
	const struct ef800_port_api *api;
};
typedef struct ef800_port_binding ef800_port_binding;
#else
/* Isolated workers receive only mailbox capabilities, never host pointers. */
#include "ef800_plugin_worker_sdk.h"
typedef struct ef800_plugin_worker_binding ef800_port_binding;
#endif

int32_t ef800_plugin_port_write(ef800_port_binding *port,
	const uint8_t *data, uint32_t length);
int32_t ef800_plugin_port_read(ef800_port_binding *port, uint8_t *data,
	uint32_t capacity, uint32_t *read_count, int32_t timeout_ms);

#endif /* EF800_PORT_SERVICE_H */
