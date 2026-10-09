/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_host.h"

#include <errno.h>
#include <string.h>

#if CONFIG_BOARD_NATIVE_SIM
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "plugin_manager/plugin_descriptor.h"
#include "plugin_manager/plugin_contract.h"
#include "plugin_manager/plugin_manager.h"
#include "plugin_manager/plugin_manager_zephyr.h"
#include "plugin_manager/plugin_target_arm.h"
#ifdef CONFIG_BOARD_NATIVE_SIM
#include "plugin_manager/plugin_target_native.h"
#endif

#include "ef800_main.h"
#include "ef800_plugin_contract.h"
#include "ef800_measurement_plugin.h"
#include "ef800_port_service.h"
LOG_MODULE_REGISTER(ef800_plugin_host, LOG_LEVEL_INF);

#define EF800_PLUGIN_DIRECTORY "plugins"
#define EF800_PLUGIN_SUFFIX ".PMP"
#define EF800_PLUGIN_MAX_ENTRIES 16u
#define EF800_PLUGIN_MAX_PACKAGE_SIZE (32u * 1024u)
#if CONFIG_BOARD_NATIVE_SIM
#define EF800_PLUGIN_TEXT_SIZE 0x8000u
#define EF800_PLUGIN_RODATA_SIZE 0x8000u
#define EF800_PLUGIN_DATA_SIZE 0x4000u
#define EF800_PLUGIN_BSS_SIZE 0x4000u
#else
#define EF800_PLUGIN_TEXT_SIZE 0x2000u
#define EF800_PLUGIN_RODATA_SIZE 0x2000u
#define EF800_PLUGIN_DATA_SIZE 0x0800u
#define EF800_PLUGIN_BSS_SIZE 0x0800u
#endif

static struct pm_manager *manager;
static struct ef800_plugin_registry *active_registry;
static bool ready;
K_MUTEX_DEFINE(plugin_host_mutex);
static struct pm_plugin_descriptor descriptor_slots[EF800_PLUGIN_MAX_ENTRIES];
static uint32_t descriptor_slot_ids[EF800_PLUGIN_MAX_ENTRIES];
static bool descriptor_slot_used[EF800_PLUGIN_MAX_ENTRIES];

struct plugin_image_slot {
	bool used;
#if CONFIG_BOARD_NATIVE_SIM
	uint8_t *text;
	uint8_t *rodata;
	uint8_t *data;
	uint8_t *bss;
#else
	uint8_t text[EF800_PLUGIN_TEXT_SIZE] __aligned(4);
	uint8_t rodata[EF800_PLUGIN_RODATA_SIZE] __aligned(4);
	uint8_t data[EF800_PLUGIN_DATA_SIZE] __aligned(4);
	uint8_t bss[EF800_PLUGIN_BSS_SIZE] __aligned(4);
#endif
};

#if CONFIG_BOARD_NATIVE_SIM
#define EF800_PLUGIN_IMAGE_SLOT_COUNT EF800_PLUGIN_MAX_ENTRIES
#else
#define EF800_PLUGIN_IMAGE_SLOT_COUNT 1U
#endif
static struct plugin_image_slot plugin_image_slots[EF800_PLUGIN_IMAGE_SLOT_COUNT];
static struct ef800_port_api port_api;
static struct pm_service service_items[1];
static struct pm_service_table service_table;
static const struct pm_elf_profile elf_profile = {
	.max_sections = 32u,
	.max_image_size = EF800_PLUGIN_TEXT_SIZE,
	.max_ram_size = EF800_PLUGIN_DATA_SIZE,
	.text_address = 0u,
	.rodata_address = 0x2000u,
	.data_address = 0x20010000u,
#if CONFIG_BOARD_NATIVE_SIM
	.machine = PM_ELF_MACHINE_I386,
	.relocation_type = PM_ELF_R_I386_32,
#else
	.machine = PM_ELF_MACHINE_ARM,
	.relocation_type = PM_ELF_R_ARM_ABS32,
#endif
};

static const struct pm_plugin_interface interfaces[] = {
	{
		.interface_id = {{EF800_PLUGIN_MEASUREMENT_UUID_BYTES}},
		.version = 1u,
		.supported_service_ids = (const pm_uuid_t[]){
			{{EF800_PLUGIN_PORT_SERVICE_UUID_BYTES}},
		},
		.supported_service_count = 1u,
	},
	{
		.interface_id = {{EF800_PLUGIN_CALCULATION_UUID_BYTES}},
		.version = 1u,
	},
};

static struct pm_application_contract contract = {
	.application_id = {{EF800_PLUGIN_APPLICATION_UUID_BYTES}},
	.version = EF800_PLUGIN_CONTRACT_VERSION,
	.interfaces = interfaces,
	.interface_count = ARRAY_SIZE(interfaces),
	.services = service_items,
	.service_count = 0u,
};

