/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_host.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "plugin_manager/plugin_descriptor.h"
#include "plugin_manager/plugin_contract.h"
#include "plugin_manager/plugin_manager.h"
#include "plugin_manager/plugin_manager_zephyr.h"
#include "plugin_manager/plugin_target_arm.h"

#include "ef800_main.h"
#include "ef800_plugin_contract.h"
#include "ef800_measurement_plugin.h"
#include "ef800_port_service.h"
LOG_MODULE_REGISTER(ef800_plugin_host, LOG_LEVEL_INF);

#define EF800_PLUGIN_DIRECTORY ""
#define EF800_PLUGIN_SUFFIX ".PMP"
#define EF800_PLUGIN_MAX_ENTRIES 16u
#define EF800_PLUGIN_MAX_PACKAGE_SIZE (32u * 1024u)
#define EF800_PLUGIN_TEXT_SIZE 0x2000u
#define EF800_PLUGIN_RODATA_SIZE 0x2000u
#define EF800_PLUGIN_DATA_SIZE 0x0800u
#define EF800_PLUGIN_BSS_SIZE 0x0800u

static struct pm_manager *manager;
static struct ef800_plugin_registry *active_registry;
static bool ready;
K_MUTEX_DEFINE(plugin_host_mutex);
static struct pm_plugin_descriptor descriptor;
static uint8_t plugin_text[EF800_PLUGIN_TEXT_SIZE] __aligned(4);
static uint8_t plugin_rodata[EF800_PLUGIN_RODATA_SIZE] __aligned(4);
static uint8_t plugin_data[EF800_PLUGIN_DATA_SIZE] __aligned(4);
static uint8_t plugin_bss[EF800_PLUGIN_BSS_SIZE] __aligned(4);
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
	.service_count = ARRAY_SIZE(service_items),
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
	uintptr_t target = (uintptr_t)image->text +
		((address & ~1u) - image->text_address) + (address & 1u);

	ARG_UNUSED(kind);
	*(void **)function_out = (void *)target;
	return PM_OK;
}

static int adapt_descriptor(const struct pm_wire_plugin_descriptor *wire,
	const struct pm_elf_image *image,
	const struct pm_package_manifest *manifest,
	const struct pm_plugin_descriptor **result, void *context)
{
	struct pm_arm_entry_adapter adapter = {
		.executable_base = image->text_address,
		.executable_size = image->text_size,
		.convert = convert_entry,
		.context = (void *)image,
	};
	struct pm_descriptor_bind_config bind = {
		.resolve = pm_arm_entry_resolve,
		.context = &adapter,
	};
	int status;

	ARG_UNUSED(manifest);
	ARG_UNUSED(context);
	status = pm_arm_entry_adapter_validate(&adapter, image);
	if (status == PM_OK) {
		status = pm_wire_descriptor_bind(wire, image, &bind, &descriptor);
	}
	if (status == PM_OK) {
		*result = &descriptor;
	}
	return status;
}

static int allocate_image(void *context, size_t text_size, size_t rodata_size,
	size_t data_size, size_t bss_size, uint8_t **text, uint8_t **rodata,
	uint8_t **data, uint8_t **bss)
{
	ARG_UNUSED(context);
	if (text_size > sizeof(plugin_text) || rodata_size > sizeof(plugin_rodata) ||
		data_size > sizeof(plugin_data) || bss_size > sizeof(plugin_bss)) {
		return PM_EOVERFLOW;
	}
	*text = plugin_text;
	*rodata = plugin_rodata;
	*data = plugin_data;
	*bss = plugin_bss;
	return PM_OK;
}

static void free_image(void *context, uint8_t *text, uint8_t *rodata,
	uint8_t *data, uint8_t *bss)
{
	ARG_UNUSED(context);
	ARG_UNUSED(text);
	ARG_UNUSED(rodata);
	ARG_UNUSED(data);
	ARG_UNUSED(bss);
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
	status = pm_manager_load_available(manager, (size_t)index,
		&package_config, &loaded_id);
	if (status != PM_OK) {
		return status;
	}
	status = pm_manager_create_instance(manager, loaded_id);
	if (status != PM_OK) {
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
	(void)pm_manager_unload(manager, plugin_id);
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
	if (status != PM_OK) {
		(void)pm_manager_destroy_instance(manager, session->loaded_id);
		(void)pm_manager_unload(manager, session->loaded_id);
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

int ef800_plugin_host_measurement_session_input(
	struct ef800_measurement_plugin_session *session, uint32_t input)
{
	uint32_t action = input == EF800_MEASUREMENT_INPUT_UP ?
		EF800_MEASUREMENT_ACTION_UP : input == EF800_MEASUREMENT_INPUT_DOWN ?
		EF800_MEASUREMENT_ACTION_DOWN : input == EF800_MEASUREMENT_INPUT_ENTER ?
		EF800_MEASUREMENT_ACTION_ENTER : 0U;
	struct ef800_measurement_instance *instance;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		action == 0U || (session->actions & action) == 0U) {
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

void ef800_plugin_host_measurement_session_close(
	struct ef800_measurement_plugin_session *session)
{
	if (session == NULL || session->state == EF800_MEASUREMENT_SESSION_CLOSED) {
		return;
	}
	if (manager != NULL && session->loaded_id != 0U) {
		(void)pm_manager_destroy_instance(manager, session->loaded_id);
		(void)pm_manager_unload(manager, session->loaded_id);
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
					}
				}
			}
		}
		(void)pm_manager_unload(manager, plugin_id);
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

	if (status != 0) {
		return status;
	}
	if (instance == NULL || instance->api == NULL || instance->api->measure == NULL) {
		deactivate_measurement(plugin_id);
		return -EPROTO;
	}
	status = instance->api->measure(instance, &result);
	if (status == PM_OK && measurement != NULL) {
		measurement->distance_mm = result.distance_mm;
		measurement->horizontal_gon_1e4 = result.horizontal_gon_1e4;
		measurement->vertical_gon_1e4 = result.vertical_gon_1e4;
		measurement->control = result.control;
	}
	deactivate_measurement(plugin_id);
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
