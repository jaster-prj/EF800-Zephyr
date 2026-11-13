/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Publish dummy calculation plugin metadata and ABI entry points. */
#include <stddef.h>
#include <stdint.h>

#include "ef800_plugin_contract.h"
#include "ef800_plugin_metadata.h"
#include "plugin_manager/plugin_abi.h"

#define DUMMY_PLUGIN_ID 4u
#define DUMMY_PLUGIN_NAME "Calculate position from source"
#define DUMMY_PLUGIN_VERSION "1.0.0"

int pm_plugin_create(const struct pm_service_table *services, void **instance);
int pm_plugin_destroy(void *instance);

struct ef800_wire_descriptor {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint8_t application_id[16];
	uint8_t interface_id[16];
	uint32_t interface_version;
	uint32_t plugin_id;
	uint32_t required_count;
	uint32_t required_offset;
	uint32_t name_offset;
	uint32_t name_size;
	uint32_t version_offset;
	uint32_t version_size;
	uint32_t create_address;
	uint32_t destroy_address;
	uint32_t extension_offset;
	uint32_t extension_size;
	uint8_t required_service[16];
	char name[sizeof(DUMMY_PLUGIN_NAME)];
	char plugin_version[sizeof(DUMMY_PLUGIN_VERSION)];
	struct ef800_plugin_metadata metadata;
};

__attribute__((section(".rodata.pm_descriptor"), used))
const struct ef800_wire_descriptor pm_plugin_get_descriptor = {
	.magic = 0x53444d50U,
	.version = 2U,
	.size = sizeof(struct ef800_wire_descriptor),
	.application_id = {EF800_PLUGIN_APPLICATION_UUID_BYTES},
	.interface_id = {EF800_PLUGIN_CALCULATION_UUID_BYTES},
	.interface_version = EF800_CALCULATION_API_VERSION,
	.plugin_id = DUMMY_PLUGIN_ID,
	.required_count = 0U,
	.required_offset = 88U,
	.name_offset = 104U,
	.name_size = sizeof(DUMMY_PLUGIN_NAME),
	.version_offset = 104U + sizeof(DUMMY_PLUGIN_NAME),
	.version_size = sizeof(DUMMY_PLUGIN_VERSION),
	.create_address = (uint32_t)(uintptr_t)pm_plugin_create,
	.destroy_address = (uint32_t)(uintptr_t)pm_plugin_destroy,
	.extension_offset = offsetof(struct ef800_wire_descriptor, metadata),
	.extension_size = sizeof(struct ef800_plugin_metadata),
	.name = DUMMY_PLUGIN_NAME,
	.plugin_version = DUMMY_PLUGIN_VERSION,
	.metadata = {
		{EF800_PLUGIN_METADATA_UUID_BYTES},
		EF800_PLUGIN_METADATA_VERSION,
		sizeof(struct ef800_plugin_metadata),
		0U, 0U, 0U, 0U, 0U,
		0U, 0U, 0U, 0U,
		EF800_PLUGIN_EXECUTION_USER_WORKER,
		EF800_PLUGIN_WORKER_ABI_VERSION,
		EF800_PLUGIN_TARGET_ARM_CORTEX_M,
		0U,
		EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE,
		2048U,
		2048U,
		1024U, 1024U, 1024U,
		16384U, 16384U, 1024U, 1024U, 4096U,
	},
};
