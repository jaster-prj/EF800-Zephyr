/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Define stable plugin descriptor, UI, key, and calculation ABIs. */
#ifndef EF800_PLUGIN_CONTRACT_H
#define EF800_PLUGIN_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include "ef800_plugin_metadata.h"
#include "ef800_plugin_worker_abi.h"

#define EF800_PLUGIN_CONTRACT_VERSION 4u
#define EF800_CALCULATION_API_VERSION 1u

#define EF800_PLUGIN_MAX_NAME_LENGTH 31u
#define EF800_PLUGIN_MAX_VERSION_LENGTH 15u
#define EF800_PLUGIN_SCREEN_ROWS 6u
#define EF800_PLUGIN_SCREEN_COLUMNS 40u
#define EF800_PLUGIN_MAX_INPUT_LENGTH 32u
#define EF800_PLUGIN_MAX_WORKSPACE_SNAPSHOT \
	EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT
#define EF800_PLUGIN_MAX_RESULT EF800_PLUGIN_WORKER_MAX_RESULT

#define EF800_PLUGIN_APPLICATION_UUID_BYTES \
	0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x61u, 0x70u, \
	0x70u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x00u, 0x01u

#define EF800_PLUGIN_MEASUREMENT_UUID_BYTES \
	0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x6du, 0x65u, \
	0x61u, 0x73u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u

#define EF800_PLUGIN_CALCULATION_UUID_BYTES \
	0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x63u, 0x61u, \
	0x6cu, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u, 0x01u

#define EF800_PLUGIN_PORT_SERVICE_UUID_BYTES \
	0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x70u, 0x6fu, \
	0x72u, 0x74u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u

#define EF800_PLUGIN_UI_SERVICE_UUID_BYTES \
	0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x75u, 0x69u, \
	0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u

enum ef800_plugin_kind {
	EF800_PLUGIN_KIND_MEASUREMENT = 1,
	EF800_PLUGIN_KIND_CALCULATION = 2,
};

enum ef800_plugin_capability {
	EF800_PLUGIN_CAPABILITY_UI = 1u << 0,
	EF800_PLUGIN_CAPABILITY_TEXT_INPUT = 1u << 1,
	EF800_PLUGIN_CAPABILITY_DIRECTIONAL_INPUT = 1u << 2,
};

enum ef800_measurement_action {
	EF800_MEASUREMENT_ACTION_MEASURE = 1u << 0,
	EF800_MEASUREMENT_ACTION_UP = 1u << 1,
	EF800_MEASUREMENT_ACTION_DOWN = 1u << 2,
	EF800_MEASUREMENT_ACTION_ENTER = 1u << 3,
};

enum ef800_plugin_menu_category {
	EF800_PLUGIN_MENU_CALCULATION = 1,
	EF800_PLUGIN_MENU_MEASUREMENT = 2,
};

enum ef800_plugin_hardware_capability {
	EF800_PLUGIN_HARDWARE_SERIAL = 1u << 0,
	EF800_PLUGIN_HARDWARE_SERIAL_FLOW_CONTROL = 1u << 1,
	EF800_PLUGIN_HARDWARE_PARALLEL = 1u << 2,
	EF800_PLUGIN_HARDWARE_USB = 1u << 3,
	EF800_PLUGIN_HARDWARE_I2C = 1u << 4,
	EF800_PLUGIN_HARDWARE_SPI = 1u << 5,
};

struct ef800_plugin_uuid {
	uint8_t bytes[16];
};

struct ef800_calculation_api {
	uint32_t abi_version;
	uint32_t struct_size;
	/* Transform snapshot bytes into bounded output; return 0 or negative errno. */
	int32_t (*transform)(void *instance, const char *snapshot,
		size_t snapshot_length, char *output, size_t capacity,
		size_t *written);
	uint32_t reserved[4];
};

struct ef800_calculation_instance {
	const struct ef800_calculation_api *api;
};

struct ef800_plugin_descriptor_info {
	uint32_t struct_size;
	uint32_t plugin_id;
	uint32_t interface_version;
	uint32_t capabilities;
	uint32_t required_hardware;
	uint32_t worker_services;
	uint32_t measurement_actions;
	uint32_t serial_baudrate;
	uint8_t serial_data_bits;
	uint8_t serial_parity;
	uint8_t serial_stop_bits;
	uint8_t serial_flow_control;
	enum ef800_plugin_kind kind;
	enum ef800_plugin_menu_category menu_category;
	char name[EF800_PLUGIN_MAX_NAME_LENGTH + 1u];
	char version[EF800_PLUGIN_MAX_VERSION_LENGTH + 1u];
};

enum ef800_plugin_key_code {
	EF800_PLUGIN_KEY_UP = 1,
	EF800_PLUGIN_KEY_DOWN,
	EF800_PLUGIN_KEY_LEFT,
	EF800_PLUGIN_KEY_RIGHT,
	EF800_PLUGIN_KEY_ENTER,
	EF800_PLUGIN_KEY_BACK,
	EF800_PLUGIN_KEY_BACKSPACE,
	EF800_PLUGIN_KEY_CHARACTER,
};

struct ef800_plugin_key_event {
	uint32_t struct_size;
	enum ef800_plugin_key_code code;
	uint32_t modifiers;
	uint8_t character;
	uint8_t reserved[3];
};

struct ef800_plugin_screen {
	uint32_t struct_size;
	char title[EF800_PLUGIN_MAX_NAME_LENGTH + 1u];
	char rows[EF800_PLUGIN_SCREEN_ROWS][EF800_PLUGIN_SCREEN_COLUMNS + 1u];
	char footer[EF800_PLUGIN_SCREEN_COLUMNS + 1u];
	uint8_t selected_row;
	uint8_t row_count;
	uint8_t input_active;
	uint8_t reserved;
};

enum ef800_plugin_key_result {
	EF800_PLUGIN_KEY_NOT_CONSUMED = 0,
	EF800_PLUGIN_KEY_CONSUMED = 1,
	EF800_PLUGIN_KEY_REQUEST_BACK = 2,
};

struct ef800_plugin_ui_api {
	uint32_t abi_version;
	uint32_t struct_size;
	/* Fill validated plugin metadata for menu registration. */
	int32_t (*get_menu_info)(void *instance,
		struct ef800_plugin_descriptor_info *info);
	/* Populate a bounded screen model; host validates all fields afterward. */
	int32_t (*render)(void *instance, struct ef800_plugin_screen *screen);
	/* Handle one normalized key and report consumed/back/error outcome. */
	int32_t (*handle_key)(void *instance,
		const struct ef800_plugin_key_event *event,
		enum ef800_plugin_key_result *result);
	uint32_t reserved[4];
};

struct ef800_plugin_ui_instance {
	const struct ef800_plugin_ui_api *api;
};

#endif
