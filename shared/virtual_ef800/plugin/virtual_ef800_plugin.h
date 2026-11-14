/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Simulate plugin completion, delays, failures, and quarantine. */
#ifndef VIRTUAL_EF800_PLUGIN_H
#define VIRTUAL_EF800_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

enum virtual_ef800_plugin_behavior {
	VIRTUAL_EF800_PLUGIN_HEALTHY = 0,
	VIRTUAL_EF800_PLUGIN_DELAYED,
	VIRTUAL_EF800_PLUGIN_TIMEOUT,
	VIRTUAL_EF800_PLUGIN_MALFORMED_SERVICE,
	VIRTUAL_EF800_PLUGIN_PARTIAL_OUTPUT,
	VIRTUAL_EF800_PLUGIN_FAILURE,
};

struct virtual_ef800_plugin_result {
	bool completed;
	bool output_accepted;
	bool quarantined;
	int32_t error;
};

struct virtual_ef800_plugin {
	enum virtual_ef800_plugin_behavior behavior;
	uint32_t deadline_ms;
	uint32_t elapsed_ms;
};

/** @brief Initialize a simulated plugin behavior and deadline.
 * @param plugin Simulation state to initialize.
 * @param behavior Completion/fault behavior to emulate.
 * @param deadline_ms Maximum operation duration.
 * @return None.
 */
void virtual_ef800_plugin_init(struct virtual_ef800_plugin *plugin,
				       enum virtual_ef800_plugin_behavior behavior,
				       uint32_t deadline_ms);

/** @brief Advance a simulated operation and report completion/fault status.
 * @param plugin Simulation state.
 * @param elapsed_ms Time advanced since the previous step.
 * @return Current completion, output acceptance, quarantine, and error state.
 */
struct virtual_ef800_plugin_result virtual_ef800_plugin_step(
	struct virtual_ef800_plugin *plugin, uint32_t elapsed_ms);

#endif
