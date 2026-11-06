/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_RECOVERY_STATE_H
#define EF800_PLUGIN_RECOVERY_STATE_H

#include <stdbool.h>
#include <stdint.h>

/* Clear the host ledger only for the mapping generation just detached. */
static inline bool ef800_plugin_recovery_domain_detached(
	uint32_t *ledger_generation, bool *worker_mapped,
	uint32_t detached_generation)
{
	if (ledger_generation == NULL || worker_mapped == NULL ||
		detached_generation == 0U || !*worker_mapped ||
		*ledger_generation != detached_generation) {
		return false;
	}
	*worker_mapped = false;
	*ledger_generation = 0U;
	return true;
}

#endif /* EF800_PLUGIN_RECOVERY_STATE_H */