static int validate_measurement_display(
	const struct ef800_measurement_display *display)
{
	if (display == NULL || display->struct_size < sizeof(*display) ||
		display->row_count > EF800_MEASUREMENT_SCREEN_ROWS ||
		display->selected_action >= 32u ||
		memchr(display->title, '\0', sizeof(display->title)) == NULL ||
		memchr(display->footer, '\0', sizeof(display->footer)) == NULL) {
		return -EINVAL;
	}
	for (uint32_t row = 0U; row < display->row_count; ++row) {
		if (memchr(display->rows[row], '\0', sizeof(display->rows[row])) == NULL) {
			return -EOVERFLOW;
		}
	}
	return 0;
}

static int convert_entry(void *context, uint32_t address,
	enum pm_elf_entry_kind kind, void *function_out)
{
	const struct pm_elf_image *image = context;
	uintptr_t target;

#if CONFIG_BOARD_NATIVE_SIM
	target = (uintptr_t)image->text + (address - image->text_address);
#else
	target = (uintptr_t)image->text +
		((address & ~1u) - image->text_address) + (address & 1u);
#endif

	ARG_UNUSED(kind);
	*(void **)function_out = (void *)target;
	return PM_OK;
}

static int adapt_descriptor(const struct pm_wire_plugin_descriptor *wire,
	const struct pm_elf_image *image,
	const struct pm_package_manifest *manifest,
	const struct pm_plugin_descriptor **result, void *context)
{
	#if !CONFIG_BOARD_NATIVE_SIM
	struct pm_arm_entry_adapter arm_adapter = {
		.executable_base = image->text_address,
		.executable_size = image->text_size,
		.convert = convert_entry,
		.context = (void *)image,
	};
	#endif
	#if CONFIG_BOARD_NATIVE_SIM
	struct pm_native_entry_adapter native_adapter = {
		.executable_base = image->text_address,
		.executable_size = image->text_size,
		.convert = convert_entry,
		.context = (void *)image,
	};
	#endif
	struct pm_descriptor_bind_config bind = {
#if CONFIG_BOARD_NATIVE_SIM
		.resolve = pm_native_entry_resolve,
		.context = &native_adapter,
#else
		.resolve = pm_arm_entry_resolve,
		.context = &arm_adapter,
#endif
	};
	struct pm_plugin_descriptor *descriptor_slot = NULL;
	for (size_t index = 0U; index < ARRAY_SIZE(descriptor_slots); ++index) {
		if (descriptor_slot_used[index] &&
			descriptor_slot_ids[index] == wire->plugin_id) {
			descriptor_slot = &descriptor_slots[index];
			break;
		}
	}
	if (descriptor_slot == NULL) {
		for (size_t index = 0U; index < ARRAY_SIZE(descriptor_slots); ++index) {
			if (!descriptor_slot_used[index]) {
				descriptor_slot_used[index] = true;
				descriptor_slot_ids[index] = wire->plugin_id;
				descriptor_slot = &descriptor_slots[index];
				break;
			}
		}
	}
	if (descriptor_slot == NULL) {
		return PM_ENOMEM;
	}
	int status;

	ARG_UNUSED(manifest);
	ARG_UNUSED(context);
#if CONFIG_BOARD_NATIVE_SIM
	status = pm_native_entry_adapter_validate(&native_adapter, image);
#else
	status = pm_arm_entry_adapter_validate(&arm_adapter, image);
#endif
	if (status != PM_OK) {
		LOG_WRN("Plugin entry adapter validation failed: %d text=%p size=%u",
			status, image == NULL ? NULL : image->text,
			(unsigned)(image == NULL ? 0U : image->text_size));
	}
	if (status == PM_OK) {
		status = pm_wire_descriptor_bind(wire, image, &bind, descriptor_slot);
		if (status != PM_OK) {
			LOG_WRN("Plugin wire descriptor bind failed: %d create=0x%08x destroy=0x%08x",
				status, wire->create_address, wire->destroy_address);
		}
	}
	if (status == PM_OK) {
		*result = descriptor_slot;
	}
	return status;
}

