/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_worker.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>

#include "ef800_plugin_contract.h"
#include "ef800_measurement_plugin.h"
#include "ef800_plugin_worker_abi.h"
#include "ef800_plugin_worker_sdk.h"
#include "plugin_manager/plugin_abi.h"

#define EF800_WORKER_TEXT_SIZE (32u * 1024u)
#define EF800_WORKER_RODATA_SIZE (32u * 1024u)
#define EF800_WORKER_RW_SIZE 4096u
#define EF800_WORKER_OUTPUT_OFFSET EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT

static bool memory_range_contains(uintptr_t base, size_t region_size,
	uintptr_t pointer, size_t length)
{
	if (region_size > UINTPTR_MAX - base || pointer < base ||
		length > UINTPTR_MAX - pointer) {
		return false;
	}
	return pointer + length <= base + region_size;
}

static bool executable_address_valid(uintptr_t base, size_t region_size,
	uintptr_t entry)
{
	return memory_range_contains(base, region_size, entry & ~(uintptr_t)1U, 2U);
}

static int copy_request_data(const struct ef800_plugin_worker_mailbox *mailbox,
	const struct ef800_plugin_worker_message *request, void *destination,
	size_t capacity)
{
	if (request->payload_length > capacity) {
		return -EOVERFLOW;
	}
	memcpy(destination, &mailbox->payload[request->payload_offset],
		request->payload_length);
	return 0;
}

static int copy_response_data(struct ef800_plugin_worker_mailbox *mailbox,
	const struct ef800_plugin_worker_message *request, const void *source,
	size_t length)
{
	if (length > request->response_capacity ||
		length > EF800_PLUGIN_WORKER_MAX_RESULT) {
		return -EOVERFLOW;
	}
	memcpy(&mailbox->payload[request->response_offset], source, length);
	return 0;
}

static int worker_create(struct ef800_plugin_worker_mailbox *mailbox,
	const struct ef800_plugin_worker_message *request, uintptr_t text_base,
	size_t text_size, uintptr_t rw_base, size_t rw_size, void **instance)
{
	uint32_t entry_offset;
	struct ef800_plugin_worker_binding binding = {
		.abi_version = EF800_PLUGIN_WORKER_BINDING_ABI_VERSION,
		.struct_size = sizeof(binding),
		.mailbox = mailbox,
		.generation = request->generation,
		.capability = request->capability,
		.deadline_ms = request->deadline_ms,
	};
	const pm_uuid_t worker_service_id = {{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}};
	struct pm_service service = {
		.service_id = worker_service_id,
		.version = EF800_PLUGIN_WORKER_SERVICE_VERSION,
		.vtable = &binding,
	};
	struct pm_service_table services = {
		.count = mailbox->allowed_services == 0U ? 0U : 1U,
		.items = mailbox->allowed_services == 0U ? NULL : &service,
	};
	pm_plugin_create_fn create;
	int ret;

	if (*instance != NULL || request->payload_length != sizeof(entry_offset)) {
		return PM_EBUSY;
	}
	ret = copy_request_data(mailbox, request, &entry_offset,
		sizeof(entry_offset));
	if (ret != 0 || entry_offset >= text_size) {
		return PM_EINVAL;
	}
	create = (pm_plugin_create_fn)(text_base + entry_offset + 1U);
	if (!executable_address_valid(text_base, text_size, (uintptr_t)create)) {
		return PM_EINVAL;
	}
	ret = create(&services, instance);
	if (ret != PM_OK) {
		return ret;
	}
	if (*instance == NULL || !memory_range_contains(rw_base, rw_size,
		(uintptr_t)*instance, 1U)) {
		*instance = NULL;
		return PM_EPROTO;
	}
	mailbox->plugin_instance_offset =
		(uint32_t)((uintptr_t)*instance - rw_base);
	mailbox->plugin_instance_valid = 1U;
	return PM_OK;
}

static int worker_destroy(struct ef800_plugin_worker_mailbox *mailbox,
	const struct ef800_plugin_worker_message *request, uintptr_t text_base,
	size_t text_size, void **instance)
{
	uint32_t entry_offset;
	pm_plugin_destroy_fn destroy;
	int ret;

	if (*instance == NULL || mailbox->plugin_instance_valid == 0U ||
		request->payload_length != sizeof(entry_offset)) {
		return PM_EINVAL;
	}
	ret = copy_request_data(mailbox, request, &entry_offset,
		sizeof(entry_offset));
	if (ret != 0 || entry_offset >= text_size) {
		return PM_EINVAL;
	}
	destroy = (pm_plugin_destroy_fn)(text_base + entry_offset + 1U);
	if (!executable_address_valid(text_base, text_size, (uintptr_t)destroy)) {
		return PM_EINVAL;
	}
	ret = destroy(*instance);
	if (ret == PM_OK) {
		*instance = NULL;
		mailbox->plugin_instance_offset = 0U;
		mailbox->plugin_instance_valid = 0U;
	}
	return ret;
}

