/* SPDX-License-Identifier: Apache-2.0 */
#include "virtual_ef800_plugin.h"

#include <stddef.h>

void virtual_ef800_plugin_init(struct virtual_ef800_plugin *plugin,
				       enum virtual_ef800_plugin_behavior behavior,
				       uint32_t deadline_ms)
{
	if (plugin == NULL) {
		return;
	}
	plugin->behavior = behavior;
	plugin->deadline_ms = deadline_ms;
	plugin->elapsed_ms = 0U;
}

struct virtual_ef800_plugin_result virtual_ef800_plugin_step(
	struct virtual_ef800_plugin *plugin, uint32_t elapsed_ms)
{
	struct virtual_ef800_plugin_result result = {0};

	if (plugin == NULL) {
		result.error = -1;
		return result;
	}
	plugin->elapsed_ms += elapsed_ms;
	if (plugin->behavior == VIRTUAL_EF800_PLUGIN_TIMEOUT ||
		plugin->elapsed_ms > plugin->deadline_ms) {
		result.quarantined = true;
		result.error = -2;
		return result;
	}
	if (plugin->behavior == VIRTUAL_EF800_PLUGIN_MALFORMED_SERVICE ||
		plugin->behavior == VIRTUAL_EF800_PLUGIN_FAILURE) {
		result.quarantined = true;
		result.error = -3;
		return result;
	}
	if (plugin->behavior == VIRTUAL_EF800_PLUGIN_PARTIAL_OUTPUT) {
		result.completed = true;
		result.output_accepted = false;
		return result;
	}
	if (plugin->behavior == VIRTUAL_EF800_PLUGIN_DELAYED &&
		plugin->elapsed_ms < plugin->deadline_ms) {
		return result;
	}
	result.completed = true;
	result.output_accepted = true;
	return result;
}
