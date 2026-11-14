/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Drive simulated plugin operations through UI and recovery policy. */
#ifndef VIRTUAL_EF800_SUPERVISOR_H
#define VIRTUAL_EF800_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ef800_ui.h"
#include "virtual_ef800_plugin.h"

struct virtual_ef800_supervisor {
	struct virtual_ef800_plugin plugin;
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
void virtual_ef800_supervisor_start(struct virtual_ef800_supervisor *supervisor,
					    struct ef800_ui_state *ui,
					    enum virtual_ef800_plugin_behavior behavior,
 					    uint32_t deadline_ms);

/** @brief Declare whether supervisor cleanup can safely release resources.
 * @param supervisor Supervisor state.
 * @param safe True when cleanup is safe to perform.
 * @return None.
 */
void virtual_ef800_supervisor_set_cleanup_safe(
	struct virtual_ef800_supervisor *supervisor, bool safe);

/** @brief Advance plugin simulation and apply completion/quarantine effects.
 * @param supervisor Supervisor state.
 * @param elapsed_ms Time advanced since the previous step.
 * @return Plugin operation result for this step.
 */
struct virtual_ef800_plugin_result virtual_ef800_supervisor_step(
	struct virtual_ef800_supervisor *supervisor, uint32_t elapsed_ms);

#endif
