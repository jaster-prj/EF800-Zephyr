/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Load and exercise the isolation spike's dummy plugin. */
#ifndef TEST_ISOLATION_SPIKE_PM_DUMMY_PLUGIN_HOST_H
#define TEST_ISOLATION_SPIKE_PM_DUMMY_PLUGIN_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "plugin_manager/plugin_abi.h"

struct pm_manager_instance_executor;

/** @brief Load dummy-plugin image sections into caller-provided memory.
 * @param text Executable text destination.
 * @param text_capacity Text capacity.
 * @param rodata Read-only data destination.
 * @param rodata_capacity Read-only capacity.
 * @param data Writable data destination.
 * @param data_capacity Writable capacity.
 * @param protected_host_word Host word used by isolation checks.
 * @return 0 on load; negative errno for invalid capacities or load failure.
 */
int spike_pm_dummy_plugin_load(uint8_t *text, size_t text_capacity,
			      uint8_t *rodata, size_t rodata_capacity,
			      uint8_t *data, size_t data_capacity,
			      volatile uint32_t *protected_host_word);

/** @brief Unload the dummy plugin after all instances have been released.
 * @return 0 on unload; negative errno when plugin remains active.
 */
int spike_pm_dummy_plugin_unload(void);

/** @brief Create a plugin instance using the supplied worker executor.
 * @param executor Instance executor/interface.
 * @return 0 on create; negative errno on manager/plugin failure.
 */
int spike_pm_dummy_plugin_create_instance(
	const struct pm_manager_instance_executor *executor);

/** @brief Destroy a live dummy-plugin instance.
 * @param executor Executor owning the instance.
 * @return 0 on destroy; negative errno on invalid state/callback failure.
 */
int spike_pm_dummy_plugin_destroy_instance(
	const struct pm_manager_instance_executor *executor);

/** @brief Abandon an instance after unsafe cleanup/fault handling.
 * @return 0 when abandoned; negative errno if no instance can be abandoned.
 */
int spike_pm_dummy_plugin_abandon_instance(void);

#endif /* TEST_ISOLATION_SPIKE_PM_DUMMY_PLUGIN_HOST_H */