static int allocate_image(void *context, size_t text_size, size_t rodata_size,
	size_t data_size, size_t bss_size, uint8_t **text, uint8_t **rodata,
	uint8_t **data, uint8_t **bss)
{
	struct plugin_image_slot *slot;

	ARG_UNUSED(context);
	if (text == NULL || rodata == NULL || data == NULL || bss == NULL ||
		text_size > EF800_PLUGIN_TEXT_SIZE ||
		rodata_size > EF800_PLUGIN_RODATA_SIZE ||
		data_size > EF800_PLUGIN_DATA_SIZE || bss_size > EF800_PLUGIN_BSS_SIZE) {
		return PM_EOVERFLOW;
	}
	for (size_t index = 0U; index < ARRAY_SIZE(plugin_image_slots); ++index) {
		slot = &plugin_image_slots[index];
		if (!slot->used) {
#if CONFIG_BOARD_NATIVE_SIM
			slot->text = mmap(NULL, EF800_PLUGIN_TEXT_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			slot->rodata = mmap(NULL, EF800_PLUGIN_RODATA_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			slot->data = mmap(NULL, EF800_PLUGIN_DATA_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			slot->bss = mmap(NULL, EF800_PLUGIN_BSS_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (slot->text == MAP_FAILED || slot->rodata == MAP_FAILED ||
				slot->data == MAP_FAILED || slot->bss == MAP_FAILED) {
				if (slot->text != MAP_FAILED) munmap(slot->text, EF800_PLUGIN_TEXT_SIZE);
				if (slot->rodata != MAP_FAILED) munmap(slot->rodata, EF800_PLUGIN_RODATA_SIZE);
				if (slot->data != MAP_FAILED) munmap(slot->data, EF800_PLUGIN_DATA_SIZE);
				if (slot->bss != MAP_FAILED) munmap(slot->bss, EF800_PLUGIN_BSS_SIZE);
				slot->text = slot->rodata = slot->data = slot->bss = NULL;
				return PM_ENOMEM;
			}
#endif
			slot->used = true;
			*text = slot->text;
			*rodata = slot->rodata;
			*data = slot->data;
			*bss = slot->bss;
			return PM_OK;
		}
	}
	return PM_ENOMEM;
}

static void free_image(void *context, uint8_t *text, uint8_t *rodata,
	uint8_t *data, uint8_t *bss)
{
	ARG_UNUSED(context);
	for (size_t index = 0U; index < ARRAY_SIZE(plugin_image_slots); ++index) {
		struct plugin_image_slot *slot = &plugin_image_slots[index];
		if (slot->text == text && slot->rodata == rodata &&
			slot->data == data && slot->bss == bss) {
#if CONFIG_BOARD_NATIVE_SIM
			munmap(slot->text, EF800_PLUGIN_TEXT_SIZE);
			munmap(slot->rodata, EF800_PLUGIN_RODATA_SIZE);
			munmap(slot->data, EF800_PLUGIN_DATA_SIZE);
			munmap(slot->bss, EF800_PLUGIN_BSS_SIZE);
			slot->text = slot->rodata = slot->data = slot->bss = NULL;
#endif
			slot->used = false;
			return;
		}
	}
}

static int port_write(void *context, const uint8_t *data, uint32_t length)
{
	const struct ef800_measurement_transport *transport = context;
	return transport == NULL || transport->write == NULL ? PM_EINVAL :
		transport->write(transport->context, data, length);
}

static int port_read(void *context, uint8_t *data, uint32_t capacity,
	int32_t timeout_ms)
{
	const struct ef800_measurement_transport *transport = context;
	return transport == NULL || transport->read == NULL ? PM_EINVAL :
		transport->read(transport->context, data, timeout_ms);
}

static struct pm_manager_package_config package_config = {
	.elf_profile = &elf_profile,
#if CONFIG_BOARD_NATIVE_SIM
	.target_architecture = PM_PACKAGE_TARGET_NATIVE_SIM,
#else
	.target_architecture = PM_PACKAGE_TARGET_ARM_CORTEX_M,
#endif
	.adapt_wire_descriptor = adapt_descriptor,
	.alloc_image = allocate_image,
	.free_image = free_image,
};

static int available_index(uint32_t plugin_id)
{
	struct pm_available_plugin available;

	for (size_t index = 0u; index < pm_manager_available_count(manager);
		++index) {
		if (pm_manager_available_get(manager, index, &available) == PM_OK &&
			available.plugin_id == plugin_id) {
			return (int)index;
		}
	}
	return -ENOENT;
}

static int activate_measurement(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	void **instance)
{
	int index;
	uint32_t loaded_id;
	int status;

	if (!ready || instance == NULL) {
		return -ENODEV;
	}
	port_api.abi_version = EF800_PORT_API_VERSION;
	port_api.struct_size = sizeof(port_api);
	port_api.context = (void *)transport;
	port_api.write = port_write;
	port_api.read = port_read;
	service_items[0] = (struct pm_service){
		.service_id = {{EF800_PLUGIN_PORT_SERVICE_UUID_BYTES}},
		.version = 1u,
		.vtable = &port_api,
	};
	service_table.items = service_items;
	service_table.count = ARRAY_SIZE(service_items);
	contract.services = service_items;
	contract.service_count = service_table.count;
	index = available_index(plugin_id);
	if (index < 0) {
		return index;
	}
#if CONFIG_BOARD_NATIVE_SIM
	/* Native discovery keeps the validated relocatable image resident.  This
	 * avoids a second FAT read/load cycle through the simulator boundary and
	 * lets activation create the already-registered descriptor instance. */
	status = pm_manager_create_instance(manager, plugin_id);
	if (status == PM_OK) {
		status = pm_manager_get_instance(manager, plugin_id, instance);
		if (status == PM_OK) {
			return PM_OK;
		}
		(void)pm_manager_destroy_instance(manager, plugin_id);
	}
#endif
	LOG_INF("Loading plugin %u index %d config profile=%p adapt=%p alloc=%p manager=%p",
		(unsigned)plugin_id, index, package_config.elf_profile,
		package_config.adapt_wire_descriptor, package_config.alloc_image, manager);
	status = pm_manager_load_available(manager, (size_t)index,
		&package_config, &loaded_id);
	if (status != PM_OK) {
		LOG_WRN("Plugin %u load failed: %d", plugin_id, status);
		return status;
	}
	status = pm_manager_create_instance(manager, loaded_id);
	if (status != PM_OK) {
		LOG_WRN("Plugin %u instance create failed: %d", plugin_id, status);
		(void)pm_manager_unload(manager, loaded_id);
		return status;
	}
	status = pm_manager_get_instance(manager, loaded_id, instance);
	if (status != PM_OK) {
		(void)pm_manager_destroy_instance(manager, loaded_id);
		(void)pm_manager_unload(manager, loaded_id);
	}
	return status;
}

static void deactivate_measurement(uint32_t plugin_id)
{
	(void)pm_manager_destroy_instance(manager, plugin_id);
	#if !CONFIG_BOARD_NATIVE_SIM
	(void)pm_manager_unload(manager, plugin_id);
	#endif
}

int ef800_plugin_host_measurement_session_open(
	struct ef800_measurement_plugin_session *session, uint32_t plugin_id,
	const struct ef800_measurement_transport *transport)
{
	struct ef800_measurement_instance *instance;
	const struct ef800_plugin_registry_entry *entry;
	int status;

	if (session == NULL || transport == NULL || session->state !=
		EF800_MEASUREMENT_SESSION_CLOSED || !ready) {
		return -EINVAL;
	}
	entry = ef800_plugin_registry_find(active_registry, plugin_id);
	if (entry == NULL || entry->state != EF800_PLUGIN_REGISTRY_READY ||
		entry->info.kind != EF800_PLUGIN_KIND_MEASUREMENT ||
		entry->info.measurement_actions == 0U) {
		return -ENODEV;
	}
	port_api.abi_version = EF800_PORT_API_VERSION;
	port_api.struct_size = sizeof(port_api);
	port_api.context = (void *)transport;
	port_api.write = port_write;
	port_api.read = port_read;
	service_items[0] = (struct pm_service){
		.service_id = {{EF800_PLUGIN_PORT_SERVICE_UUID_BYTES}},
		.version = 1u,
		.vtable = &port_api,
	};
	service_table.items = service_items;
	service_table.count = ARRAY_SIZE(service_items);
	contract.services = service_items;
	contract.service_count = service_table.count;
	#if CONFIG_BOARD_NATIVE_SIM
	/* Native discovery keeps the validated image resident. Reuse that
	 * descriptor instead of loading a second copy into another bounded slot. */
	session->loaded_id = plugin_id;
	{
		const struct pm_plugin_descriptor *loaded_descriptor = NULL;
		if (pm_manager_get_descriptor(manager, plugin_id, &loaded_descriptor) == PM_OK &&
			loaded_descriptor != NULL) {
			LOG_INF("Native session descriptor plugin=%u create=%p destroy=%p services=%u",
				(unsigned)plugin_id, loaded_descriptor->create,
				loaded_descriptor->destroy,
				(unsigned)loaded_descriptor->required_service_count);
		}
	}
	LOG_INF("Native session: creating plugin %u instance", (unsigned)plugin_id);
	status = pm_manager_create_instance(manager, session->loaded_id);
	LOG_INF("Native session: create result %d", status);
	if (status != PM_OK) {
		return status;
	}
	#else
	status = pm_manager_load_available(manager, (size_t)available_index(plugin_id),
		&package_config, &session->loaded_id);
	if (status != PM_OK) {
		return status;
	}
	status = pm_manager_create_instance(manager, session->loaded_id);
	if (status == PM_OK) {
		status = pm_manager_get_instance(manager, session->loaded_id,
			(void **)&instance);
	}
	#endif
	#if CONFIG_BOARD_NATIVE_SIM
	LOG_INF("Native session: getting plugin %u instance", (unsigned)session->loaded_id);
	status = pm_manager_get_instance(manager, session->loaded_id,
		(void **)&instance);
	LOG_INF("Native session: get result %d instance=%p", status, instance);
	#endif
	if (status != PM_OK) {
		(void)pm_manager_destroy_instance(manager, session->loaded_id);
	#if !CONFIG_BOARD_NATIVE_SIM
		(void)pm_manager_unload(manager, session->loaded_id);
	#endif
		return status;
	}
	session->plugin_id = plugin_id;
	session->actions = entry->info.measurement_actions;
	session->instance = instance;
	session->transport = transport;
	session->state = EF800_MEASUREMENT_SESSION_ACTIVE;
	return 0;
}

int ef800_plugin_host_measurement_session_measure(
	struct ef800_measurement_plugin_session *session,
	struct ef800_measurement_data *measurement)
{
	struct ef800_measurement_instance *instance;
	struct ef800_measurement_result result = {.struct_size = sizeof(result)};
	int status;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		(session->actions & EF800_MEASUREMENT_ACTION_MEASURE) == 0U ||
		measurement == NULL) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL || instance->api->measure == NULL) {
		return -EPROTO;
	}
	if (instance->api->abi_version != EF800_MEASUREMENT_API_VERSION ||
		instance->api->struct_size < sizeof(struct ef800_measurement_api) ||
		instance->api->ui_abi_version != EF800_MEASUREMENT_UI_API_VERSION ||
		instance->api->ui_struct_size < sizeof(struct ef800_measurement_api) ||
		instance->api->render == NULL || instance->api->handle_key == NULL) {
		return -EPROTO;
	}
	status = instance->api->measure(instance, &result);
	if (status == PM_OK) {
		measurement->distance_mm = result.distance_mm;
		measurement->horizontal_gon_1e4 = result.horizontal_gon_1e4;
		measurement->vertical_gon_1e4 = result.vertical_gon_1e4;
		measurement->control = result.control;
	}
	return status == PM_OK ? 0 : -EIO;
}

int ef800_plugin_host_measurement_session_set_context(
	struct ef800_measurement_plugin_session *session,
	const struct ef800_measurement_context *context)
{
	struct ef800_measurement_instance *instance;

	if (session == NULL || context == NULL || session->state !=
		EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->set_context == NULL ||
		context->struct_size < sizeof(*context)) {
		return -ENOTSUP;
	}
	return instance->api->set_context(instance, context) == PM_OK ? 0 : -EIO;
}

int ef800_plugin_host_measurement_session_input(
	struct ef800_measurement_plugin_session *session, uint32_t input)
{
	uint32_t action = input == EF800_MEASUREMENT_INPUT_UP ?
		EF800_MEASUREMENT_ACTION_UP : input == EF800_MEASUREMENT_INPUT_DOWN ?
		EF800_MEASUREMENT_ACTION_DOWN : input == EF800_MEASUREMENT_INPUT_ENTER ?
		EF800_MEASUREMENT_ACTION_ENTER : 0U;
	struct ef800_measurement_instance *instance;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		action == 0U) {
		return -ENOTSUP;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL || instance->api->handle_input == NULL) {
		return -ENOTSUP;
	}
	return instance->api->handle_input(instance, input) == PM_OK ? 0 : -EIO;
}

int ef800_plugin_host_measurement_session_key(
	struct ef800_measurement_plugin_session *session, uint8_t key,
	bool *consumed)
{
	struct ef800_measurement_key_event event = {
		.struct_size = sizeof(event),
		.code = EF800_MEASUREMENT_KEY_CHARACTER,
		.character = key,
	};
	uint32_t result = EF800_MEASUREMENT_KEY_NOT_CONSUMED;
	struct ef800_measurement_instance *instance;

	if (session == NULL || consumed == NULL ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->handle_key == NULL) return -EPROTO;
	if (key == 0x1BU || key == 0x08U || key == 0x7FU) {
		event.code = EF800_MEASUREMENT_KEY_BACK;
	}
	if (instance->api->handle_key(instance, &event, &result) != PM_OK) {
		return -EIO;
	}
	*consumed = result == EF800_MEASUREMENT_KEY_CONSUMED ||
		result == EF800_MEASUREMENT_KEY_REQUEST_BACK;
	return result == EF800_MEASUREMENT_KEY_ERROR ? -EIO :
		result == EF800_MEASUREMENT_KEY_UNSUPPORTED ? -ENOTSUP : 0;
}

int ef800_plugin_host_measurement_session_render(
	struct ef800_measurement_plugin_session *session,
	struct ef800_measurement_display *display)
{
	struct ef800_measurement_instance *instance;

	if (session == NULL || display == NULL ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance->api == NULL || instance->api->render == NULL ||
		instance->api->ui_abi_version != EF800_MEASUREMENT_UI_API_VERSION ||
		instance->api->ui_struct_size < sizeof(struct ef800_measurement_api)) {
		return -EPROTO;
	}
	return instance->api->render(instance, display) == PM_OK ?
		validate_measurement_display(display) : -EIO;
}

int ef800_plugin_host_measurement_session_workspace_record(
	struct ef800_measurement_plugin_session *session, char *buffer,
	size_t capacity, size_t *length)
{
	struct ef800_measurement_instance *instance;
	uint32_t output_length = 0U;
	int status;

	if (session == NULL || buffer == NULL || length == NULL || capacity == 0U ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->get_workspace_record == NULL) {
		return -ENOTSUP;
	}
	status = instance->api->get_workspace_record(instance, buffer,
		(uint32_t)capacity, &output_length);
	if (status == PM_ENOENT) {
		return -ENODATA;
	}
	if (status != PM_OK || output_length >= capacity) {
		return status == PM_OK ? -EOVERFLOW : -EIO;
	}
	*length = output_length;
	return 0;
}

int ef800_plugin_host_measurement_session_workspace_result(
	struct ef800_measurement_plugin_session *session, int status)
{
	struct ef800_measurement_instance *instance;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->workspace_record_result == NULL) {
		return -ENOTSUP;
	}
	return instance->api->workspace_record_result(instance, status) == PM_OK ?
		0 : -EIO;
}

