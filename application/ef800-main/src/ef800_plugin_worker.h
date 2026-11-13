/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Entry point for the isolated unprivileged plugin worker thread. */
#ifndef EF800_PLUGIN_WORKER_H
#define EF800_PLUGIN_WORKER_H

/** @brief Run worker mailbox dispatch within its assigned memory regions.
 * @param mailbox Shared request/response mailbox.
 * @param text_region Executable plugin text mapping.
 * @param rw_region Writable plugin data mapping.
 * @return None; the worker runs until stopped or faults.
 */
void ef800_plugin_worker_entry(void *mailbox, void *text_region,
	void *rw_region);

#endif /* EF800_PLUGIN_WORKER_H */