static struct ef800_measurement_instance *measurement_instance_get(
	struct ef800_plugin_worker_mailbox *mailbox, uintptr_t rw_base,
	size_t rw_size, uintptr_t text_base, size_t text_size,
	uintptr_t rodata_base, size_t rodata_size)
{
	struct ef800_measurement_instance *instance;
	const struct ef800_measurement_api *api;

	if (mailbox->plugin_instance_valid == 0U ||
		mailbox->plugin_instance_offset > rw_size ||
		sizeof(*instance) > rw_size - mailbox->plugin_instance_offset) {
		return NULL;
	}
	instance = (struct ef800_measurement_instance *)(rw_base +
		mailbox->plugin_instance_offset);
	api = instance->api;
	if (api == NULL || !memory_range_contains(rodata_base, rodata_size,
		(uintptr_t)api, sizeof(*api)) ||
		api->abi_version != EF800_MEASUREMENT_API_VERSION ||
		api->struct_size < sizeof(*api) ||
		api->ui_abi_version != EF800_MEASUREMENT_UI_API_VERSION ||
		api->ui_struct_size < sizeof(*api) ||
		!executable_address_valid(text_base, text_size,
			(uintptr_t)api->measure) ||
		!executable_address_valid(text_base, text_size,
			(uintptr_t)api->render) ||
		!executable_address_valid(text_base, text_size,
			(uintptr_t)api->handle_key)) {
		return NULL;
	}
	return instance;
}

