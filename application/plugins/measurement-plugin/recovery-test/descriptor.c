/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <stdint.h>

#include "ef800_plugin_contract.h"
#include "ef800_plugin_ids.h"
#include "ef800_plugin_worker_abi.h"
#include "plugin_manager/plugin_abi.h"

#ifndef EF800_TEST_PLUGIN_ID
#error EF800_TEST_PLUGIN_ID is required
#endif
#ifndef EF800_TEST_PLUGIN_MODE
#error EF800_TEST_PLUGIN_MODE is required
#endif

#if EF800_TEST_PLUGIN_MODE == 0
#define EF800_TEST_PLUGIN_NAME "EF800 Healthy Probe"
#elif EF800_TEST_PLUGIN_MODE == 9
#define EF800_TEST_PLUGIN_NAME "EF800 Malformed Service Probe"
#elif EF800_TEST_PLUGIN_MODE == 10
#define EF800_TEST_PLUGIN_NAME "EF800 Stale Generation Probe"
#else
#define EF800_TEST_PLUGIN_NAME "EF800 Fault Probe"
#endif
#define EF800_TEST_PLUGIN_VERSION "1.0.0"

#if EF800_TEST_PLUGIN_MODE == 9 || EF800_TEST_PLUGIN_MODE == 10
#define EF800_TEST_PLUGIN_WORKER_SERVICES EF800_PLUGIN_WORKER_SERVICE_LOG
#define EF800_TEST_PLUGIN_REQUIRED_COUNT 1U
#define EF800_TEST_PLUGIN_REQUIRED_SERVICE \
	{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}
#else
#define EF800_TEST_PLUGIN_WORKER_SERVICES 0U
#define EF800_TEST_PLUGIN_REQUIRED_COUNT 0U
#define EF800_TEST_PLUGIN_REQUIRED_SERVICE {0U}
#endif

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
	char name[sizeof(EF800_TEST_PLUGIN_NAME)];
	char plugin_version[sizeof(EF800_TEST_PLUGIN_VERSION)];
	struct ef800_plugin_metadata metadata;
};

__attribute__((section(".rodata.pm_descriptor"), used))
const struct ef800_wire_descriptor pm_plugin_get_descriptor = {
	.magic = 0x53444d50U,
	.version = 2U,
	.size = sizeof(struct ef800_wire_descriptor),
	.application_id = {EF800_APPLICATION_UUID_BYTES},
	.interface_id = {EF800_PLUGIN_CALCULATION_UUID_BYTES},
	.interface_version = 1U,
	.plugin_id = EF800_TEST_PLUGIN_ID,
	.required_count = EF800_TEST_PLUGIN_REQUIRED_COUNT,
	.required_offset = 88U,
	.name_offset = 104U,
	.name_size = sizeof(EF800_TEST_PLUGIN_NAME),
	.version_offset = 104U + sizeof(EF800_TEST_PLUGIN_NAME),
	.version_size = sizeof(EF800_TEST_PLUGIN_VERSION),
	.create_address = (uint32_t)(uintptr_t)pm_plugin_create,
	.destroy_address = (uint32_t)(uintptr_t)pm_plugin_destroy,
	.extension_offset = offsetof(struct ef800_wire_descriptor, metadata),
	.extension_size = sizeof(struct ef800_plugin_metadata),
	.required_service = EF800_TEST_PLUGIN_REQUIRED_SERVICE,
	.name = EF800_TEST_PLUGIN_NAME,
	.plugin_version = EF800_TEST_PLUGIN_VERSION,
	.metadata = {
		{EF800_PLUGIN_METADATA_UUID_BYTES},
		EF800_PLUGIN_METADATA_VERSION,
		sizeof(struct ef800_plugin_metadata),
		0U, 0U, 0U, 0U, 0U,
		0U, 0U, 0U, 0U,
		EF800_PLUGIN_EXECUTION_USER_WORKER,
		EF800_PLUGIN_WORKER_ABI_VERSION,
		EF800_PLUGIN_TARGET_ARM_CORTEX_M,
		EF800_TEST_PLUGIN_WORKER_SERVICES,
		EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE,
		2048U,
		2048U,
		1024U, 1024U, 1024U,
	},
};
