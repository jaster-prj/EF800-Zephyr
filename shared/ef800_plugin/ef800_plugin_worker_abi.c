/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_worker_abi.h"

#include <errno.h>

static int range_valid(uint32_t offset, uint32_t length, size_t capacity)
{
	return (size_t)offset <= capacity &&
		(size_t)length <= capacity - (size_t)offset;
}

static int ranges_overlap(uint32_t first_offset, uint32_t first_length,
	uint32_t second_offset, uint32_t second_length)
{
	if (first_length == 0U || second_length == 0U) {
		return 0;
	}
	return first_offset < second_offset + second_length &&
		second_offset < first_offset + first_length;
}

static int deadline_valid(const struct ef800_plugin_worker_message *message,
	uint64_t now_ms)
{
	uint64_t remaining;

	if (message->deadline_ms <= now_ms) {
		return 0;
	}
	remaining = message->deadline_ms - now_ms;
	return remaining <= EF800_PLUGIN_WORKER_MAX_DEADLINE_MS;
}

static int invocation_valid(
	const struct ef800_plugin_worker_message *message, size_t capacity)
{
	if (message->operation < EF800_PLUGIN_WORKER_CREATE ||
		message->operation > EF800_PLUGIN_WORKER_STOP ||
		message->capability == 0U || message->timeout_ms != 0U ||
		message->payload_length > capacity ||
		message->response_capacity > capacity ||
		!range_valid(message->payload_offset, message->payload_length, capacity) ||
		!range_valid(message->response_offset, message->response_capacity, capacity) ||
		ranges_overlap(message->payload_offset, message->payload_length,
			message->response_offset, message->response_capacity)) {
		return -EINVAL;
	}
	return 0;
}

static int service_request_valid(
	const struct ef800_plugin_worker_message *message, size_t capacity,
	uint64_t now_ms)
{
	uint32_t max_timeout = 0U;

	if (message->operation < EF800_PLUGIN_WORKER_PORT_WRITE ||
		message->operation > EF800_PLUGIN_WORKER_LOG ||
		message->capability == 0U) {
		return -EINVAL;
	}

	switch (message->operation) {
	case EF800_PLUGIN_WORKER_PORT_WRITE:
		if (message->payload_length == 0U ||
			message->payload_length > EF800_PLUGIN_WORKER_MAX_PORT_TRANSFER ||
			message->response_capacity != 0U || message->timeout_ms != 0U) {
			return -EINVAL;
		}
		break;
	case EF800_PLUGIN_WORKER_PORT_READ:
		if (message->payload_length != 0U ||
			message->response_capacity == 0U ||
			message->response_capacity > EF800_PLUGIN_WORKER_MAX_PORT_TRANSFER ||
			message->timeout_ms == 0U) {
			return -EINVAL;
		}
		max_timeout = message->timeout_ms;
		break;
	case EF800_PLUGIN_WORKER_WORKSPACE_SNAPSHOT:
		if (message->payload_length != 0U ||
			message->response_capacity == 0U ||
			message->response_capacity > EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT ||
			message->timeout_ms != 0U) {
			return -EINVAL;
		}
		break;
	case EF800_PLUGIN_WORKER_CLOCK_UPTIME:
		if (message->payload_length != 0U || message->response_capacity !=
			sizeof(uint64_t) || message->timeout_ms != 0U) {
			return -EINVAL;
		}
		break;
	case EF800_PLUGIN_WORKER_LOG:
		if (message->payload_length == 0U ||
			message->payload_length > EF800_PLUGIN_WORKER_MAX_LOG_MESSAGE ||
			message->response_capacity != 0U || message->timeout_ms != 0U) {
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	if (!range_valid(message->payload_offset, message->payload_length, capacity) ||
		!range_valid(message->response_offset, message->response_capacity, capacity) ||
		ranges_overlap(message->payload_offset, message->payload_length,
			message->response_offset, message->response_capacity)) {
		return -EFAULT;
	}
	if (!deadline_valid(message, now_ms)) {
		return -ETIMEDOUT;
	}
	if (max_timeout != 0U &&
		(uint64_t)max_timeout > message->deadline_ms - now_ms) {
		return -ETIMEDOUT;
	}
	return 0;
}

int ef800_plugin_worker_message_validate(
	const struct ef800_plugin_worker_message *message,
	size_t payload_capacity, uint32_t active_generation, uint64_t now_ms)
{
	if (message == NULL || payload_capacity > EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE ||
		message->abi_version != EF800_PLUGIN_WORKER_ABI_VERSION ||
		message->struct_size != sizeof(*message) || message->reserved != 0U ||
		message->deadline_padding != 0U ||
		message->response_length != 0U || message->status != 0 ||
		message->generation == 0U || message->generation != active_generation ||
		message->sequence == 0U) {
		return -EINVAL;
	}
	if (!deadline_valid(message, now_ms)) {
		return -ETIMEDOUT;
	}
	switch (message->kind) {
	case EF800_PLUGIN_WORKER_HOST_INVOKE:
		return invocation_valid(message, payload_capacity);
	case EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST:
		return service_request_valid(message, payload_capacity, now_ms);
	default:
		/* Results are consumed by the supervisor, never accepted as requests. */
		return -EINVAL;
	}
}