static int32_t worker_invoke(struct ef800_plugin_worker_mailbox *mailbox,
	struct ef800_plugin_worker_message *request, uintptr_t text_base,
	size_t text_size, uintptr_t rw_base, size_t rw_size,
	uintptr_t rodata_base, size_t rodata_size, void *instance)
{
	struct ef800_plugin_worker_message response = *request;
	uint8_t *output = &mailbox->payload[request->response_offset];
	int ret;

	if (instance == NULL || mailbox->plugin_instance_valid == 0U) {
		return PM_EINVAL;
	}
	if (!memory_range_contains(rw_base, rw_size, (uintptr_t)instance,
				sizeof(void *))) {
		return PM_EPROTO;
	}
	switch (request->operation) {
	case EF800_PLUGIN_WORKER_GET_SERIAL_DEFAULTS: {
		struct ef800_measurement_instance *measurement =
			measurement_instance_get(mailbox, rw_base, rw_size, text_base,
				text_size, rodata_base, rodata_size);
		struct ef800_serial_defaults defaults = {
			.struct_size = sizeof(defaults),
		};

		if (measurement == NULL || request->payload_length != 0U ||
			request->response_capacity < sizeof(defaults) ||
			measurement->api->get_serial_defaults == NULL ||
			!executable_address_valid(text_base, text_size,
				(uintptr_t)measurement->api->get_serial_defaults)) {
			return PM_EPROTO;
		}
		ret = measurement->api->get_serial_defaults(measurement, &defaults);
		if (ret == PM_OK) {
			ret = copy_response_data(mailbox, request, &defaults,
				sizeof(defaults)) == 0 ? PM_OK : PM_EOVERFLOW;
			if (ret == PM_OK) {
				response.response_length = sizeof(defaults);
			}
		}
		break;
	}
	case EF800_PLUGIN_WORKER_MEASURE: {
		struct ef800_measurement_instance *measurement =
			measurement_instance_get(mailbox, rw_base, rw_size, text_base,
				text_size, rodata_base, rodata_size);
		struct ef800_measurement_result result = {
			.struct_size = sizeof(result),
		};

		if (measurement == NULL || request->payload_length != 0U ||
			request->response_capacity < sizeof(result)) {
			return PM_EPROTO;
		}
		ret = measurement->api->measure(measurement, &result);
		if (ret == PM_OK) {
			ret = copy_response_data(mailbox, request, &result,
				sizeof(result)) == 0 ? PM_OK : PM_EOVERFLOW;
			if (ret == PM_OK) {
				response.response_length = sizeof(result);
			}
		}
		break;
	}
	case EF800_PLUGIN_WORKER_SET_CONTEXT: {
		struct ef800_measurement_instance *measurement =
			measurement_instance_get(mailbox, rw_base, rw_size, text_base,
				text_size, rodata_base, rodata_size);
		struct ef800_measurement_context context;

		if (measurement == NULL || request->payload_length != sizeof(context) ||
			measurement->api->set_context == NULL ||
			!executable_address_valid(text_base, text_size,
				(uintptr_t)measurement->api->set_context)) {
			return PM_EPROTO;
		}
		ret = copy_request_data(mailbox, request, &context, sizeof(context));
		if (ret == 0 && context.struct_size >= sizeof(context)) {
			ret = measurement->api->set_context(measurement, &context);
		} else if (ret == 0) {
			ret = PM_EPROTO;
		}
		break;
	}
	case EF800_PLUGIN_WORKER_INPUT: {
		struct ef800_measurement_instance *measurement =
			measurement_instance_get(mailbox, rw_base, rw_size, text_base,
				text_size, rodata_base, rodata_size);
		uint32_t action;

		if (measurement == NULL || request->payload_length != sizeof(action) ||
			measurement->api->handle_input == NULL ||
			!executable_address_valid(text_base, text_size,
				(uintptr_t)measurement->api->handle_input)) {
			return PM_EPROTO;
		}
		ret = copy_request_data(mailbox, request, &action, sizeof(action));
		if (ret == 0) {
			ret = measurement->api->handle_input(measurement, action);
		}
		break;
	}
	case EF800_PLUGIN_WORKER_KEY: {
		struct ef800_measurement_instance *measurement =
			measurement_instance_get(mailbox, rw_base, rw_size, text_base,
				text_size, rodata_base, rodata_size);
		struct ef800_measurement_key_event event;
		uint32_t result = EF800_MEASUREMENT_KEY_NOT_CONSUMED;

		if (measurement == NULL || request->payload_length != sizeof(event) ||
			request->response_capacity < sizeof(result)) {
			return PM_EPROTO;
		}
		ret = copy_request_data(mailbox, request, &event, sizeof(event));
		if (ret == 0 && event.struct_size >= sizeof(event)) {
			ret = measurement->api->handle_key(measurement, &event, &result);
		} else if (ret == 0) {
			ret = PM_EPROTO;
		}
		if (ret == PM_OK) {
			ret = copy_response_data(mailbox, request, &result,
				sizeof(result)) == 0 ? PM_OK : PM_EOVERFLOW;
			if (ret == PM_OK) {
				response.response_length = sizeof(result);
			}
		}
		break;
	}
	case EF800_PLUGIN_WORKER_RENDER: {
		struct ef800_measurement_instance *measurement =
			measurement_instance_get(mailbox, rw_base, rw_size, text_base,
				text_size, rodata_base, rodata_size);
		struct ef800_measurement_display display = {
			.struct_size = sizeof(display),
		};

		if (measurement == NULL || request->payload_length != 0U ||
			request->response_capacity < sizeof(display)) {
			return PM_EPROTO;
		}
		ret = measurement->api->render(measurement, &display);
		if (ret == PM_OK) {
			ret = copy_response_data(mailbox, request, &display,
				sizeof(display)) == 0 ? PM_OK : PM_EOVERFLOW;
			if (ret == PM_OK) {
				response.response_length = sizeof(display);
			}
		}
		break;
	}
	case EF800_PLUGIN_WORKER_GET_WORKSPACE_RECORD: {
		struct ef800_measurement_instance *measurement =
			measurement_instance_get(mailbox, rw_base, rw_size, text_base,
				text_size, rodata_base, rodata_size);
		uint32_t length = 0U;

		if (measurement == NULL || request->payload_length != 0U ||
			request->response_capacity == 0U ||
			measurement->api->get_workspace_record == NULL ||
			!executable_address_valid(text_base, text_size,
				(uintptr_t)measurement->api->get_workspace_record)) {
			return PM_EPROTO;
		}
		ret = measurement->api->get_workspace_record(measurement,
			(char *)output, request->response_capacity, &length);
		if (ret == PM_OK && length < request->response_capacity) {
			response.response_length = length;
		} else if (ret == PM_OK) {
			ret = PM_EOVERFLOW;
		}
		break;
	}
	case EF800_PLUGIN_WORKER_WORKSPACE_RECORD_RESULT: {
		struct ef800_measurement_instance *measurement =
			measurement_instance_get(mailbox, rw_base, rw_size, text_base,
				text_size, rodata_base, rodata_size);
		int32_t status;

		if (measurement == NULL || request->payload_length != sizeof(status) ||
			measurement->api->workspace_record_result == NULL ||
			!executable_address_valid(text_base, text_size,
				(uintptr_t)measurement->api->workspace_record_result)) {
			return PM_EPROTO;
		}
		ret = copy_request_data(mailbox, request, &status, sizeof(status));
		if (ret == 0) {
			ret = measurement->api->workspace_record_result(measurement, status);
		}
		break;
	}
	case EF800_PLUGIN_WORKER_CALCULATE: {
		struct ef800_calculation_instance *calculation = instance;
		const struct ef800_calculation_api *api = calculation->api;
		size_t length = 0U;

		if (api == NULL || !memory_range_contains(rodata_base, rodata_size,
			(uintptr_t)api, sizeof(*api)) ||
			api->abi_version != EF800_CALCULATION_API_VERSION ||
			api->struct_size < sizeof(*api) ||
			!executable_address_valid(text_base, text_size,
				(uintptr_t)api->transform) ||
			request->payload_length > EF800_PLUGIN_MAX_WORKSPACE_SNAPSHOT ||
			request->response_capacity > EF800_PLUGIN_MAX_RESULT) {
			return PM_EPROTO;
		}
		ret = api->transform(instance,
			(const char *)&mailbox->payload[request->payload_offset],
			request->payload_length, (char *)output,
			request->response_capacity, &length);
		if (ret == PM_OK && length <= request->response_capacity) {
			response.response_length = (uint32_t)length;
		} else if (ret == PM_OK) {
			ret = PM_EOVERFLOW;
		}
		break;
	}
	default:
		ret = PM_ENOTSUP;
		break;
	}
	if (ret == PM_OK) {
		request->response_length = response.response_length;
	}
	return ret;
}

