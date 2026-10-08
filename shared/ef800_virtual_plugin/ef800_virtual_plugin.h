/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_PLUGIN_H
#define EF800_VIRTUAL_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

enum ef800_virtual_plugin_behavior {
	EF800_VIRTUAL_PLUGIN_HEALTHY = 0,
	EF800_VIRTUAL_PLUGIN_DELAYED,
	EF800_VIRTUAL_PLUGIN_TIMEOUT,
	EF800_VIRTUAL_PLUGIN_MALFORMED_SERVICE,
	EF800_VIRTUAL_PLUGIN_PARTIAL_OUTPUT,
	EF800_VIRTUAL_PLUGIN_FAILURE,
};

struct ef800_virtual_plugin_result {
	bool completed;
	bool output_accepted;
	bool quarantined;
	int32_t error;
};

struct ef800_virtual_plugin {
	enum ef800_virtual_plugin_behavior behavior;
	uint32_t deadline_ms;
	uint32_t elapsed_ms;
};

void ef800_virtual_plugin_init(struct ef800_virtual_plugin *plugin,
				       enum ef800_virtual_plugin_behavior behavior,
				       uint32_t deadline_ms);
struct ef800_virtual_plugin_result ef800_virtual_plugin_step(
	struct ef800_virtual_plugin *plugin, uint32_t elapsed_ms);

#endif
