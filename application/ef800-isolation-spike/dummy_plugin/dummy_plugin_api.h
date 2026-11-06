/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_SPIKE_DUMMY_PLUGIN_API_H
#define EF800_SPIKE_DUMMY_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

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