void ef800_plugin_host_measurement_session_close(
	struct ef800_measurement_plugin_session *session)
{
	if (session == NULL || session->state == EF800_MEASUREMENT_SESSION_CLOSED) {
		return;
	}
	if (manager != NULL && session->loaded_id != 0U) {
		(void)pm_manager_destroy_instance(manager, session->loaded_id);
	#if !CONFIG_BOARD_NATIVE_SIM
		(void)pm_manager_unload(manager, session->loaded_id);
	#endif
	}
	*session = (struct ef800_measurement_plugin_session){0};
}

int ef800_plugin_host_calculate(uint32_t plugin_id,
	const struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length)
{
	int index;
	uint32_t loaded_id;
	void *opaque = NULL;
	struct ef800_calculation_instance *instance;
	struct pm_manager_package_config calculation_config = package_config;
	size_t snapshot_length = 0U;
	int status;

	if (!ready || workspace == NULL || snapshot == NULL || output == NULL ||
		output_length == NULL || snapshot_capacity == 0U || output_capacity == 0U) {
		return -EINVAL;
	}
	index = available_index(plugin_id);
	if (index < 0) {
		return index;
	}
	status = pm_manager_load_available(manager, (size_t)index,
		&calculation_config, &loaded_id);
	if (status != PM_OK) {
		return -EIO;
	}
	status = ef800_workspace_snapshot(workspace, snapshot, snapshot_capacity,
		&snapshot_length);
	if (status != 0) {
		(void)pm_manager_unload(manager, loaded_id);
		return status;
	}
	status = pm_manager_create_instance(manager, loaded_id);
	if (status == PM_OK) {
		status = pm_manager_get_instance(manager, loaded_id, &opaque);
	}
	if (status == PM_OK) {
		instance = opaque;
		if (instance == NULL || instance->api == NULL ||
			instance->api->transform == NULL) {
			status = PM_EPROTO;
		} else {
			status = instance->api->transform(instance, snapshot,
				snapshot_length, output, output_capacity, output_length);
		}
	}
	(void)pm_manager_destroy_instance(manager, loaded_id);
	(void)pm_manager_unload(manager, loaded_id);
	return status == PM_OK ? 0 : -EIO;
}

