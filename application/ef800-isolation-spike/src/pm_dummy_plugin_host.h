/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_SPIKE_PM_DUMMY_PLUGIN_HOST_H
#define EF800_SPIKE_PM_DUMMY_PLUGIN_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "plugin_manager/plugin_abi.h"

struct pm_manager_instance_executor;

int spike_pm_dummy_plugin_load(uint8_t *text, size_t text_capacity,
			      uint8_t *rodata, size_t rodata_capacity,
			      uint8_t *data, size_t data_capacity,
			      volatile uint32_t *protected_host_word);
int spike_pm_dummy_plugin_unload(void);
int spike_pm_dummy_plugin_create_instance(
	const struct pm_manager_instance_executor *executor);
int spike_pm_dummy_plugin_destroy_instance(
	const struct pm_manager_instance_executor *executor);
int spike_pm_dummy_plugin_abandon_instance(void);

#endif /* EF800_SPIKE_PM_DUMMY_PLUGIN_HOST_H */
