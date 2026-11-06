/* SPDX-License-Identifier: Apache-2.0 */

#include "pm_dummy_plugin_host.h"

#if CONFIG_EF800_SPIKE_USB_CDC_TEST

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "plugin_manager/plugin_descriptor.h"
#include "plugin_manager/plugin_elf.h"
#include "plugin_manager/plugin_manager.h"
#include "plugin_manager/plugin_target_arm.h"

#define SPIKE_PM_DUMMY_PLUGIN_ID 42U
#define SPIKE_PM_DUMMY_INTERFACE_VERSION 1U
#define SPIKE_PM_DUMMY_MAX_IMAGE_SIZE 8192U
#define SPIKE_PM_DUMMY_MAX_RAM_SIZE 4096U
#define SPIKE_PM_DUMMY_DATA_SIZE 4U

extern const uint8_t ef800_spike_dummy_package[];
extern const size_t ef800_spike_dummy_package_size;

static const struct pm_plugin_interface dummy_interface = {
	.interface_id = {{ 2U }},
	.version = SPIKE_PM_DUMMY_INTERFACE_VERSION,
};

static const struct pm_application_contract dummy_contract = {
	.application_id = {{ 1U }},
	.version = 1U,
	.interfaces = &dummy_interface,
	.interface_count = 1U,
};

static struct pm_manager *manager;
static bool package_loaded;
static uint8_t *loaded_text;
static size_t loaded_text_capacity;
static uint8_t *loaded_rodata;
static size_t loaded_rodata_capacity;
static uint8_t *loaded_data;
static size_t loaded_data_capacity;

static int resolve_dummy_import(void *context, const char *name,
				uint32_t *address)
{
	volatile uint32_t *protected_host_word = context;

	if (name == NULL || address == NULL || protected_host_word == NULL) {
		return PM_EINVAL;
	}
	if (strcmp(name, "ef800_spike_test_protected_word") != 0) {
		return PM_ENOENT;
	}
	*address = (uint32_t)(uintptr_t)protected_host_word;
	return PM_OK;
}

static int convert_dummy_entry(void *context, uint32_t address,
			       enum pm_elf_entry_kind kind, void *function_out)
{
	uintptr_t text_base = (uintptr_t)context;
	uintptr_t relocated_address = text_base + (address & ~(uintptr_t)1U);

	if (function_out == NULL) {
		return PM_EINVAL;
	}
	if ((address & 1U) != 0U) {
		relocated_address |= 1U;
	}
	if (kind == PM_ELF_ENTRY_CREATE) {
		*(pm_plugin_create_fn *)function_out =
			(pm_plugin_create_fn)relocated_address;
	} else if (kind == PM_ELF_ENTRY_DESTROY) {
		*(pm_plugin_destroy_fn *)function_out =
			(pm_plugin_destroy_fn)relocated_address;
	} else {
		return PM_EINVAL;
	}
	return PM_OK;
}

static int adapt_dummy_wire_descriptor(
	const struct pm_wire_plugin_descriptor *wire,
	const struct pm_elf_image *image,
	const struct pm_package_manifest *manifest,
	const struct pm_plugin_descriptor **descriptor,
	void *context)
{
	static struct pm_plugin_descriptor bound_descriptor;
	struct pm_arm_entry_adapter adapter = {
		.executable_base = image == NULL ? 0U : image->text_address,
		.executable_size = image == NULL ? 0U : image->text_size,
		.convert = convert_dummy_entry,
		.context = context,
	};
	struct pm_descriptor_bind_config bind = {
		.resolve = pm_arm_entry_resolve,
		.context = &adapter,
	};
	int ret;

	ARG_UNUSED(manifest);
	ARG_UNUSED(context);
	if (descriptor == NULL || image == NULL ||
		pm_arm_entry_adapter_validate(&adapter, image) != PM_OK) {
		return PM_EPROTO;
	}
	ret = pm_wire_descriptor_bind(wire, image, &bind, &bound_descriptor);
	if (ret == PM_OK) {
		*descriptor = &bound_descriptor;
	}
	return ret;
}

static int ensure_manager(void)
{
	const struct pm_manager_config config = {
		.contract = &dummy_contract,
		.max_plugins = 1U,
	};

	if (manager != NULL) {
		return PM_OK;
	}
	if (pm_contract_validate(&dummy_contract) != PM_OK) {
		printk("PM dummy contract invalid\n");
		return PM_EPROTO;
	}
	manager = pm_manager_create(&config);
	if (manager == NULL) {
		printk("PM dummy manager allocation failed\n");
		return PM_ENOMEM;
	}
	return PM_OK;
}