int ef800_plugin_host_calculate_confirmed(uint32_t plugin_id,
	struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length)
{
	int ret = ef800_plugin_host_calculate(plugin_id, workspace, snapshot,
		snapshot_capacity, output, output_capacity, output_length);

	if (ret != 0) {
		return ret;
	}
	if (*output_length == 0U || *output_length > output_capacity) {
		return -EFBIG;
	}
	/* The host performs the only Workspace mutation after explicit confirmation. */
	return ef800_workspace_append(workspace, output,
		*output_length);
}

static int host_lock(void *context)
{
	ARG_UNUSED(context);
	return k_mutex_lock(&plugin_host_mutex, K_FOREVER) == 0 ? PM_OK : PM_EBUSY;
}

static void host_unlock(void *context)
{
	ARG_UNUSED(context);
	(void)k_mutex_unlock(&plugin_host_mutex);
}

static void register_available_plugins(void)
{
	struct pm_available_plugin available;

	for (size_t index = 0u; index < pm_manager_available_count(manager);
		++index) {
		struct ef800_plugin_descriptor_info info = {
			.struct_size = sizeof(info),
			.interface_version = 1u,
			.kind = EF800_PLUGIN_KIND_CALCULATION,
			.menu_category = EF800_PLUGIN_MENU_CALCULATION,
		};

		if (pm_manager_available_get(manager, index, &available) != PM_OK) {
			continue;
		}
		info.plugin_id = available.plugin_id;
		info.interface_version = available.interface_version;
		info.required_hardware = EF800_PLUGIN_HARDWARE_SERIAL;
		strncpy(info.name, available.name == NULL ? "plugin" : available.name,
			sizeof(info.name) - 1u);
		strncpy(info.version, available.version == NULL ? "unknown" :
			available.version, sizeof(info.version) - 1u);
		if (pm_uuid_equal(&available.interface_id,
			&(pm_uuid_t){{EF800_PLUGIN_MEASUREMENT_UUID_BYTES}})) {
			info.kind = EF800_PLUGIN_KIND_MEASUREMENT;
			info.menu_category = EF800_PLUGIN_MENU_MEASUREMENT;
			info.measurement_actions = 0U;
		}
		if (ef800_plugin_registry_register_external(active_registry, &info) != 0) {
			LOG_WRN("Plugin %u rejected from registry", info.plugin_id);
		}
	}
}

