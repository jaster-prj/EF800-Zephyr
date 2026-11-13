/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "ef800_measurement_plugin.h"
#include "ef800_plugin_ids.h"
#include "ef800_plugin_worker_sdk.h"
#include "plugin_manager/plugin_descriptor.h"
#include "plugin_manager/plugin_manager.h"
#include "plugin_manager/plugin_target_native.h"

struct fake_port {
	const uint8_t *input;
	size_t input_length;
	size_t input_offset;
	uint8_t output[64];
	size_t output_length;
};

static struct ef800_plugin_worker_mailbox worker_mailbox;
static struct fake_port *active_port;
static uint32_t worker_sequence;
#define TEST_WORKER_GENERATION 9u
#define TEST_WORKER_CAPABILITY 0x7a51c0deu

int ef800_plugin_worker_syscall(
	struct ef800_plugin_worker_mailbox *mailbox)
{
	struct ef800_plugin_worker_message *message;

	assert(mailbox == &worker_mailbox);
	message = &mailbox->message;
	assert(ef800_plugin_worker_message_validate(message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE,
		TEST_WORKER_GENERATION, 100U) == 0);
	assert(message->kind == EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST);
	assert(message->capability == TEST_WORKER_CAPABILITY);
	if (message->sequence == 1U) worker_sequence = 0U;
	assert(message->sequence == ++worker_sequence);
	assert(active_port != NULL);
	switch (message->operation) {
	case EF800_PLUGIN_WORKER_PORT_WRITE:
		if (message->payload_length > sizeof(active_port->output) -
			active_port->output_length) {
			message->status = PM_EOVERFLOW;
			break;
		}
		memcpy(active_port->output + active_port->output_length,
			mailbox->payload + message->payload_offset, message->payload_length);
		active_port->output_length += message->payload_length;
		message->status = 0;
		break;
	case EF800_PLUGIN_WORKER_PORT_READ:
		if (active_port->input_offset == active_port->input_length) {
			message->status = -ETIMEDOUT;
			break;
		}
		mailbox->payload[message->response_offset] =
			active_port->input[active_port->input_offset++];
		message->response_length = 1U;
		message->status = 0;
		break;
	case EF800_PLUGIN_WORKER_LOG:
		message->status = 0;
		break;
	default:
		assert(false);
	}
	return 0;
}

static int convert_native_entry(void *context, uint32_t address,
				enum pm_elf_entry_kind kind, void *function_out)
{
	const struct pm_elf_image *image = context;
	uintptr_t target;

	if (image == NULL || function_out == NULL ||
		address < image->text_address ||
		(uint64_t)(address - image->text_address) >= image->text_size) {
		return PM_EPROTO;
	}
	target = (uintptr_t)image->text + (address - image->text_address);
	if (kind == PM_ELF_ENTRY_CREATE) {
		*(pm_plugin_create_fn *)function_out = (pm_plugin_create_fn)target;
	} else if (kind == PM_ELF_ENTRY_DESTROY) {
		*(pm_plugin_destroy_fn *)function_out = (pm_plugin_destroy_fn)target;
	} else {
		return PM_EINVAL;
	}
	return PM_OK;
}

static int adapt_native_descriptor(
	const struct pm_wire_plugin_descriptor *wire,
	const struct pm_elf_image *image,
	const struct pm_package_manifest *manifest,
	const struct pm_plugin_descriptor **descriptor, void *context)
{
	struct pm_native_entry_adapter adapter = {
		.executable_base = image->text_address,
		.executable_size = image->text_size,
		.convert = convert_native_entry,
		.context = (void *)image,
	};
	const struct pm_descriptor_bind_config bind = {
		.resolve = pm_native_entry_resolve,
		.context = &adapter,
	};
	static struct pm_plugin_descriptor bound;

	(void)manifest;
	(void)context;
	if (pm_native_entry_adapter_validate(&adapter, image) != PM_OK) {
		return PM_EPROTO;
	}
	int status = pm_wire_descriptor_bind(wire, image, &bind, &bound);
	if (status == PM_OK) {
		*descriptor = &bound;
	}
	return status;
}

static int dispatch_create(void *context, pm_plugin_create_fn entry,
			   const struct pm_service_table *services, void **instance)
{
	(void)context;
	return entry == NULL ? PM_EINVAL : entry(services, instance);
}

static int dispatch_destroy(void *context, pm_plugin_destroy_fn entry,
			    void *instance)
{
	(void)context;
	return entry == NULL ? PM_EINVAL : entry(instance);
}

static int resolve_worker_import(void *context, const char *name,
	uint32_t *address)
{
	(void)context;
	if (name == NULL || address == NULL ||
		strcmp(name, "ef800_plugin_worker_syscall") != 0) {
		return PM_ENOENT;
	}
	*address = (uint32_t)(uintptr_t)ef800_plugin_worker_syscall;
	return PM_OK;
}

static size_t read_package(const char *path, uint8_t **package)
{
	FILE *file = fopen(path, "rb");
	long size;
	assert(file != NULL);
	assert(fseek(file, 0, SEEK_END) == 0);
	size = ftell(file);
	assert(size > 0);
	assert(fseek(file, 0, SEEK_SET) == 0);
	*package = malloc((size_t)size);
	assert(*package != NULL);
	assert(fread(*package, 1u, (size_t)size, file) == (size_t)size);
	assert(fclose(file) == 0);
	return (size_t)size;
}

