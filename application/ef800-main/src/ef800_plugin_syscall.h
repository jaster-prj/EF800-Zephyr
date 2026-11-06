/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_SYSCALL_H
#define EF800_PLUGIN_SYSCALL_H

#include <zephyr/kernel.h>

#include "ef800_measurement.h"
#include "ef800_plugin_worker_abi.h"
#include "ef800_workspace.h"

/*
 * Kernel entry for plugin SDK stubs. The syscall receives only the address of
 * the user-mapped mailbox; the verifier authenticates the active worker and
 * copies every request/response field across the user boundary.
 */
__syscall int ef800_plugin_worker_syscall(
	struct ef800_plugin_worker_mailbox *mailbox);

#include <zephyr/syscalls/ef800_plugin_syscall.h>

/* ELF import resolver target: this code is callable from the user image and
 * only enters the generated Zephyr syscall wrapper. */
int ef800_plugin_worker_gateway_entry(
	struct ef800_plugin_worker_mailbox *mailbox);

/* Privileged supervisor API. Never expose this binding object to the worker. */
int ef800_plugin_gateway_session_bind(struct k_thread *worker,
	struct ef800_plugin_worker_mailbox *mailbox, uint32_t generation,
	uint32_t capability, uint32_t allowed_services, uint64_t deadline_ms,
	const struct ef800_measurement_transport *transport,
	const struct ef800_workspace *workspace);
int ef800_plugin_gateway_session_unbind(uint32_t generation);
int ef800_plugin_gateway_session_set_deadline(uint32_t generation,
	uint64_t deadline_ms);

#endif /* EF800_PLUGIN_SYSCALL_H */
