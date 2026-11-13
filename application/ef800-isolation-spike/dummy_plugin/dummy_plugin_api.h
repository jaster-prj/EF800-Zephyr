/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Define the dummy plugin ABI used by the isolation spike. */
#ifndef EF800_SPIKE_DUMMY_PLUGIN_API_H
#define EF800_SPIKE_DUMMY_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

/** @brief Process one command and write a bounded response.
 * @param instance Plugin instance state.
 * @param command Command bytes.
 * @param command_length Command byte count.
 * @param response Response destination.
 * @param response_capacity Response capacity.
 * @param response_length Receives response byte count.
 * @return 0 on success; negative errno when command processing fails.
 */
typedef int (*spike_dummy_process_fn)(void *instance,
				      const uint8_t *command,
				      size_t command_length,
				      uint8_t *response,
				      size_t response_capacity,
				      size_t *response_length);

struct spike_dummy_plugin_instance {
	spike_dummy_process_fn process;
};

#endif /* EF800_SPIKE_DUMMY_PLUGIN_API_H */