int spike_pm_dummy_plugin_load(uint8_t *text, size_t text_capacity,
			      uint8_t *rodata, size_t rodata_capacity,
			      uint8_t *data, size_t data_capacity,
			      volatile uint32_t *protected_host_word)
{
	struct pm_elf_profile profile;
	struct pm_manager_package_config package_config;
	uint32_t plugin_id = 0U;
	int ret;

	if (text == NULL || rodata == NULL || data == NULL ||
		text_capacity == 0U || rodata_capacity == 0U ||
		data_capacity < SPIKE_PM_DUMMY_DATA_SIZE || protected_host_word == NULL) {
		return PM_EINVAL;
	}
	/* The fixture is linked for this fixed WB55 arena layout. Fail closed if
	 * a linker/configuration change moves any user-visible section. */
	ret = ensure_manager();
	if (ret != PM_OK) {
		return ret;
	}
	if (package_loaded && (loaded_text != text || loaded_rodata != rodata ||
		loaded_data != data)) {
		return PM_EBUSY;
	}

	if (!package_loaded) {
		memset(text, 0, text_capacity);
		memset(rodata, 0, rodata_capacity);
		memset(data, 0, data_capacity);
		profile = (struct pm_elf_profile){
			.max_sections = PM_ELF_MAX_SECTIONS,
			.max_image_size = SPIKE_PM_DUMMY_MAX_IMAGE_SIZE,
			.max_ram_size = SPIKE_PM_DUMMY_MAX_RAM_SIZE,
			.text_address = 0U,
			.rodata_address = 0x1000U,
			.data_address = 0x20000000U,
			.machine = PM_ELF_MACHINE_ARM,
			.relocation_type = PM_ELF_R_ARM_ABS32,
		};
		package_config = (struct pm_manager_package_config){
			.elf_profile = &profile,
			.target_architecture = PM_PACKAGE_TARGET_ARM_CORTEX_M,
			.text_storage = text,
			.text_capacity = text_capacity,
			.rodata_storage = rodata,
			.rodata_capacity = rodata_capacity,
			.data_storage = data,
			.data_capacity = SPIKE_PM_DUMMY_DATA_SIZE,
			.bss_storage = data + SPIKE_PM_DUMMY_DATA_SIZE,
			.bss_capacity = data_capacity - SPIKE_PM_DUMMY_DATA_SIZE,
			.resolve = resolve_dummy_import,
			.resolve_context = (void *)protected_host_word,
			.adapt_wire_descriptor = adapt_dummy_wire_descriptor,
			.wire_descriptor_context = text,
		};
		ret = pm_manager_load_package(manager, ef800_spike_dummy_package,
					      ef800_spike_dummy_package_size,
					      &package_config, &plugin_id);
		if (ret != PM_OK || plugin_id != SPIKE_PM_DUMMY_PLUGIN_ID) {
			printk("PM dummy package load failed ret=%d id=%u\n",
			       ret, (unsigned int)plugin_id);
			memset(text, 0, text_capacity);
			memset(rodata, 0, rodata_capacity);
			memset(data, 0, data_capacity);
			return ret == PM_OK ? PM_EPROTO : ret;
		}
		loaded_text = text;
		loaded_text_capacity = text_capacity;
		loaded_rodata = rodata;
		loaded_rodata_capacity = rodata_capacity;
		loaded_data = data;
		loaded_data_capacity = data_capacity;
		package_loaded = true;
	}

	return PM_OK;
}

int spike_pm_dummy_plugin_unload(void)
{
	int ret;

	if (!package_loaded || manager == NULL) {
		return PM_OK;
	}
	ret = pm_manager_unload(manager, SPIKE_PM_DUMMY_PLUGIN_ID);
	if (ret != PM_OK) {
		return ret;
	}
	memset(loaded_text, 0, loaded_text_capacity);
	memset(loaded_rodata, 0, loaded_rodata_capacity);
	memset(loaded_data, 0, loaded_data_capacity);
	loaded_text = NULL;
	loaded_text_capacity = 0U;
	loaded_rodata = NULL;
	loaded_rodata_capacity = 0U;
	loaded_data = NULL;
	loaded_data_capacity = 0U;
	package_loaded = false;
	return PM_OK;
}

int spike_pm_dummy_plugin_create_instance(
	const struct pm_manager_instance_executor *executor)
{
	if (manager == NULL || !package_loaded) {
		return PM_ENOENT;
	}
	return pm_manager_create_instance_with_executor(manager,
		SPIKE_PM_DUMMY_PLUGIN_ID, executor);
}

int spike_pm_dummy_plugin_destroy_instance(
	const struct pm_manager_instance_executor *executor)
{
	if (manager == NULL || !package_loaded) {
		return PM_ENOENT;
	}
	return pm_manager_destroy_instance_with_executor(manager,
		SPIKE_PM_DUMMY_PLUGIN_ID, executor);
}

int spike_pm_dummy_plugin_abandon_instance(void)
{
	if (manager == NULL || !package_loaded) {
		return PM_ENOENT;
	}
	return pm_manager_abandon_instance(manager, SPIKE_PM_DUMMY_PLUGIN_ID);
}

#else

int spike_pm_dummy_plugin_load(uint8_t *text, size_t text_capacity,
			      uint8_t *rodata, size_t rodata_capacity,
			      uint8_t *data, size_t data_capacity,
			      volatile uint32_t *protected_host_word)
{
	ARG_UNUSED(text);
	ARG_UNUSED(text_capacity);
	ARG_UNUSED(rodata);
	ARG_UNUSED(rodata_capacity);
	ARG_UNUSED(data);
	ARG_UNUSED(data_capacity);
	ARG_UNUSED(protected_host_word);
	return -ENOTSUP;
}

int spike_pm_dummy_plugin_unload(void)
{
	return -ENOTSUP;
}

int spike_pm_dummy_plugin_create_instance(
	const struct pm_manager_instance_executor *executor)
{
	ARG_UNUSED(executor);
	return -ENOTSUP;
}

int spike_pm_dummy_plugin_destroy_instance(
	const struct pm_manager_instance_executor *executor)
{
	ARG_UNUSED(executor);
	return -ENOTSUP;
}

int spike_pm_dummy_plugin_abandon_instance(void)
{
	return -ENOTSUP;
}

#endif
