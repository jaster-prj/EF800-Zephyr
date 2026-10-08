/* SPDX-License-Identifier: Apache-2.0 */
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

void ef800_virtual_supervisor_start(struct ef800_virtual_supervisor *supervisor,
					    struct ef800_ui_state *ui,
					    enum ef800_virtual_plugin_behavior behavior,
 					    uint32_t deadline_ms);
void ef800_virtual_supervisor_set_cleanup_safe(
	struct ef800_virtual_supervisor *supervisor, bool safe);
struct ef800_virtual_plugin_result ef800_virtual_supervisor_step(
	struct ef800_virtual_supervisor *supervisor, uint32_t elapsed_ms);

#endif
