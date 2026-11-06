/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_registry.h"

#include <errno.h>
#include <string.h>


static int register_entry(struct ef800_plugin_registry *registry,
	const struct ef800_plugin_descriptor_info *info)
{
	struct ef800_plugin_registry_entry *entry;

	if (registry == NULL || info == NULL || info->plugin_id == 0u ||
		info->name[0] == '\0') {
		return -EINVAL;
	}
	if (ef800_plugin_registry_find(registry, info->plugin_id) != NULL) {
		return -EEXIST;
	}
	if (registry->count >= EF800_PLUGIN_REGISTRY_MAX_ENTRIES) {
		return -ENOSPC;
	}
	entry = &registry->entries[registry->count++];
	entry->info = *info;
	entry->state = EF800_PLUGIN_REGISTRY_READY;
	return 0;
}

void ef800_plugin_registry_init(struct ef800_plugin_registry *registry)
{
	if (registry != NULL) {
		memset(registry, 0, sizeof(*registry));
	}
}

int ef800_plugin_registry_register_builtins(
	struct ef800_plugin_registry *registry)
{
	if (registry == NULL) {
		return -EINVAL;
	}
	return 0;
}

int ef800_plugin_registry_register_external(
	struct ef800_plugin_registry *registry,
	const struct ef800_plugin_descriptor_info *info)
{
	if (info == NULL || info->kind == 0 || info->menu_category == 0) {
		return -EINVAL;
	}
	return register_entry(registry, info);
}

int ef800_plugin_registry_set_hardware(
	struct ef800_plugin_registry *registry, uint32_t plugin_id,
	uint32_t required_hardware)
{
	struct ef800_plugin_registry_entry *entry;

	if (registry == NULL || required_hardware == 0U) {
		return -EINVAL;
	}
	entry = (struct ef800_plugin_registry_entry *)
		ef800_plugin_registry_find(registry, plugin_id);
	if (entry == NULL) {
		return -ENOENT;
	}
	entry->info.required_hardware = required_hardware;
	return 0;
}

int ef800_plugin_registry_record_diagnostic(
	struct ef800_plugin_registry *registry, uint32_t plugin_id,
	enum ef800_plugin_registry_state state)
{
	struct ef800_plugin_descriptor_info info = {
		.struct_size = sizeof(info),
		.plugin_id = plugin_id,
		.kind = EF800_PLUGIN_KIND_MEASUREMENT,
		.menu_category = EF800_PLUGIN_MENU_MEASUREMENT,
	};

	if (registry == NULL || plugin_id == 0u ||
		state <= EF800_PLUGIN_REGISTRY_READY ||
		state > EF800_PLUGIN_REGISTRY_TIMED_OUT) {
		return -EINVAL;
	}
	if (ef800_plugin_registry_find(registry, plugin_id) != NULL) {
		return -EEXIST;
	}
	if (registry->count >= EF800_PLUGIN_REGISTRY_MAX_ENTRIES) {
		return -ENOSPC;
	}
	registry->entries[registry->count].info = info;
	registry->entries[registry->count].state = state;
	++registry->count;
	return 0;
}

int ef800_plugin_registry_set_state(struct ef800_plugin_registry *registry,
	uint32_t plugin_id, enum ef800_plugin_registry_state state)
{
	struct ef800_plugin_registry_entry *entry;

	if (registry == NULL || plugin_id == 0u ||
		state <= EF800_PLUGIN_REGISTRY_READY ||
		state > EF800_PLUGIN_REGISTRY_TIMED_OUT) {
		return -EINVAL;
	}
	entry = (struct ef800_plugin_registry_entry *)
		ef800_plugin_registry_find(registry, plugin_id);
	if (entry == NULL) {
		return -ENOENT;
	}
	entry->state = state;
	return 0;
}

const struct ef800_plugin_registry_entry *ef800_plugin_registry_find(
	const struct ef800_plugin_registry *registry, uint32_t plugin_id)
{
	if (registry == NULL) {
		return NULL;
	}
	for (size_t index = 0u; index < registry->count; ++index) {
		if (registry->entries[index].info.plugin_id == plugin_id) {
			return &registry->entries[index];
		}
	}
	return NULL;
}

size_t ef800_plugin_registry_count(
	const struct ef800_plugin_registry *registry)
{
	return registry == NULL ? 0u : registry->count;
}

const struct ef800_plugin_registry_entry *ef800_plugin_registry_at(
	const struct ef800_plugin_registry *registry, size_t index)
{
	return registry != NULL && index < registry->count ?
		&registry->entries[index] : NULL;
}

enum ef800_plugin_registry_state ef800_plugin_registry_state_for(
	const struct ef800_plugin_registry *registry, uint32_t plugin_id)
{
	const struct ef800_plugin_registry_entry *entry =
		ef800_plugin_registry_find(registry, plugin_id);

	return entry == NULL ? EF800_PLUGIN_REGISTRY_MISSING : entry->state;
}
