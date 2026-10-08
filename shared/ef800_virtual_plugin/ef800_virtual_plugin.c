/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_virtual_plugin.h"

#include <stddef.h>

void ef800_virtual_plugin_init(struct ef800_virtual_plugin *plugin,
				       enum ef800_virtual_plugin_behavior behavior,
				       uint32_t deadline_ms)
{
	if (plugin == NULL) {
		return;
	}
	plugin->behavior = behavior;
	plugin->deadline_ms = deadline_ms;
	plugin->elapsed_ms = 0U;
}

struct ef800_virtual_plugin_result ef800_virtual_plugin_step(
	struct ef800_virtual_plugin *plugin, uint32_t elapsed_ms)
{
	struct ef800_virtual_plugin_result result = {0};

	if (plugin == NULL) {
		result.error = -1;
		return result;
	}
	plugin->elapsed_ms += elapsed_ms;
	if (plugin->behavior == EF800_VIRTUAL_PLUGIN_TIMEOUT ||
		plugin->elapsed_ms > plugin->deadline_ms) {
		result.quarantined = true;
		result.error = -2;
		return result;
	}
	if (plugin->behavior == EF800_VIRTUAL_PLUGIN_MALFORMED_SERVICE ||
		plugin->behavior == EF800_VIRTUAL_PLUGIN_FAILURE) {
		result.quarantined = true;
		result.error = -3;
		return result;
	}
	if (plugin->behavior == EF800_VIRTUAL_PLUGIN_PARTIAL_OUTPUT) {
		result.completed = true;
		result.output_accepted = false;
		return result;
	}
	if (plugin->behavior == EF800_VIRTUAL_PLUGIN_DELAYED &&
		plugin->elapsed_ms < plugin->deadline_ms) {
		return result;
	}
	result.completed = true;
	result.output_accepted = true;
	return result;
}