static void inspect_available_metadata(void)
{
	for (size_t index = 0u; index < pm_manager_available_count(manager); ++index) {
		uint32_t plugin_id;
		const struct pm_plugin_descriptor *descriptor;

		if (pm_manager_load_available(manager, index, &package_config,
			&plugin_id) != PM_OK) {
			continue;
		}
		if (pm_manager_get_descriptor(manager, plugin_id, &descriptor) == PM_OK &&
			descriptor != NULL && descriptor->extension_size >=
			sizeof(struct ef800_plugin_metadata)) {
			const struct ef800_plugin_metadata *metadata =
				(const struct ef800_plugin_metadata *)descriptor->extension;
			if (memcmp(metadata->metadata_id,
				(uint8_t[]){EF800_PLUGIN_METADATA_UUID_BYTES}, 16u) == 0 &&
				metadata->version == EF800_PLUGIN_METADATA_VERSION &&
				metadata->size >= sizeof(*metadata)) {
				(void)ef800_plugin_registry_set_hardware(active_registry,
					plugin_id, metadata->required_hardware);
				if (metadata->measurement_ui_version !=
					EF800_MEASUREMENT_UI_API_VERSION ||
					metadata->measurement_ui_size <
					sizeof(struct ef800_measurement_api)) {
					(void)ef800_plugin_registry_record_diagnostic(active_registry,
						plugin_id, EF800_PLUGIN_REGISTRY_INCOMPATIBLE);
				} else {
					struct ef800_plugin_registry_entry *entry =
						(struct ef800_plugin_registry_entry *)
						ef800_plugin_registry_find(active_registry, plugin_id);
					if (entry != NULL) {
						entry->info.measurement_actions =
							metadata->measurement_actions;
						entry->info.serial_baudrate = metadata->serial_baudrate;
						entry->info.serial_data_bits = metadata->serial_data_bits;
						entry->info.serial_parity = metadata->serial_parity;
						entry->info.serial_stop_bits = metadata->serial_stop_bits;
						entry->info.serial_flow_control = metadata->serial_flow_control;
					}
				}
			}
		}
		#if !CONFIG_BOARD_NATIVE_SIM
		(void)pm_manager_unload(manager, plugin_id);
		#endif
	}
}

