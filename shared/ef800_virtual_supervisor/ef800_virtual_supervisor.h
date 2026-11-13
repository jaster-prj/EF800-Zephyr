/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Drive simulated plugin operations through UI and recovery policy. */
#ifndef EF800_VIRTUAL_SUPERVISOR_H
#define EF800_VIRTUAL_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ef800_ui.h"
#include "ef800_virtual_plugin.h"

struct ef800_virtual_supervisor {
	struct ef800_virtual_plugin plugin;
	struct ef800_ui_state *ui;
	bool active;
	bool quarantined;
	bool workspace_mutated;
	bool cleanup_safe;
	uint32_t plugin_id;
};

/** @brief Start a virtual plugin operation and bind it to UI state.
 * @param supervisor Supervisor state to initialize.
 * @param ui UI state affected by completion/failure.
 * @param behavior Fault or completion behavior to simulate.
 * @param deadline_ms Operation deadline in milliseconds.
 * @return None.
 */
void ef800_virtual_supervisor_start(struct ef800_virtual_supervisor *supervisor,
					    struct ef800_ui_state *ui,
					    enum ef800_virtual_plugin_behavior behavior,
 					    uint32_t deadline_ms);

/** @brief Declare whether supervisor cleanup can safely release resources.
 * @param supervisor Supervisor state.
 * @param safe True when cleanup is safe to perform.
 * @return None.
 */
void ef800_virtual_supervisor_set_cleanup_safe(
	struct ef800_virtual_supervisor *supervisor, bool safe);

/** @brief Advance plugin simulation and apply completion/quarantine effects.
 * @param supervisor Supervisor state.
 * @param elapsed_ms Time advanced since the previous step.
 * @return Plugin operation result for this step.
 */
struct ef800_virtual_plugin_result ef800_virtual_supervisor_step(
	struct ef800_virtual_supervisor *supervisor, uint32_t elapsed_ms);

#endif
