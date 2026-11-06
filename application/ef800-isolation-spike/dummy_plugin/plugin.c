/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "plugin_manager/plugin_abi.h"
#include "plugin_manager/plugin_descriptor.h"
#include "dummy_plugin_api.h"

#define DUMMY_NAME_OFFSET 88U
#define DUMMY_NAME "usb-dummy"
#define DUMMY_VERSION "1.0"
#define DUMMY_VERSION_OFFSET (DUMMY_NAME_OFFSET + sizeof(DUMMY_NAME))

extern volatile uint32_t ef800_spike_test_protected_word;

static int dummy_process(void *instance, const uint8_t *command,
			size_t command_length, uint8_t *response,
			size_t response_capacity, size_t *response_length)
{
	static const uint8_t crash_command[] = { 'C', 'R', 'A', 'S', 'H' };
	static const uint8_t answer_prefix[] = {
		'A', 'N', 'S', 'W', 'E', 'R', ' '
	};

	(void)instance;
	if (command == NULL || response == NULL || response_length == NULL) {
		return PM_EINVAL;
	}
	if (command_length == sizeof(crash_command)) {
		bool crash = true;

		for (size_t i = 0U; i < sizeof(crash_command); ++i) {
			if (command[i] != crash_command[i]) {
				crash = false;
				break;
			}
		}
		if (crash) {
			/* Deliberate negative test: this imported host address resolves,
			 * but MPU policy must still deny the unprivileged store. */
			ef800_spike_test_protected_word = 0xdeadbeefU;
		}
	}
	if (response_capacity < sizeof(answer_prefix) + command_length) {
		return PM_EOVERFLOW;
	}
	for (size_t i = 0U; i < sizeof(answer_prefix); ++i) {
		response[i] = answer_prefix[i];
	}
	for (size_t i = 0U; i < command_length; ++i) {
		response[sizeof(answer_prefix) + i] = command[i];
	}
	*response_length = sizeof(answer_prefix) + command_length;
	return PM_OK;
}

static struct spike_dummy_plugin_instance dummy_instance = {
	.process = dummy_process,
};

int pm_plugin_create(const struct pm_service_table *services, void **instance)
{
	if (instance == NULL || (services != NULL && services->count != 0U)) {
		return PM_EINVAL;
	}
	dummy_instance.process = dummy_process;
	*instance = &dummy_instance;
	return PM_OK;
}

int pm_plugin_destroy(void *instance)
{
	return instance == &dummy_instance ? PM_OK : PM_EINVAL;
}

struct dummy_wire_descriptor {
	uint32_t magic;
	uint16_t wire_version;
	uint16_t descriptor_size;
	uint8_t application_id[PM_UUID_SIZE];
	uint8_t interface_id[PM_UUID_SIZE];
	uint32_t interface_version;
	uint32_t plugin_id;
	uint32_t required_service_count;
	uint32_t required_service_offset;
	uint32_t name_offset;
	uint32_t name_size;
	uint32_t version_offset;
	uint32_t version_size;
	pm_plugin_create_fn create_address;
	pm_plugin_destroy_fn destroy_address;
	uint32_t extension_offset;
	uint32_t extension_size;
	char name[sizeof(DUMMY_NAME)];
	char version[sizeof(DUMMY_VERSION)];
} __attribute__((packed));

_Static_assert(offsetof(struct dummy_wire_descriptor, name) == DUMMY_NAME_OFFSET,
	       "wire descriptor header layout mismatch");
_Static_assert(sizeof(struct dummy_wire_descriptor) ==
	       DUMMY_VERSION_OFFSET + sizeof(DUMMY_VERSION),
	       "dummy descriptor image layout mismatch");

__attribute__((section(".rodata.pm_descriptor"), used))
const struct dummy_wire_descriptor pm_plugin_get_descriptor = {
	.magic = PM_WIRE_DESCRIPTOR_MAGIC,
	.wire_version = PM_WIRE_DESCRIPTOR_VERSION,
	.descriptor_size = PM_WIRE_DESCRIPTOR_HEADER_SIZE,
	.application_id = { 1U },
	.interface_id = { 2U },
	.interface_version = 1U,
	.plugin_id = 42U,
	.required_service_count = 0U,
	.required_service_offset = 0U,
	.name_offset = DUMMY_NAME_OFFSET,
	.name_size = sizeof(DUMMY_NAME),
	.version_offset = DUMMY_VERSION_OFFSET,
	.version_size = sizeof(DUMMY_VERSION),
	.create_address = pm_plugin_create,
	.destroy_address = pm_plugin_destroy,
	.extension_offset = 0U,
	.extension_size = 0U,
	.name = DUMMY_NAME,
	.version = DUMMY_VERSION,
};
