/* SPDX-License-Identifier: Apache-2.0 */
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

void ef800_plugin_registry_init(struct ef800_plugin_registry *registry);
int ef800_plugin_registry_register_builtins(
	struct ef800_plugin_registry *registry);
int ef800_plugin_registry_register_external(
	struct ef800_plugin_registry *registry,
	const struct ef800_plugin_descriptor_info *info);
int ef800_plugin_registry_set_hardware(
	struct ef800_plugin_registry *registry, uint32_t plugin_id,
	uint32_t required_hardware);
int ef800_plugin_registry_record_diagnostic(
	struct ef800_plugin_registry *registry, uint32_t plugin_id,
	enum ef800_plugin_registry_state state);
int ef800_plugin_registry_set_state(struct ef800_plugin_registry *registry,
	uint32_t plugin_id, enum ef800_plugin_registry_state state);
const struct ef800_plugin_registry_entry *ef800_plugin_registry_find(
	const struct ef800_plugin_registry *registry, uint32_t plugin_id);
size_t ef800_plugin_registry_count(
	const struct ef800_plugin_registry *registry);
const struct ef800_plugin_registry_entry *ef800_plugin_registry_at(
	const struct ef800_plugin_registry *registry, size_t index);
enum ef800_plugin_registry_state ef800_plugin_registry_state_for(
	const struct ef800_plugin_registry *registry, uint32_t plugin_id);

#endif
