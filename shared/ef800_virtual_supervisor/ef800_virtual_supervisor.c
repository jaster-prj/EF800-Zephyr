/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_virtual_supervisor.h"

void ef800_virtual_supervisor_start(struct ef800_virtual_supervisor *supervisor,
					    struct ef800_ui_state *ui,
					    enum ef800_virtual_plugin_behavior behavior,
					    uint32_t deadline_ms)
{
	if (supervisor == NULL) {
		return;
	}
	ef800_virtual_plugin_init(&supervisor->plugin, behavior, deadline_ms);
	supervisor->ui = ui;
	supervisor->active = true;
	supervisor->quarantined = false;
	supervisor->workspace_mutated = false;
	supervisor->cleanup_safe = true;
	if (ui != NULL) {
		ui->plugin_session_active = true;
		ui->diagnostic = EF800_UI_DIAGNOSTIC_NONE;
	}
}

struct ef800_virtual_plugin_result ef800_virtual_supervisor_step(
	struct ef800_virtual_supervisor *supervisor, uint32_t elapsed_ms)
{
	struct ef800_virtual_plugin_result result;

	if (supervisor == NULL || !supervisor->active) {
		return (struct ef800_virtual_plugin_result){.error = -1};
	}
	result = ef800_virtual_plugin_step(&supervisor->plugin, elapsed_ms);
	if (result.quarantined) {
		supervisor->active = false;
		supervisor->quarantined = true;
		if (!supervisor->cleanup_safe) {
			result.error = -4;
		}
		if (supervisor->ui != NULL) {
			ef800_ui_show_diagnostic(supervisor->ui,
				result.error == -2 ? EF800_UI_DIAGNOSTIC_TIMEOUT :
				EF800_UI_DIAGNOSTIC_FAILURE, NULL);
		}
	} else if (result.completed) {
		supervisor->active = false;
		supervisor->workspace_mutated = result.output_accepted;
		if (supervisor->ui != NULL) {
			supervisor->ui->plugin_session_active = false;
		}
	}
	return result;
}

void ef800_virtual_supervisor_set_cleanup_safe(
	struct ef800_virtual_supervisor *supervisor, bool safe)
{
	if (supervisor != NULL) {
		supervisor->cleanup_safe = safe;
	}
}
