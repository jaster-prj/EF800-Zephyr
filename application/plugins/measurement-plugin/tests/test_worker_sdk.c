/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "ef800_plugin_worker_sdk.h"

static struct ef800_plugin_worker_mailbox mailbox;
static uint32_t expected_generation = 3U;
static uint32_t expected_capability = 0x55667788U;
static uint32_t expected_sequence = 0U;

int ef800_plugin_worker_syscall(struct ef800_plugin_worker_mailbox *request_mailbox)
{
	struct ef800_plugin_worker_message *message;
	int ret;

	assert(request_mailbox == &mailbox);
	message = &request_mailbox->message;
	ret = ef800_plugin_worker_message_validate(message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE,
		expected_generation, 100U);
	assert(ret == 0);
	assert(message->kind == EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST);
	assert(message->capability == expected_capability);
	expected_sequence++;
	assert(message->sequence == expected_sequence);
	assert(message->deadline_ms == 500U);

	switch (message->operation) {
	case EF800_PLUGIN_WORKER_PORT_WRITE:
		assert(message->payload_length == 3U);
		assert(memcmp(request_mailbox->payload, "abc", 3U) == 0);
		message->status = 0;
		break;
	case EF800_PLUGIN_WORKER_PORT_READ:
		request_mailbox->payload[0] = 0xa5U;
		message->response_length = 1U;
		message->status = 0;
		break;
	case EF800_PLUGIN_WORKER_WORKSPACE_SNAPSHOT:
		memcpy(request_mailbox->payload, "data", 4U);
		message->response_length = 4U;
		message->status = 0;
		break;
	case EF800_PLUGIN_WORKER_CLOCK_UPTIME:
		{
			const uint64_t uptime = 123456U;
			memcpy(request_mailbox->payload, &uptime, sizeof(uptime));
			message->response_length = sizeof(uptime);
			message->status = 0;
		}
		break;
	case EF800_PLUGIN_WORKER_LOG:
		assert(message->payload_length == 4U);
		assert(memcmp(request_mailbox->payload, "safe", 4U) == 0);
		message->status = 0;
		break;
	default:
		assert(false);
	}
	return 0;
}

int main(void)
{
	struct ef800_plugin_worker_binding offered = {
		.abi_version = EF800_PLUGIN_WORKER_BINDING_ABI_VERSION,
		.struct_size = sizeof(offered),
		.mailbox = &mailbox,
		.generation = expected_generation,
		.capability = expected_capability,
		.deadline_ms = 500U,
	};
	struct pm_service service = {
		.service_id = {{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}},
		.version = EF800_PLUGIN_WORKER_SERVICE_VERSION,
		.vtable = &offered,
	};
	struct pm_service_table services = {.count = 1U, .items = &service};
	struct ef800_plugin_worker_binding binding;
	uint8_t read_data[1] = {0U};
	uint32_t read_length = 0U;
	char snapshot[8] = {0};
	uint32_t snapshot_length = 0U;
	uint64_t uptime = 0U;

	assert(ef800_plugin_worker_binding_from_services(&services, &binding) == 0);
	assert(binding.mailbox == &mailbox);
	assert(ef800_plugin_worker_port_write(&binding,
		(const uint8_t *)"abc", 3U) == 3);
	assert(ef800_plugin_worker_port_read(&binding, read_data,
		sizeof(read_data), &read_length, 100) == 1);
	assert(read_length == 1U && read_data[0] == 0xa5U);
	assert(ef800_plugin_worker_workspace_snapshot(&binding, snapshot,
		sizeof(snapshot), &snapshot_length) == 0);
	assert(snapshot_length == 4U && memcmp(snapshot, "data", 4U) == 0);
	assert(ef800_plugin_worker_clock_uptime(&binding, &uptime) == 0);
	assert(uptime == 123456U);
	assert(ef800_plugin_worker_log(&binding, "safe", 4U) == 0);
	assert(ef800_plugin_worker_port_write(&binding,
		(const uint8_t *)"x", EF800_PLUGIN_WORKER_MAX_PORT_TRANSFER + 1U) == -EINVAL);

	services.count = 0U;
	assert(ef800_plugin_worker_binding_from_services(&services, &binding) ==
		-ENOENT);
	return 0;
}
