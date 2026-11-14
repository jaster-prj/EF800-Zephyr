/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Define versioned host services available to trusted plugins. */
#ifndef EF800_PLUGIN_SERVICES_H
#define EF800_PLUGIN_SERVICES_H

#include <stddef.h>
#include <stdint.h>

#include "ef800_plugin_contract.h"

#define EF800_PLUGIN_SERVICE_VERSION 1u

struct ef800_plugin_port_service {
	uint32_t abi_version;
	uint32_t struct_size;
	/* Opaque argument shared by callbacks below. */
	void *context;
	/* Transfer bytes through the assigned port; return count or negative errno. */
	int32_t (*write)(void *context, const uint8_t *data, size_t length);
	int32_t (*read)(void *context, uint8_t *data, size_t capacity,
		int32_t timeout_ms);
};

struct ef800_plugin_workspace_service {
	uint32_t abi_version;
	uint32_t struct_size;
	/* Opaque argument shared by workspace callbacks. */
	void *context;
	/* Copy current workspace snapshot / append a bounded record. */
	int32_t (*snapshot)(void *context, char *destination,
		size_t capacity, size_t *length);
	int32_t (*append)(void *context, const char *data, size_t length);
};

struct ef800_plugin_clock_service {
	uint32_t abi_version;
	uint32_t struct_size;
	/* Opaque argument supplied to uptime_ms. */
	void *context;
	/* Return monotonic uptime in milliseconds. */
	uint64_t (*uptime_ms)(void *context);
};

struct ef800_plugin_log_service {
	uint32_t abi_version;
	uint32_t struct_size;
	/* Opaque argument supplied to write. */
	void *context;
	/* Submit one severity-tagged log message. */
	void (*write)(void *context, uint32_t level, const char *message);
};

struct ef800_plugin_services {
	uint32_t abi_version;
	uint32_t struct_size;
	const struct ef800_plugin_port_service *port;
	const struct ef800_plugin_workspace_service *workspace;
	const struct ef800_plugin_clock_service *clock;
	const struct ef800_plugin_log_service *log;
};

#endif