static void publish_response(struct ef800_plugin_worker_mailbox *mailbox,
	struct ef800_plugin_worker_message *request, uint32_t sequence,
	int32_t status)
{
	request->kind = EF800_PLUGIN_WORKER_RESULT;
	request->sequence = sequence;
	request->response_length = status == PM_OK ? request->response_length : 0U;
	request->status = status;
	mailbox->message = *request;
	barrier_dmem_fence_full();
	mailbox->worker_sequence = sequence;
	barrier_dmem_fence_full();
}

void ef800_plugin_worker_entry(void *mailbox_arg, void *text_arg,
	void *rw_arg)
{
	struct ef800_plugin_worker_mailbox *mailbox = mailbox_arg;
	uintptr_t text_base = (uintptr_t)text_arg;
	uintptr_t rw_base = (uintptr_t)rw_arg;
	uintptr_t rodata_base = (uintptr_t)mailbox->plugin_rodata_base;
	size_t text_size = mailbox->plugin_text_size;
	size_t rodata_size = mailbox->plugin_rodata_size;
	size_t rw_size = mailbox->plugin_rw_size;
	uint32_t generation = mailbox->worker_generation;
	void *instance = NULL;
	if (mailbox->plugin_instance_valid != 0U &&
		mailbox->plugin_instance_offset < rw_size) {
		instance = (void *)(rw_base + mailbox->plugin_instance_offset);
	}

	while (true) {
		uint32_t sequence = mailbox->host_sequence;
		struct ef800_plugin_worker_message request;
		int32_t status;

		barrier_dmem_fence_full();
		if (sequence == 0U) {
			k_sleep(K_MSEC(1));
			continue;
		}
		request = mailbox->message;
		if (request.kind != EF800_PLUGIN_WORKER_HOST_INVOKE ||
			request.sequence != sequence || request.generation != generation ||
			ef800_plugin_worker_message_validate(&request,
				EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE,
				generation, (uint64_t)k_uptime_get()) != 0) {
			status = PM_EPROTO;
		} else if (request.operation == EF800_PLUGIN_WORKER_CREATE) {
			status = worker_create(mailbox, &request, text_base, text_size,
				rw_base, rw_size, &instance);
		} else if (request.operation == EF800_PLUGIN_WORKER_DESTROY) {
			status = worker_destroy(mailbox, &request, text_base, text_size,
				&instance);
		} else if (request.operation == EF800_PLUGIN_WORKER_STOP) {
			status = instance == NULL ? PM_OK : PM_EBUSY;
		} else {
			status = worker_invoke(mailbox, &request, text_base, text_size,
				rw_base, rw_size, rodata_base, rodata_size, instance);
		}

		publish_response(mailbox, &request, sequence, status);
		/* Terminate after every plugin entry. The supervisor joins this
		 * untrusted call stack before consuming results or reusing its stack. */
		k_thread_abort(k_current_get());
		while (true) {
			k_yield();
		}
	}
}
