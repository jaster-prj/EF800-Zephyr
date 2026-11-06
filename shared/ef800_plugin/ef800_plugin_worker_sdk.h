/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_WORKER_SDK_H
#define EF800_PLUGIN_WORKER_SDK_H

#include <stddef.h>
#include <stdint.h>

#include "ef800_plugin_worker_abi.h"
#include "plugin_manager/plugin_abi.h"

#define EF800_PLUGIN_WORKER_BINDING_ABI_VERSION 1u

/*
 * This binding is copied into the worker's own stack or plugin data. The only
 * pointer is to the RW/XN mailbox mapped into the same user domain. It carries
 * no host context, device handle, or callback pointer.
 */
struct ef800_plugin_worker_binding {
	uint32_t abi_version;
	uint32_t struct_size;
	struct ef800_plugin_worker_mailbox *mailbox;
	uint32_t generation;
	uint32_t capability;
	uint32_t sequence;
	uint32_t reserved;
	uint64_t deadline_ms;
};

int ef800_plugin_worker_binding_from_services(
	const struct pm_service_table *services,
	struct ef800_plugin_worker_binding *binding);

int32_t ef800_plugin_worker_port_write(
	struct ef800_plugin_worker_binding *binding,
	const uint8_t *data, uint32_t length);
int32_t ef800_plugin_worker_port_read(
	struct ef800_plugin_worker_binding *binding,
	uint8_t *data, uint32_t capacity, uint32_t *length,
	int32_t timeout_ms);
int32_t ef800_plugin_worker_workspace_snapshot(
	struct ef800_plugin_worker_binding *binding, char *destination,
	uint32_t capacity, uint32_t *length);
int32_t ef800_plugin_worker_clock_uptime(
	struct ef800_plugin_worker_binding *binding, uint64_t *uptime_ms);
int32_t ef800_plugin_worker_log(
	struct ef800_plugin_worker_binding *binding, const char *message,
	uint32_t length);

/* One imported symbol, resolved by MainApplication to its SVC-only stub. */
int ef800_plugin_worker_syscall(struct ef800_plugin_worker_mailbox *mailbox);

#endif /* EF800_PLUGIN_WORKER_SDK_H */
