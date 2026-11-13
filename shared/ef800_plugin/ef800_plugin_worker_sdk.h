/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Provide isolated worker SDK calls through a validated mailbox. */
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

/** @brief Resolve the worker binding from the plugin-manager service table.
 * @param services Manager-provided service table.
 * @param binding Receives mailbox, generation, capability, and deadline.
 * @return 0 on resolution; negative errno for missing/invalid service data.
 */
int ef800_plugin_worker_binding_from_services(
	const struct pm_service_table *services,
	struct ef800_plugin_worker_binding *binding);

/** @brief Write bytes through the session-authorized port service.
 * @param binding Worker binding.
 * @param data Bytes to transmit.
 * @param length Number of bytes.
 * @return Bytes written or negative errno.
 */
int32_t ef800_plugin_worker_port_write(
	struct ef800_plugin_worker_binding *binding,
	const uint8_t *data, uint32_t length);

/** @brief Read bytes through the session-authorized port service.
 * @param binding Worker binding.
 * @param data Destination buffer.
 * @param capacity Buffer capacity.
 * @param length Receives bytes read.
 * @param timeout_ms Maximum wait time.
 * @return 0 on read; negative errno on timeout or service failure.
 */
int32_t ef800_plugin_worker_port_read(
	struct ef800_plugin_worker_binding *binding,
	uint8_t *data, uint32_t capacity, uint32_t *length,
	int32_t timeout_ms);

/** @brief Request a bounded workspace snapshot from the host.
 * @param binding Worker binding.
 * @param destination Snapshot buffer.
 * @param capacity Buffer capacity.
 * @param length Receives snapshot length.
 * @return 0 on snapshot; negative errno on bounds/service failure.
 */
int32_t ef800_plugin_worker_workspace_snapshot(
	struct ef800_plugin_worker_binding *binding, char *destination,
	uint32_t capacity, uint32_t *length);

/** @brief Query host monotonic uptime.
 * @param binding Worker binding.
 * @param uptime_ms Receives milliseconds since boot.
 * @return 0 on query; negative errno on service failure.
 */
int32_t ef800_plugin_worker_clock_uptime(
	struct ef800_plugin_worker_binding *binding, uint64_t *uptime_ms);

/** @brief Send a bounded message to the host logging service.
 * @param binding Worker binding.
 * @param message Message bytes.
 * @param length Message length.
 * @return 0 on log; negative errno on invalid message/service failure.
 */
int32_t ef800_plugin_worker_log(
	struct ef800_plugin_worker_binding *binding, const char *message,
	uint32_t length);

/* One imported symbol, resolved by EF800Application to its SVC-only stub. */
int ef800_plugin_worker_syscall(struct ef800_plugin_worker_mailbox *mailbox);

#endif /* EF800_PLUGIN_WORKER_SDK_H */
