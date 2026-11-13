/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Clear recovery-ledger state after a matching domain detach. */
#ifndef EF800_PLUGIN_RECOVERY_STATE_H
#define EF800_PLUGIN_RECOVERY_STATE_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Clear the host ledger only for the generation just detached.
 * @param ledger_generation Generation recorded by the privileged ledger.
 * @param worker_mapped Receives false when the matching mapping is cleared.
 * @param detached_generation Generation reported by the domain detach.
 * @return True when matching ledger state was cleared.
 */
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
