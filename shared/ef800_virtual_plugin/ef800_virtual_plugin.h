/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Simulate plugin completion, delays, failures, and quarantine. */
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

/** @brief Initialize a simulated plugin behavior and deadline.
 * @param plugin Simulation state to initialize.
 * @param behavior Completion/fault behavior to emulate.
 * @param deadline_ms Maximum operation duration.
 * @return None.
 */
void ef800_virtual_plugin_init(struct ef800_virtual_plugin *plugin,
				       enum ef800_virtual_plugin_behavior behavior,
				       uint32_t deadline_ms);

/** @brief Advance a simulated operation and report completion/fault status.
 * @param plugin Simulation state.
 * @param elapsed_ms Time advanced since the previous step.
 * @return Current completion, output acceptance, quarantine, and error state.
 */
struct ef800_virtual_plugin_result ef800_virtual_plugin_step(
	struct ef800_virtual_plugin *plugin, uint32_t elapsed_ms);

#endif