static void verify_package_runtime(const char *path, uint32_t expected_id,
				  uint32_t baudrate,
				  const uint8_t *response,
				  size_t response_length)
{
	uint8_t *text = mmap(NULL, 0x8000u, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uint8_t *rodata = mmap(NULL, 0x8000u, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uint8_t *data = mmap(NULL, 0x4000u, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uint8_t *bss = mmap(NULL, 0x4000u, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	static const pm_uuid_t supported_services[] = {
		{{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}},
	};
	static const struct pm_plugin_interface interface = {
	.interface_id = {{EF800_MEASUREMENT_INTERFACE_UUID_BYTES}},
	.version = 1u,
		.supported_service_ids = supported_services,
		.supported_service_count = 1u,
	};
	struct pm_application_contract contract = {
		.application_id = {{EF800_APPLICATION_UUID_BYTES}},
		.version = 1u,
		.interfaces = &interface,
		.interface_count = 1u,
	};
	struct pm_elf_profile profile = {
		.max_sections = 32u,
		.max_image_size = 32u * 1024u,
		.max_ram_size = 16u * 1024u,
		.text_address = 0u,
		.rodata_address = 0x2000u,
		.data_address = 0x20010000u,
		.machine = PM_ELF_MACHINE_I386,
		.relocation_type = PM_ELF_R_I386_32,
	};
	struct pm_manager_package_config package_config = {
		.elf_profile = &profile,
		.target_architecture = PM_PACKAGE_TARGET_NATIVE_SIM,
		.text_storage = text,
		.text_capacity = 0x8000u,
		.rodata_storage = rodata,
		.rodata_capacity = 0x8000u,
		.data_storage = data,
		.data_capacity = 0x4000u,
		.bss_storage = bss,
		.bss_capacity = 0x4000u,
		.resolve = resolve_worker_import,
		.adapt_wire_descriptor = adapt_native_descriptor,
	};
	const struct pm_manager_config manager_config = {
		.contract = &contract,
		.max_plugins = 1u,
	};
	struct ef800_plugin_worker_binding binding = {
		.abi_version = EF800_PLUGIN_WORKER_BINDING_ABI_VERSION,
		.struct_size = sizeof(binding),
		.mailbox = &worker_mailbox,
		.generation = TEST_WORKER_GENERATION,
		.capability = TEST_WORKER_CAPABILITY,
		.deadline_ms = 10000u,
	};
	struct fake_port fake = {
		.input = response,
		.input_length = response_length,
	};
	const struct pm_service service = {
		.service_id = {{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}},
		.version = EF800_PLUGIN_WORKER_SERVICE_VERSION,
		.vtable = &binding,
	};
	struct pm_manager_instance_executor executor = {
		.create = dispatch_create,
		.destroy = dispatch_destroy,
	};
	struct pm_manager *manager;
	struct ef800_measurement_instance *instance;
	struct ef800_serial_defaults defaults = {
		.struct_size = sizeof(defaults),
	};
	struct ef800_measurement_result measurement = {
		.struct_size = sizeof(measurement),
	};
	void *opaque = NULL;
	uint8_t *package = NULL;
	uint32_t loaded_id = 0u;
	int status;
	size_t package_size;

	assert(text != MAP_FAILED && rodata != MAP_FAILED &&
		data != MAP_FAILED && bss != MAP_FAILED);
	active_port = &fake;
	contract.services = &service;
	contract.service_count = 1u;
	package_size = read_package(path, &package);
	manager = pm_manager_create(&manager_config);
	assert(manager != NULL);
	status = pm_manager_load_package(manager, package, package_size,
		&package_config, &loaded_id);
	if (status != PM_OK) {
		fprintf(stderr, "native package load id=%u failed: %d\n",
			(unsigned)expected_id, status);
	}
	assert(status == PM_OK);
	assert(loaded_id == expected_id);
	fprintf(stderr, "native package %u loaded\n", (unsigned)expected_id);
	assert(pm_manager_create_instance_with_executor(manager, loaded_id,
		&executor) == PM_OK);
	fprintf(stderr, "native package %u created\n", (unsigned)expected_id);
	assert(pm_manager_get_instance(manager, loaded_id, &opaque) == PM_OK);
	instance = opaque;
	assert(instance != NULL && instance->api != NULL);
	fprintf(stderr, "native package %u api=%p\n", (unsigned)expected_id,
		(void *)instance->api);
	assert(instance->api->get_serial_defaults(instance, &defaults) == PM_OK);
	assert(defaults.baudrate == baudrate);
	fprintf(stderr, "native package %u defaults ok\n", (unsigned)expected_id);
	assert(instance->api->measure(instance, &measurement) == PM_OK);
	fprintf(stderr, "native package %u measure ok\n", (unsigned)expected_id);
	assert(measurement.distance_mm == 1234);
	assert(measurement.horizontal_gon_1e4 == 1000000);
	assert(measurement.vertical_gon_1e4 == 505000);
	assert(pm_manager_destroy_instance_with_executor(manager, loaded_id,
		&executor) == PM_OK);
	assert(pm_manager_get_instance(manager, loaded_id, &opaque) == PM_ENOENT);
	assert(pm_manager_unload(manager, loaded_id) == PM_OK);
	pm_manager_destroy(manager);
	free(package);
	assert(munmap(text, 0x8000u) == 0);
	assert(munmap(rodata, 0x8000u) == 0);
	assert(munmap(data, 0x4000u) == 0);
	assert(munmap(bss, 0x4000u) == 0);
}

int main(int argc, char **argv)
{
	static const uint8_t elta_response[] =
		"A\r\n#48 0 1.234; 100.0000; 50.5000\r\n";
	static const uint8_t fts_response[] = "1234 0452700 0900000\r\n";

	assert(argc == 3);
	verify_package_runtime(argv[1], 2u, 1200u, elta_response,
		sizeof(elta_response) - 1u);
	verify_package_runtime(argv[2], 3u, 9600u, fts_response,
		sizeof(fts_response) - 1u);
	return 0;
}
