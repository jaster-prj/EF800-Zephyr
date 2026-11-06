/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_worker_sdk.h"

#include <errno.h>

static const pm_uuid_t worker_service_id =
	{{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}};

static int uuid_equal(const pm_uuid_t *left, const pm_uuid_t *right)
{
	for (uint32_t index = 0U; index < PM_UUID_SIZE; ++index) {
		if (left->bytes[index] != right->bytes[index]) {
			return 0;
		}
	}
	return 1;
}

int ef800_plugin_worker_binding_from_services(
	const struct pm_service_table *services,
	struct ef800_plugin_worker_binding *binding)
{
	if (services == NULL || services->items == NULL || binding == NULL) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < services->count; ++index) {
		const struct pm_service *service = &services->items[index];
		const struct ef800_plugin_worker_binding *candidate;

		if (!uuid_equal(&service->service_id, &worker_service_id) ||
			service->version < EF800_PLUGIN_WORKER_SERVICE_VERSION ||
			service->vtable == NULL) {
			continue;
		}
		candidate = service->vtable;
		if (candidate->abi_version != EF800_PLUGIN_WORKER_BINDING_ABI_VERSION ||
			candidate->struct_size < sizeof(*candidate) ||
			candidate->mailbox == NULL || candidate->generation == 0U ||
			candidate->capability == 0U || candidate->reserved != 0U ||
			candidate->deadline_ms == 0U) {
			return -EPROTO;
		}
		*binding = *candidate;
		return 0;
	}
	return -ENOENT;
}

static void bytes_copy(uint8_t *destination, const uint8_t *source,
	uint32_t length)
{
	for (uint32_t index = 0U; index < length; ++index) {
		destination[index] = source[index];
	}
}

static int32_t worker_service_call(
	struct ef800_plugin_worker_binding *binding, uint32_t operation,
	const uint8_t *input, uint32_t input_length, uint8_t *output,
	uint32_t output_capacity, uint32_t timeout_ms, uint32_t *output_length)
{
	struct ef800_plugin_worker_message *message;
	int ret;

	if (binding == NULL || binding->mailbox == NULL || binding->generation == 0U ||
		binding->capability == 0U || binding->deadline_ms == 0U ||
		(input == NULL && input_length != 0U) ||
		(output == NULL && output_capacity != 0U) ||
		input_length > EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE ||
		output_capacity > EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE ||
		(input_length != 0U && output_capacity != 0U)) {
		return -EINVAL;
	}
	if (binding->sequence == UINT32_MAX) {
		return -EOVERFLOW;
	}
	message = &binding->mailbox->message;
	if (input_length != 0U) {
		bytes_copy(binding->mailbox->payload, input, input_length);
	}
	*message = (struct ef800_plugin_worker_message){
		.abi_version = EF800_PLUGIN_WORKER_ABI_VERSION,
		.struct_size = sizeof(*message),
		.generation = binding->generation,
		.sequence = ++binding->sequence,
		.kind = EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST,
		.operation = operation,
		.capability = binding->capability,
		.payload_offset = 0U,
		.payload_length = input_length,
		.response_offset = 0U,
		.response_capacity = output_capacity,
		.timeout_ms = timeout_ms,
		.deadline_ms = binding->deadline_ms,
	};
	ret = ef800_plugin_worker_syscall(binding->mailbox);
	if (ret != 0) {
		return ret;
	}
	if (message->response_length > output_capacity) {
		return -EOVERFLOW;
	}
	if (output_length != NULL) {
		*output_length = message->response_length;
	}
	if (message->status == 0 && message->response_length != 0U) {
		bytes_copy(output, binding->mailbox->payload,
			message->response_length);
	}
	return message->status;
}

int32_t ef800_plugin_worker_port_write(
	struct ef800_plugin_worker_binding *binding,
	const uint8_t *data, uint32_t length)
{
	int32_t status;

	if (length == 0U || length > EF800_PLUGIN_WORKER_MAX_PORT_TRANSFER) {
		return -EINVAL;
	}
	status = worker_service_call(binding, EF800_PLUGIN_WORKER_PORT_WRITE,
		data, length, NULL, 0U, 0U, NULL);
	return status == 0 ? (int32_t)length : status;
}

int32_t ef800_plugin_worker_port_read(
	struct ef800_plugin_worker_binding *binding,
	uint8_t *data, uint32_t capacity, uint32_t *length,
	int32_t timeout_ms)
{
	uint32_t output_length = 0U;
	int32_t status;

	if (data == NULL || length == NULL || capacity == 0U ||
		capacity > EF800_PLUGIN_WORKER_MAX_PORT_TRANSFER || timeout_ms <= 0) {
		return -EINVAL;
	}
	status = worker_service_call(binding, EF800_PLUGIN_WORKER_PORT_READ,
		NULL, 0U, data, capacity, (uint32_t)timeout_ms, &output_length);
	*length = status == 0 ? output_length : 0U;
	return status == 0 ? (int32_t)output_length : status;
}

int32_t ef800_plugin_worker_workspace_snapshot(
	struct ef800_plugin_worker_binding *binding, char *destination,
	uint32_t capacity, uint32_t *length)
{
	uint32_t output_length = 0U;
	int32_t status;

	if (destination == NULL || length == NULL || capacity == 0U ||
		capacity > EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT) {
		return -EINVAL;
	}
	status = worker_service_call(binding,
		EF800_PLUGIN_WORKER_WORKSPACE_SNAPSHOT, NULL, 0U,
		(uint8_t *)destination, capacity, 0U, &output_length);
	*length = status == 0 ? output_length : 0U;
	return status;
}

int32_t ef800_plugin_worker_clock_uptime(
	struct ef800_plugin_worker_binding *binding, uint64_t *uptime_ms)
{
	uint32_t output_length = 0U;
	int32_t status;

	if (uptime_ms == NULL) {
		return -EINVAL;
	}
	status = worker_service_call(binding, EF800_PLUGIN_WORKER_CLOCK_UPTIME,
		NULL, 0U, (uint8_t *)uptime_ms, sizeof(*uptime_ms), 0U,
		&output_length);
	return status == 0 && output_length != sizeof(*uptime_ms) ? -EPROTO : status;
}

int32_t ef800_plugin_worker_log(
	struct ef800_plugin_worker_binding *binding, const char *message,
	uint32_t length)
{
	if (message == NULL || length == 0U ||
		length > EF800_PLUGIN_WORKER_MAX_LOG_MESSAGE) {
		return -EINVAL;
	}
	return worker_service_call(binding, EF800_PLUGIN_WORKER_LOG,
		(const uint8_t *)message, length, NULL, 0U, 0U, NULL);
}