static void register_loaded_hardware(uint32_t plugin_id)
{
	const struct pm_plugin_descriptor *descriptor;
	const struct ef800_plugin_metadata *metadata;

	if (pm_manager_get_descriptor(manager, plugin_id, &descriptor) != PM_OK ||
		descriptor == NULL || descriptor->extension == NULL ||
		descriptor->extension_size < sizeof(*metadata)) {
		return;
	}
	metadata = (const struct ef800_plugin_metadata *)descriptor->extension;
	if (memcmp(metadata->metadata_id,
		(uint8_t[]){EF800_PLUGIN_METADATA_UUID_BYTES}, 16u) == 0 &&
		metadata->version == EF800_PLUGIN_METADATA_VERSION &&
		metadata->size >= sizeof(*metadata)) {
		(void)ef800_plugin_registry_set_hardware(active_registry, plugin_id,
			metadata->required_hardware);
	}
}

int ef800_plugin_host_init(struct ef800_plugin_registry *registry)
{
	struct pm_manager_config config = {
		.contract = &contract,
		.max_plugins = EF800_PLUGIN_MAX_ENTRIES,
		.lock = host_lock,
		.unlock = host_unlock,
	};
	struct pm_zephyr_filesystem filesystem = {
		.mount = ef800_storage_mount_descriptor(),
		.plugin_directory = EF800_PLUGIN_DIRECTORY,
		.candidate_suffix = EF800_PLUGIN_SUFFIX,
		.max_entries = EF800_PLUGIN_MAX_ENTRIES,
		.max_name_length = EF800_PLUGIN_MAX_NAME_LENGTH,
		.max_package_size = EF800_PLUGIN_MAX_PACKAGE_SIZE,
	};
	int result;

	ready = false;
	active_registry = registry;
	if (registry == NULL) {
		return -EINVAL;
	}
	if (!ef800_storage_is_mounted()) {
		return -ENODEV;
	}
	manager = pm_manager_create(&config);
	if (manager == NULL) {
		return -ENOMEM;
	}
	result = pm_manager_zephyr_attach_filesystem(manager, &filesystem);
	if (result == PM_OK) {
		result = pm_manager_zephyr_scan(manager, &filesystem);
	}
	if (result != PM_OK) {
		LOG_WRN("Plugin discovery unavailable: %d", result);
		pm_manager_destroy(manager);
		manager = NULL;
		return -EIO;
	}
	register_available_plugins();
	inspect_available_metadata();
	ready = true;
	LOG_INF("Plugin discovery found %u package(s)",
		(unsigned)pm_manager_available_count(manager));
	return 0;
}

