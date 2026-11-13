/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Register built-in/external plugins and track their availability. */
#ifndef EF800_PLUGIN_REGISTRY_H
#define EF800_PLUGIN_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "ef800_measurement.h"
#include "ef800_plugin_contract.h"

#define EF800_PLUGIN_REGISTRY_MAX_ENTRIES 16u

enum ef800_plugin_registry_state {
	EF800_PLUGIN_REGISTRY_READY = 0,
	EF800_PLUGIN_REGISTRY_MISSING,
	EF800_PLUGIN_REGISTRY_INVALID,
	EF800_PLUGIN_REGISTRY_INCOMPATIBLE,
	EF800_PLUGIN_REGISTRY_DUPLICATE,
	EF800_PLUGIN_REGISTRY_UNAVAILABLE,
	EF800_PLUGIN_REGISTRY_FAULTED,
	EF800_PLUGIN_REGISTRY_TIMED_OUT,
};

struct ef800_plugin_registry_entry {
	struct ef800_plugin_descriptor_info info;
	enum ef800_plugin_registry_state state;
};

struct ef800_plugin_registry {
	struct ef800_plugin_registry_entry entries[
		EF800_PLUGIN_REGISTRY_MAX_ENTRIES];
	size_t count;
};

/** @brief Clear registry state before registrations are added.
 * @param registry Registry to initialize.
 * @return None.
 */
void ef800_plugin_registry_init(struct ef800_plugin_registry *registry);

/** @brief Register all platform-provided built-in plugin descriptors.
 * @param registry Registry receiving descriptors.
 * @return 0 on success; negative errno when capacity or descriptors are invalid.
 */
int ef800_plugin_registry_register_builtins(
	const struct ef800_plugin_registry *registry);

/** @brief Register one validated external plugin descriptor.
 * @param registry Registry receiving the descriptor.
 * @param info Descriptor metadata to validate and copy.
 * @return 0 on registration; negative errno for duplicate, invalid, or full registry.
 */
int ef800_plugin_registry_register_external(
	struct ef800_plugin_registry *registry,
	const struct ef800_plugin_descriptor_info *info);

/** @brief Set detected hardware capabilities used to evaluate a plugin.
 * @param registry Registry containing the plugin.
 * @param plugin_id Plugin identifier.
 * @param required_hardware Detected capability mask.
 * @return 0 on update; negative errno if plugin is unknown or state is invalid.
 */
int ef800_plugin_registry_set_hardware(
	struct ef800_plugin_registry *registry, uint32_t plugin_id,
	uint32_t required_hardware);

/** @brief Record a diagnostic state for a registered plugin.
 * @param registry Registry containing the plugin.
 * @param plugin_id Plugin identifier.
 * @param state Diagnostic availability state.
 * @return 0 on update; negative errno if plugin/state is invalid.
 */
int ef800_plugin_registry_record_diagnostic(
	struct ef800_plugin_registry *registry, uint32_t plugin_id,
	enum ef800_plugin_registry_state state);

/** @brief Set a registered plugin's lifecycle/availability state.
 * @param registry Registry containing the plugin.
 * @param plugin_id Plugin identifier.
 * @param state New state.
 * @return 0 on update; negative errno if plugin/state is invalid.
 */
int ef800_plugin_registry_set_state(struct ef800_plugin_registry *registry,
	uint32_t plugin_id, enum ef800_plugin_registry_state state);

/** @brief Find a registered plugin by identifier.
 * @param registry Registry to search.
 * @param plugin_id Plugin identifier.
 * @return Entry pointer, or NULL when not found/invalid registry.
 */
const struct ef800_plugin_registry_entry *ef800_plugin_registry_find(
	const struct ef800_plugin_registry *registry, uint32_t plugin_id);

/** @brief Get the number of registered entries.
 * @param registry Registry to inspect.
 * @return Entry count, or zero for NULL.
 */
size_t ef800_plugin_registry_count(
	const struct ef800_plugin_registry *registry);

/** @brief Get a registered entry by its array index.
 * @param registry Registry to inspect.
 * @param index Zero-based entry index.
 * @return Entry pointer, or NULL when index is out of range.
 */
const struct ef800_plugin_registry_entry *ef800_plugin_registry_at(
	const struct ef800_plugin_registry *registry, size_t index);

/** @brief Get a plugin's current state by identifier.
 * @param registry Registry to search.
 * @param plugin_id Plugin identifier.
 * @return Current state, or the invalid/missing state when absent.
 */
enum ef800_plugin_registry_state ef800_plugin_registry_state_for(
	const struct ef800_plugin_registry *registry, uint32_t plugin_id);

#endif