int ef800_plugin_host_measure(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	struct ef800_measurement_data *measurement)
{
	struct ef800_measurement_instance *instance;
	struct ef800_measurement_result result = {
		.struct_size = sizeof(result),
	};
	int status = activate_measurement(plugin_id, transport,
		(void **)&instance);
	LOG_INF("Native runtime plugin %u activate=%d instance=%p",
		(unsigned)plugin_id, status, instance);

	if (status != 0) {
		return status;
	}
	if (instance == NULL || instance->api == NULL || instance->api->measure == NULL) {
		deactivate_measurement(plugin_id);
		return -EPROTO;
	}
	status = instance->api->measure(instance, &result);
	LOG_INF("Native runtime plugin %u measure=%d",
		(unsigned)plugin_id, status);
	if (status == PM_OK && measurement != NULL) {
		measurement->distance_mm = result.distance_mm;
		measurement->horizontal_gon_1e4 = result.horizontal_gon_1e4;
		measurement->vertical_gon_1e4 = result.vertical_gon_1e4;
		measurement->control = result.control;
	}
	deactivate_measurement(plugin_id);
	LOG_INF("Native runtime plugin %u deactivated", (unsigned)plugin_id);
	return status == PM_OK ? 0 : -EIO;
}

int ef800_plugin_host_input(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport, uint32_t input)
{
	struct ef800_measurement_instance *instance;
	int status = activate_measurement(plugin_id, transport,
		(void **)&instance);

	if (status != 0) {
		return status;
	}
	if (instance == NULL || instance->api == NULL ||
		instance->api->handle_input == NULL) {
		deactivate_measurement(plugin_id);
		return -ENOTSUP;
	}
	status = instance->api->handle_input(instance, input);
	deactivate_measurement(plugin_id);
	return status == PM_OK ? 0 : -EIO;
}

int ef800_plugin_host_serial_config(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	struct ef800_measurement_serial_config *config)
{
	struct ef800_measurement_instance *instance;
	struct ef800_serial_defaults defaults = {
		.struct_size = sizeof(defaults),
	};
	int status;

	if (config == NULL) {
		return -EINVAL;
	}
	/* Defaults are descriptor metadata and do not require a live port. */
	if (transport == NULL) {
		int index;
		struct pm_available_plugin available;

		index = available_index(plugin_id);
		if (index < 0 || pm_manager_available_get(manager, (size_t)index,
			&available) != PM_OK) {
			return -ENOENT;
		}
	}
	status = activate_measurement(plugin_id, transport, (void **)&instance);
	if (status != 0) {
		return status;
	}
	if (instance == NULL || instance->api == NULL ||
		instance->api->get_serial_defaults == NULL) {
		deactivate_measurement(plugin_id);
		return -ENOTSUP;
	}
	status = instance->api->get_serial_defaults(instance, &defaults);
	deactivate_measurement(plugin_id);
	if (status != PM_OK) {
		return -EIO;
	}
	*config = (struct ef800_measurement_serial_config){
		.baudrate = defaults.baudrate,
		.data_bits = (uint8_t)defaults.data_bits,
		.parity = defaults.parity == EF800_SERIAL_PARITY_ODD ?
			EF800_MEASUREMENT_PARITY_ODD :
			defaults.parity == EF800_SERIAL_PARITY_EVEN ?
			EF800_MEASUREMENT_PARITY_EVEN : EF800_MEASUREMENT_PARITY_NONE,
		.stop_bits = (uint8_t)defaults.stop_bits,
		.flow_control = defaults.flow_control == EF800_SERIAL_FLOW_RTS_CTS ?
			EF800_MEASUREMENT_FLOW_RTS_CTS : EF800_MEASUREMENT_FLOW_NONE,
	};
	return 0;
}

bool ef800_plugin_host_is_ready(void)
{
	return ready;
}

size_t ef800_plugin_host_available_count(void)
{
	return manager == NULL ? 0u : pm_manager_available_count(manager);
}
