/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#ifdef CONFIG_BOARD_NATIVE_SIM
#include <zephyr/drivers/adc/adc_emul.h>
#endif

#include "ef800_max17043.h"
#include "ef800_battery_soc.h"
#include "ef800_battery_soc_gauge.h"
#include "ef800_battery_soc_store.h"
#include "ef800_main.h"
#include "ef800_mcp23017.h"
#include "ef800_persistence.h"
#include "ef800_persistence_legacy.h"
#include "ef800_composition.h"
#include "ef800_lcd_contrast.h"
#include "ef800_lcd_contrast_storage.h"
#ifndef CONFIG_BOARD_NATIVE_SIM
#include "ef800_lcd_contrast_stm32.h"
#endif
#include "ef800_position.h"
#include "ef800_runtime_state.h"
#include "ef800_ports.h"
#include "ef800_port_uart.h"
#include "ef800_console.h"
#include "ef800_shell_transport.h"
#include "ef800_main_usb_msc.h"
#include "ef800_storage_owner.h"
#include "ef800_measurement_zephyr.h"
#include "ef800_t6963.h"
#include "ef800_workspace.h"
#include "keyboard_host.h"
#include "ef800_keyboard_package.h"
#include "ef800_keyboard_i2c.h"
#include "ef800_keyboard_update.h"
#include "ef800_keyboard_update_protocol.h"
#include "ef800_update.h"
#include "ef800_mcumgr_transport.h"
#include "ef800_plugin_host.h"
#include "ef800_usb_manager.h"
#include "ef800_measurement_plugin.h"
#include "ef800_ui.h"
#ifdef CONFIG_BOARD_NATIVE_SIM
#include "ef800_virtual_frontpanel.h"
#include "ef800_virtual_profile.h"
#include "ef800_virtual_protocol.h"
#include "ef800_virtual_usb_peer.h"
#endif

LOG_MODULE_REGISTER(ef800_main_app, LOG_LEVEL_INF);

#if defined(CONFIG_BOARD_NATIVE_SIM) && defined(CONFIG_USBD_MSC_CLASS)
#error "USBMSD0 native-sim must not compile the MainApplication MSC class"
#endif

/* lpuart1 is the EF800 board's PG7/PC0 keyboard link. */
#define KEYBOARD_UART_NODE DT_CHOSEN(zephyr_keyboard_uart)
#define KEYBOARD_RX_QUEUE_SIZE 128U
#define KEYBOARD_COMMAND_TIMEOUT_MS 1000U
#define EF800_USER_NODE DT_PATH(zephyr_user)
#define LCD_NODE EF800_USER_NODE
#define LCD_COLUMNS 40U
#define STATUS_LINE_SIZE 80U
#define EF800_KEYBOARD_PACKAGE_PATH "/DATA:/fwu/EF800_KEYBOARD.pkg"
#define EF800_KEYBOARD_UPLOAD_MAX_SIZE \
	(EF800_KEYBOARD_PACKAGE_HEADER_SIZE + EF800_KEYBOARD_PACKAGE_LIMIT)
#define EF800_KEYBOARD_UPLOAD_ACK 0x06U

static const struct device *const keyboard_uart =
	DEVICE_DT_GET(KEYBOARD_UART_NODE);
#if defined(CONFIG_BOARD_NATIVE_SIM)
static const struct device *const keyboard_i2c;
#else
static const struct device *const keyboard_i2c =
	DEVICE_DT_GET(DT_NODELABEL(i2c1));
#endif
static struct ef800_keyboard_host keyboard_host;
K_MSGQ_DEFINE(keyboard_rx_msgq, sizeof(uint8_t), KEYBOARD_RX_QUEUE_SIZE, 1);
K_MUTEX_DEFINE(keyboard_command_mutex);
K_SEM_DEFINE(keyboard_command_complete, 0, 1);
static volatile bool keyboard_command_pending;
static volatile uint8_t keyboard_pending_command;
static bool keyboard_uart_initialized;
static atomic_t keyboard_ready_indication;
static bool keyboard_updater_request_ready;
static bool keyboard_debug_active;
static bool keyboard_update_active;

static void keyboard_uart_isr(const struct device *dev, void *user_data);

static struct ef800_mcp23017 mcp23017;
static struct ef800_t6963 lcd;
static struct ef800_max17043 fuel_gauge;
static struct ef800_battery_soc_gauge battery_gauge_source;
static struct ef800_battery_soc_store battery_soc_store;
static struct ef800_runtime_state runtime_state;
static struct ef800_persistence persistence;
static struct ef800_composition_service composition_service;
static struct ef800_lcd_contrast lcd_contrast;
static bool composition_service_ready;
static bool qspi_format_confirmation_pending;
static int64_t qspi_format_confirmation_deadline_ms;
static const struct ef800_composition_catalog_entry composition_entries[] = {
#if defined(CONFIG_BOARD_NATIVE_SIM)
	{ "USBMSD0", "USBMSD", 0U, true, true },
	{ "USBMSD1", "USBMSD", 1U, false, false },
#else
	{ "USBMSD0", "USBMSD", 0U, false, true },
	{ "USBMSD1", "USBMSD", 1U, true, true },
#endif
	{ "BATSOC0", "BATSOC", EF800_BATTERY_SOC_SOURCE_NONE, false, true },
	{ "BATSOC1", "BATSOC", EF800_BATTERY_SOC_SOURCE_ADC, false, true },
	{ "BATSOC2", "BATSOC", EF800_BATTERY_SOC_SOURCE_MAX17048, true, true },
	{ "LCDCONTR0", "LCDCONTR", EF800_LCD_CONTRAST_STATIC, true, true },
	{ "LCDCONTR1", "LCDCONTR", EF800_LCD_CONTRAST_PWM, false, true },
};
static const struct ef800_composition_catalog composition_catalog = {
	.entries = composition_entries,
	.entry_count = ARRAY_SIZE(composition_entries),
};
static struct ef800_workspace workspace;
static struct ef800_port_assignment_service port_assignments;
static struct ef800_port_uart_registry port_uart_registry;
static struct ef800_plugin_registry plugin_registry;
static struct ef800_measurement_zephyr_transport measurement_transports[EF800_PORT_COUNT];
static uint8_t measurement_port_ids[EF800_PORT_COUNT];
static size_t measurement_port_count;
static uint8_t selected_measurement_port;
static char assigned_plugin_names[EF800_PORT_COUNT][EF800_PLUGIN_MAX_NAME_LENGTH + 1U];
static bool measurement_pending;
static bool measurement_fixed;
static int32_t measurement_number;
static struct ef800_measurement_record measurement_record;
static char measurement_code[EF800_MEASUREMENT_TEXT_MAX + 1U] = "02";
static bool lcd_ready;
#ifdef CONFIG_BOARD_NATIVE_SIM
static bool virtual_power_off;
#endif
static bool battery_soc_store_ready;
static bool battery_adc_ready;
static bool mcumgr_transport_active;
static const struct adc_dt_spec battery_adc =
	ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), battery);
static bool runtime_state_ready;
static char header_title[17];
static uint8_t menu_selection;
static uint8_t selected_port_index;
static uint8_t selected_builder_id;
static uint32_t selected_plugin_id;
static struct ef800_measurement_plugin_session measurement_plugin_session;
static struct ef800_ui_state ui_state;
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static bool plugin_async_ui_pending;
static uint32_t plugin_async_ui_token;
static enum ef800_plugin_async_operation plugin_async_ui_operation;
static bool plugin_measurement_fixed;
static bool plugin_display_valid;
static struct ef800_measurement_display plugin_display_cache;
static bool plugin_failure_context_valid;
static uint8_t plugin_failure_parent_menu;
static uint8_t plugin_failure_parent_selection;
static uint32_t plugin_failure_plugin_id;
static int plugin_failure_status;
static enum ef800_plugin_failure_class plugin_failure_class;
static struct ef800_plugin_async_result plugin_async_ui_result;
static char calculation_snapshot[EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT];
static bool calculation_result_pending;
#endif

static bool usb_msd_composition_enabled(void)
{
	const struct ef800_composition_snapshot *snapshot =
		ef800_composition_service_active(&composition_service);

	if (snapshot != NULL) {
		for (size_t index = 0U; index < snapshot->effective_count; ++index) {
			const struct ef800_composition_catalog_entry *entry =
				snapshot->effective[index].catalog_entry;

			if (entry != NULL && strcmp(entry->group, "USBMSD") == 0) {
				return strcmp(entry->token, "USBMSD1") == 0;
			}
		}
	}
#if defined(CONFIG_BOARD_NATIVE_SIM)
	return false;
#else
	return true;
#endif
}

static enum ef800_lcd_contrast_mode lcd_contrast_composition_mode(void)
{
	const struct ef800_composition_snapshot *snapshot =
		composition_service_ready ?
		ef800_composition_service_active(&composition_service) : NULL;

	if (snapshot != NULL) {
		for (size_t index = 0U; index < snapshot->effective_count; ++index) {
			const struct ef800_composition_catalog_entry *entry =
				snapshot->effective[index].catalog_entry;

			if (entry != NULL && strcmp(entry->group, "LCDCONTR") == 0) {
				return entry->selection == EF800_LCD_CONTRAST_PWM ?
					EF800_LCD_CONTRAST_PWM : EF800_LCD_CONTRAST_STATIC;
			}
		}
	}
	return EF800_LCD_CONTRAST_STATIC;
}

static const struct ef800_composition_catalog_entry *
battery_soc_composition_entry(void)
{
	const struct ef800_composition_snapshot *snapshot =
		composition_service_ready ?
		ef800_composition_service_active(&composition_service) : NULL;

	if (snapshot != NULL) {
		for (size_t index = 0U; index < snapshot->effective_count; ++index) {
			const struct ef800_composition_catalog_entry *entry =
				snapshot->effective[index].catalog_entry;

			if (entry != NULL && strcmp(entry->group, "BATSOC") == 0) {
				return entry;
			}
		}
	}
	return NULL;
}

static enum ef800_battery_soc_source battery_soc_composition_source(void)
{
	const struct ef800_composition_catalog_entry *entry =
		battery_soc_composition_entry();

	if (entry != NULL && entry->selection <= EF800_BATTERY_SOC_SOURCE_MAX17048) {
		return (enum ef800_battery_soc_source)entry->selection;
	}
	return EF800_BATTERY_SOC_SOURCE_MAX17048;
}

#ifdef CONFIG_BOARD_NATIVE_SIM
static const char *composition_group_token(
	const struct ef800_composition_snapshot *snapshot, const char *group)
{
	if (snapshot == NULL) {
		return NULL;
	}
	for (size_t index = 0U; index < snapshot->effective_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			snapshot->effective[index].catalog_entry;

		if (entry != NULL && strcmp(entry->group, group) == 0) {
			return entry->token;
		}
	}
	return NULL;
}

static void publish_virtual_composition_state(void)
{
	char payload[EF800_VIRTUAL_COMPOSITION_STATE_MAX];
	size_t offset = 0U;
	const struct ef800_composition_snapshot *active = composition_service_ready ?
		ef800_composition_service_active(&composition_service) : NULL;
	const struct ef800_composition_snapshot *pending = composition_service_ready ?
		ef800_composition_service_pending(&composition_service) : NULL;
	const struct ef800_composition_snapshot *selected = pending != NULL ? pending : active;
	int written;
	bool first_group = true;

	if (active == NULL || selected == NULL) {
		static const char unavailable[] =
			"{\"type\":\"composition\",\"version\":1,"
			"\"available\":false,\"restartRequired\":false,\"groups\":[]}";

		ef800_virtual_frontpanel_set_composition_state(unavailable,
			sizeof(unavailable) - 1U);
		return;
	}
	written = snprintk(payload, sizeof(payload),
		"{\"type\":\"composition\",\"version\":1,\"available\":true,"
		"\"restartRequired\":%s,\"groups\":[",
		ef800_composition_service_restart_required(&composition_service) ?
			"true" : "false");
	if (written < 0 || (size_t)written >= sizeof(payload)) {
		return;
	}
	offset = (size_t)written;
	for (size_t index = 0U; index < composition_catalog.entry_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			&composition_catalog.entries[index];
		const char *active_token;
		const char *selected_token;
		bool group_seen = false;

		if (!entry->supported) {
			continue;
		}
		for (size_t previous = 0U; previous < index; ++previous) {
			const struct ef800_composition_catalog_entry *prior =
				&composition_catalog.entries[previous];

			if (prior->supported && strcmp(prior->group, entry->group) == 0) {
				group_seen = true;
				break;
			}
		}
		if (group_seen) {
			continue;
		}
		active_token = composition_group_token(active, entry->group);
		selected_token = composition_group_token(selected, entry->group);
		if (active_token == NULL || selected_token == NULL) {
			continue;
		}
		written = snprintk(&payload[offset], sizeof(payload) - offset,
			"%s{\"id\":\"%s\",\"active\":\"%s\","
			"\"selected\":\"%s\",\"options\":[",
			first_group ? "" : ",", entry->group, active_token, selected_token);
		if (written < 0 || (size_t)written >= sizeof(payload) - offset) {
			return;
		}
		offset += (size_t)written;
		bool first_option = true;
		for (size_t option = 0U; option < composition_catalog.entry_count; ++option) {
			const struct ef800_composition_catalog_entry *choice =
				&composition_catalog.entries[option];

			if (!choice->supported || strcmp(choice->group, entry->group) != 0) {
				continue;
			}
			written = snprintk(&payload[offset], sizeof(payload) - offset,
				"%s{\"token\":\"%s\",\"default\":%s}",
				first_option ? "" : ",", choice->token,
				choice->is_default ? "true" : "false");
			if (written < 0 || (size_t)written >= sizeof(payload) - offset) {
				return;
			}
			offset += (size_t)written;
			first_option = false;
		}
		written = snprintk(&payload[offset], sizeof(payload) - offset, "]}");
		if (written < 0 || (size_t)written >= sizeof(payload) - offset) {
			return;
		}
		offset += (size_t)written;
		first_group = false;
	}
	written = snprintk(&payload[offset], sizeof(payload) - offset, "]}");
	if (written >= 0 && (size_t)written < sizeof(payload) - offset) {
		offset += (size_t)written;
		ef800_virtual_frontpanel_set_composition_state(payload, offset);
	}
}

static int virtual_set_composition(void *context, const char *group,
				   const char *token)
{
	int ret;

	ARG_UNUSED(context);
	if (!composition_service_ready ||
	    !ef800_storage_configuration_is_mounted()) {
		return -ENOTSUP;
	}
	if (ef800_composition_service_restart_required(&composition_service)) {
		return -EALREADY;
	}
	ret = ef800_composition_service_stage_group_selection(&composition_service,
		group, token);
	if (ret == 0) {
		ret = ef800_composition_service_commit(&composition_service);
	}
	if (ret != 0) {
		ef800_composition_service_discard(&composition_service);
	}
	publish_virtual_composition_state();
	return ret;
}
#endif

static int main_contrast_set_pwm(void *context, uint8_t duty_percent)
{
	ARG_UNUSED(context);
#ifdef CONFIG_BOARD_NATIVE_SIM
	ARG_UNUSED(duty_percent);
	return 0;
#else
	return ef800_lcd_contrast_stm32_set(duty_percent);
#endif
}

static void main_contrast_state_changed(void *context,
					 enum ef800_lcd_contrast_mode mode,
					 uint8_t duty_percent)
{
	ARG_UNUSED(context);
#ifdef CONFIG_BOARD_NATIVE_SIM
	ef800_virtual_frontpanel_set_lcd_contrast(
		mode == EF800_LCD_CONTRAST_PWM, duty_percent);
#else
	ARG_UNUSED(mode);
	ARG_UNUSED(duty_percent);
#endif
}

static void main_contrast_diagnostic(void *context, const char *message,
				     int error)
{
	ARG_UNUSED(context);
	LOG_WRN("%s: %d", message, error);
}

static const struct ef800_lcd_contrast_ops main_contrast_ops = {
	.load_duty = ef800_lcd_contrast_storage_load_duty,
	.save_duty = ef800_lcd_contrast_storage_save_duty,
	.set_pwm = main_contrast_set_pwm,
	.state_changed = main_contrast_state_changed,
	.diagnostic = main_contrast_diagnostic,
};

static void clear_unsupported_msd_assignment(void)
{
	bool cleared;
	bool persisted;
	int ret;

	ret = ef800_port_assignment_enforce_msd_policy(&port_assignments,
		usb_msd_composition_enabled(), &cleared, &persisted);
	if (ret != 0 && cleared && !persisted) {
		LOG_ERR("USBMSD0 could not persist Port 4 MSD cleanup: %d; "
			"MSD remains disabled for this boot", ret);
	} else if (ret != 0) {
		LOG_ERR("USBMSD0 Port 4 assignment cleanup failed: %d", ret);
	} else if (cleared) {
		LOG_INF("USBMSD0 cleared stale Port 4 USB MSD assignment");
	}
	if (cleared && !persisted &&
		ef800_port_assignment_get_builder(&port_assignments, 4U) !=
			EF800_PORT_BUILDER_NONE) {
		/* Defensive assertion path: the shared policy must always clear RAM. */
		LOG_ERR("USBMSD0 Port 4 remained assigned after cleanup failure");
		return;
	}
}

enum application_menu {
	APPLICATION_MENU_MAIN = 0,
	APPLICATION_MENU_SETTINGS,
	APPLICATION_MENU_WORKSPACE,
	APPLICATION_MENU_WORKSPACE_MANAGEMENT,
	APPLICATION_MENU_WORKSPACE_VIEW,
	APPLICATION_MENU_WORKSPACE_ROW_ACTIONS,
	APPLICATION_MENU_WORKSPACE_ROW_EDITOR,
	APPLICATION_MENU_WORKSPACE_ROW_DELETE_CONFIRMATION,
	APPLICATION_MENU_WORKSPACE_CREATE,
	APPLICATION_MENU_WORKSPACE_SELECT,
	APPLICATION_MENU_WORKSPACE_LIST,
	APPLICATION_MENU_WORKSPACE_DELETE_CONFIRMATION,
	APPLICATION_MENU_POSITION,
	APPLICATION_MENU_PORTS,
	APPLICATION_MENU_PORT_DEVICES,
	APPLICATION_MENU_PORT_CONNECTIONS,
	APPLICATION_MENU_PORT_DEBUG_CONFIRMATION,
	APPLICATION_MENU_MEASUREMENTS,
	APPLICATION_MENU_MEASUREMENT_ACTIONS,
	APPLICATION_MENU_MEASUREMENT_EDITOR,
	APPLICATION_MENU_CALCULATIONS,
	APPLICATION_MENU_LCD_CONTRAST,
	APPLICATION_MENU_PLUGIN_FAILURE,
	APPLICATION_MENU_CALCULATION_CONFIRMATION,
};

#define WORKSPACE_LIST_MAX 16U

static enum application_menu active_menu = APPLICATION_MENU_MAIN;
static char workspace_input[EF800_WORKSPACE_NAME_MAX + 1U];
static uint8_t workspace_input_length;
static char workspace_names[WORKSPACE_LIST_MAX][EF800_WORKSPACE_NAME_MAX + 1U];
static size_t workspace_name_count;
#define WORKSPACE_VIEW_ROWS 4U
#define WORKSPACE_VIEW_COLUMNS (LCD_COLUMNS)
#define WORKSPACE_VIEW_SCAN_CHUNK 128U
static char workspace_view_lines[WORKSPACE_VIEW_ROWS][WORKSPACE_VIEW_COLUMNS + 1U];
static size_t workspace_view_line_starts[WORKSPACE_VIEW_ROWS];
static size_t workspace_view_line_ends[WORKSPACE_VIEW_ROWS];
static size_t workspace_view_line_count;
static size_t workspace_view_start;
static size_t workspace_view_end;
static size_t workspace_view_file_size;
static size_t workspace_view_selected_row;
static struct ef800_workspace_version workspace_view_version;
static bool workspace_view_active;
static bool workspace_view_position_only;

enum position_editor_field {
	POSITION_EDITOR_STP = 0,
	POSITION_EDITOR_CODE,
	POSITION_EDITOR_HEIGHT,
};

struct position_editor_state {
	bool active;
	enum position_editor_field field;
	uint8_t length;
	char input[EF800_POSITION_TEXT_MAX + 1U];
	struct ef800_position_entry entry;
};

static struct position_editor_state position_editor;

enum measurement_editor_field {
	MEASUREMENT_EDITOR_CODE = 0,
	MEASUREMENT_EDITOR_STAFF_HEIGHT,
};

struct measurement_editor_state {
	bool active;
	enum measurement_editor_field field;
	uint8_t length;
	char input[EF800_MEASUREMENT_TEXT_MAX + 1U];
};

static struct measurement_editor_state measurement_editor;

enum time_editor_field {
	TIME_EDITOR_WEEKDAY,
	TIME_EDITOR_DATE,
	TIME_EDITOR_MONTH,
	TIME_EDITOR_YEAR,
	TIME_EDITOR_HOUR,
	TIME_EDITOR_MINUTE,
	TIME_EDITOR_SECOND,
	TIME_EDITOR_OFFSET,
	TIME_EDITOR_DST,
};

struct time_editor_state {
	bool active;
	enum time_editor_field field;
	uint8_t length;
	char input[8];
	struct rtc_time local;
	struct ef800_time_settings settings;
};

static struct time_editor_state time_editor;

static const char *const menu_items[] = {
	"Workspace",
	"Position",
	"Measurement",
	"Calculation",
	"Settings",
};

static const char *const menu_shortcuts[] = {"w", "s", "m", "c", "e"};
static const char *const settings_items[] = {
	"Set time",
	"Port assignment",
	"Workspace management",
	"LCD contrast",
};
static const char *const settings_shortcuts[] = {"z", "p", "w", "c"};
static const char *const workspace_items[] = {
	"Show Measurements",
	"Show current Position",
};
static const char *const workspace_management_items[] = {
	"Restore current",
	"Load existing",
	"New workspace",
	"List workspaces",
	"Delete workspace",
};
static const char *const workspace_shortcuts[] = {"l", "e", "n", "i", "d"};
static const char *const workspace_view_shortcuts[] = {"m", "p"};
static const char *const workspace_row_action_items[] = {
	"Modify", "Delete", "Cancel",
};
static const char *const workspace_row_action_shortcuts[] = {"m", "d", "c"};

#define WORKSPACE_ROW_EDIT_MAX 255U
struct workspace_row_editor_state {
	struct ef800_workspace_row row;
	char text[WORKSPACE_ROW_EDIT_MAX + 1U];
	size_t length;
	size_t caret;
	size_t viewport_start;
	bool insert_mode;
};

static struct workspace_row_editor_state workspace_row_editor;

static void start_time_editor(void);
static void display_time_editor(void);
static void clear_display(void);
static void display_workspace_list(void);
static void display_workspace_management_menu(void);
static void display_workspace_view(void);
static void display_workspace_row_actions(void);
static void display_workspace_row_editor(void);
static void display_workspace_row_delete_confirmation(void);
static void display_workspace_delete_confirmation(void);
static void display_position_editor(void);
static void display_port_assignment_ports(void);
static void display_port_assignment_devices(void);
static void display_port_assignment_connections(void);
static void display_port_debug_confirmation(void);
static void display_measurement_devices(void);
static void display_measurement_actions(void);
static void display_measurement_plugin_menu(void);
static void display_active_menu(void);
static void display_measurement_editor(void);
static const char *measurement_device_title(uint8_t port_id);
static void display_calculation_menu(void);
static size_t active_menu_count(void);
static void display_line(uint8_t row, const char *text);
static void handle_measurement_editor_key(uint8_t key);
static int trigger_measurement(bool fixed);
static int send_measurement_input(uint32_t input);
static bool selected_measurement_has_submenu(void);
static void close_measurement_plugin_session(void);
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static void process_plugin_async_completion(void);
static int submit_plugin_async(
	const struct ef800_plugin_async_request *request);
#endif
static void start_measurement_editor(void);
static const struct device *port_uart_device(enum ef800_port_uart_id uart_id);
static int port_uart_check_ready(uint8_t port_id, uint32_t connection);
static int claim_loaded_port_assignments(void);
static int reconcile_plugin_assignments(void);
#ifndef CONFIG_BOARD_NATIVE_SIM
static int set_console_for_port(uint8_t port_id);
#endif
#ifdef CONFIG_BOARD_NATIVE_SIM
static int initialize_virtual_measurement_transport(uint8_t port_id,
	uint32_t plugin_id)
{
	const struct ef800_port_uart_route *route;
	const struct device *device;
	struct ef800_measurement_serial_config config;
	const struct ef800_plugin_registry_entry *entry;

	if (port_id == 0U || port_id > EF800_PORT_COUNT || plugin_id == 0U) {
		return -EINVAL;
	}
	if (measurement_transports[port_id - 1U].uart != NULL) {
		return 0;
	}
	entry = ef800_plugin_registry_find(&plugin_registry, plugin_id);
	if (entry == NULL || entry->state != EF800_PLUGIN_REGISTRY_READY ||
		entry->info.serial_baudrate == 0U || entry->info.serial_data_bits == 0U ||
		entry->info.serial_stop_bits == 0U) {
		return -EINVAL;
	}
	route = ef800_port_uart_for_connection(port_id,
		ef800_port_assignment_get_connection(&port_assignments, port_id));
	device = route == NULL ? NULL : port_uart_device(route->uart_id);
	if (device == NULL) {
		return -ENODEV;
	}
	config.baudrate = entry->info.serial_baudrate;
	config.data_bits = entry->info.serial_data_bits;
	config.parity = entry->info.serial_parity;
	config.stop_bits = entry->info.serial_stop_bits;
	config.flow_control = entry->info.serial_flow_control;
	return ef800_measurement_zephyr_init_with_config(
		&measurement_transports[port_id - 1U], device, &config);
}

static int virtual_assign_plugin(void *context, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection);
static int virtual_execute_plugin(void *context, uint8_t port_id);
#endif

static void clear_display(void)
{
	for (uint8_t row = 0U; row < 8U; ++row) {
		display_line(row, "");
	}
}

static size_t ui_menu_count(void *context, enum ef800_ui_menu menu)
{
	ARG_UNUSED(context);
	return menu == (enum ef800_ui_menu)active_menu ? active_menu_count() : 0U;
}

static void ui_render(void *context, const struct ef800_ui_state *state)
{
	ARG_UNUSED(context);
	active_menu = (enum application_menu)state->menu;
	menu_selection = state->selection;
	if (state->diagnostic != EF800_UI_DIAGNOSTIC_NONE) {
		clear_display();
		display_line(0U, "Plugin recovery");
		display_line(1U, state->diagnostic == EF800_UI_DIAGNOSTIC_TIMEOUT ?
			"Plugin timed out" : "Plugin failed");
		display_line(6U, "ENTER acknowledge");
		return;
	}
	display_active_menu();
}

static enum ef800_port_uart_id port_uart_id_for_console(uint8_t port_id)
{
	switch (port_id) {
	case EF800_CONSOLE_NO_TARGET:
		return EF800_PORT_UART_NONE;
	case 1U:
		return EF800_PORT_UART3;
	case 3U:
		return EF800_PORT_UART4;
	case 0U:
	case 2U:
	default:
		return EF800_PORT_UART1;
	}
}

static int reconcile_port_uart_state(void)
{
#ifndef CONFIG_BOARD_NATIVE_SIM
	uint8_t debug_port = 0U;
#endif
	uint8_t console_port;
	int result;

#ifdef CONFIG_BOARD_NATIVE_SIM
	/* native_sim owns its shell/log console outside the EF800 UART routes.
	 * Claiming the physical fallback UART4 returns -ENOSYS and used to abort
	 * assignment reconciliation before the emulated instrument ports started. */
	console_port = EF800_CONSOLE_NO_TARGET;
	ef800_console_clear_target();
	result = 0;
#else
	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		uint32_t plugin_id = ef800_port_assignment_get_plugin_id(
			&port_assignments, port_id);

		if (debug_port == 0U && plugin_id == EF800_PORT_BUILDER_DEBUG) {
			debug_port = port_id;
		}
	}
	console_port = debug_port;
	if (console_port == 0U) {
		console_port = ef800_port_assignment_is_assigned(&port_assignments,
			EF800_CONSOLE_FALLBACK_PORT) ? EF800_CONSOLE_NO_TARGET :
			EF800_CONSOLE_FALLBACK_PORT;
	}
	if (console_port == EF800_CONSOLE_NO_TARGET) {
		ef800_console_clear_target();
		result = 0;
	} else {
		result = set_console_for_port(console_port);
	}
	if (result != 0) {
		ef800_console_clear_target();
		console_port = EF800_CONSOLE_NO_TARGET;
		LOG_WRN("Serial console unavailable; continuing port assignment reconciliation: %d",
			result);
		/* DebugConsole is optional. Do not let its unavailable UART prevent
		 * independent measurement ports from claiming their configured routes. */
		result = 0;
	}
#endif
	{
		char diagnostic[64];
		int length = snprintk(diagnostic, sizeof(diagnostic),
			"console reconcile target=%u result=%d\r\n", console_port, result);
		if (length > 0) {
			(void)ef800_console_write((const uint8_t *)diagnostic, (size_t)length);
		}
	}
	result = ef800_port_uart_registry_init(&port_uart_registry,
								 port_uart_id_for_console(console_port));
	if (result != 0) {
		return result;
	}
	result = claim_loaded_port_assignments();
	if (result != 0) {
		return result;
	}
	ef800_port_uart_startup_complete(&port_uart_registry);
	return 0;
}

static uint8_t uart_owner_for_plugin(uint32_t plugin_id)
{
	const struct ef800_plugin_registry_entry *entry =
		ef800_plugin_registry_find(&plugin_registry, plugin_id);

	return entry != NULL && entry->info.kind == EF800_PLUGIN_KIND_MEASUREMENT ?
		EF800_PORT_BUILDER_DEBUG : (uint8_t)plugin_id;
}

static int clear_stale_plugin_assignment(uint8_t port_id)
{
	int result = ef800_port_assignment_remove(&port_assignments, port_id);
	int runtime_result;

	if (result == 0) {
		return 0;
	}

	/* Keep this boot safe even if deleting the persisted record fails. The
	 * startup cleanup can retry on the next boot. */
	runtime_result = ef800_port_assignment_import_with_connection(&port_assignments,
		port_id, EF800_PORT_BUILDER_NONE, EF800_PORT_CONNECTION_NONE);
	if (runtime_result != 0) {
		LOG_ERR("Cannot clear stale plugin assignment on port %u: remove=%d runtime=%d",
			port_id, result, runtime_result);
		return runtime_result;
	}
	LOG_WRN("Port %u stale plugin assignment cleared for this boot; persistence failed: %d",
		port_id, result);
	return result;
}

static int reconcile_plugin_assignments(void)
{
	int first_error = 0;

	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		uint32_t plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments,
			port_id);
		uint8_t builder_id = ef800_port_assignment_get_builder(&port_assignments,
			port_id);
		const struct ef800_port_device_descriptor *device =
			ef800_port_device_get(builder_id);
		const struct ef800_plugin_registry_entry *entry;
		bool keep;

		if (!ef800_port_assignment_is_assigned(&port_assignments, port_id) ||
			plugin_id == EF800_PORT_BUILDER_NONE || device != NULL) {
			continue;
		}
		entry = ef800_plugin_registry_find(&plugin_registry, plugin_id);
		keep = entry != NULL && entry->state == EF800_PLUGIN_REGISTRY_READY;
		if (keep && entry->info.kind == EF800_PLUGIN_KIND_MEASUREMENT) {
			keep = entry->info.required_hardware == EF800_PLUGIN_HARDWARE_SERIAL &&
				ef800_port_connection_is_valid(port_id,
					ef800_port_assignment_get_connection(&port_assignments,
						port_id));
		}
		if (keep) {
			continue;
		}

		LOG_WRN("Clearing missing or unavailable plugin %u on port %u",
			(unsigned)plugin_id, port_id);
		{
			int result = clear_stale_plugin_assignment(port_id);

			if (result != 0 && first_error == 0) {
				first_error = result;
			}
		}
	}
	return first_error;
}

static int claim_loaded_port_assignments(void)
{
	int first_error = 0;

	for (size_t index = 0U; index < ef800_port_count(); ++index) {
		const struct ef800_port_descriptor *port = ef800_port_at(index);
		uint32_t plugin_id = ef800_port_assignment_get_plugin_id(
			&port_assignments, port->id);
		int result;

		if (plugin_id == EF800_PORT_BUILDER_NONE) {
			continue;
		}
		uint32_t connection = ef800_port_assignment_get_connection(
			&port_assignments, port->id);
		LOG_INF("Restoring port assignment: port=%u builder=%u plugin=%u connection=%u",
			port->id,
			ef800_port_assignment_get_builder(&port_assignments, port->id),
			plugin_id, connection);

		result = port_uart_check_ready(port->id, connection);
		if (result != 0) {
			LOG_WRN("Port %u UART is not ready: %d", port->id, result);
			if (first_error == 0) {
				first_error = result;
			}
			continue;
		}
		result = ef800_port_uart_claim(&port_uart_registry, port->id,
						       uart_owner_for_plugin(plugin_id),
						       ef800_port_assignment_get_connection(
							&port_assignments, port->id));
		if (result != 0) {
			LOG_WRN("Port %u assignment cannot claim UART: %d", port->id,
				result);
			if (first_error == 0) {
				first_error = result;
			}
			continue;
		}
		if (ef800_plugin_registry_find(&plugin_registry, plugin_id) != NULL &&
			ef800_plugin_registry_find(&plugin_registry, plugin_id)->info.kind ==
			EF800_PLUGIN_KIND_MEASUREMENT) {
			struct ef800_measurement_serial_config config;
			const struct ef800_port_uart_route *route =
				ef800_port_uart_for_connection(port->id, connection);
			const struct device *device = route == NULL ? NULL :
				port_uart_device(route->uart_id);

			if (route == NULL || device == NULL) {
				if (first_error == 0) {
					first_error = -ENODEV;
				}
				continue;
			}
			result = ef800_plugin_host_serial_config(plugin_id, NULL, &config);
			if (result == 0) {
				result = ef800_measurement_zephyr_init_with_config(
					&measurement_transports[port->id - 1U], device, &config);
			}
			if (result != 0 && first_error == 0) {
				first_error = result;
			} else if (result == 0) {
				LOG_INF("Measurement transport ready: port=%u plugin=%u uart=%s",
					port->id, plugin_id, route->name);
			}
		}
	}
	return first_error;
}

#ifndef CONFIG_BOARD_NATIVE_SIM
static void default_debug_serial_config(
	struct ef800_measurement_serial_config *config)
{
	*config = (struct ef800_measurement_serial_config){
		.baudrate = 9600U,
		.data_bits = 8U,
		.parity = EF800_MEASUREMENT_PARITY_NONE,
		.stop_bits = 1U,
		.flow_control = EF800_MEASUREMENT_FLOW_NONE,
	};
}

static int set_console_for_port(uint8_t port_id)
{
	struct ef800_measurement_serial_config config;
	int result;

	if (port_id == 0U || port_id == 3U || port_id == 4U) {
		return ef800_console_set_target(port_id);
	}
	default_debug_serial_config(&config);
	result = ef800_serial_config_load(&persistence, EF800_LEGACY_SERIAL_DEBUG,
							 port_id, &config);
	if (result != 0 && result != -ENOENT && result != -ENOTSUP) {
		return result;
	}
	return ef800_console_set_target_with_config(port_id, &config);
}
#endif

static const struct gpio_dt_spec keyboard_reset =
	GPIO_DT_SPEC_GET_OR(EF800_USER_NODE, keyboard_reset_gpios, {0});
static const struct gpio_dt_spec keyboard_updater_request =
	GPIO_DT_SPEC_GET_OR(EF800_USER_NODE,
		keyboard_updater_request_gpios, {0});
static const struct gpio_dt_spec usb_select =
	GPIO_DT_SPEC_GET_OR(EF800_USER_NODE, usb_select_gpios, {0});
static const struct gpio_dt_spec power_down =
	GPIO_DT_SPEC_GET_OR(EF800_USER_NODE, power_down_gpios, {0});

static const struct device *port_uart_device(
	enum ef800_port_uart_id uart_id)
{
	switch (uart_id) {
	case EF800_PORT_UART3:
		return DEVICE_DT_GET(DT_NODELABEL(usart3));
	case EF800_PORT_UART1:
		return DEVICE_DT_GET(DT_NODELABEL(usart1));
	case EF800_PORT_UART4:
		return DEVICE_DT_GET(DT_NODELABEL(uart4));
	default:
		return NULL;
	}
}

static int port_uart_check_ready(uint8_t port_id, uint32_t connection)
{
	const struct ef800_port_uart_route *route =
		ef800_port_uart_for_connection(port_id, connection);
	const struct device *device;

	if (route == NULL) {
		return 0;
	}
	device = port_uart_device(route->uart_id);
	return device != NULL && device_is_ready(device) ? 0 : -ENODEV;
}

#if DT_HAS_COMPAT_STATUS_OKAY(maxim_max17048)
static const struct i2c_dt_spec fuel_gauge_i2c =
	I2C_DT_SPEC_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(maxim_max17048));

static int probe_max17048(void *context)
{
	int ret;

	if (context == NULL) {
		return -ENODEV;
	}
	ret = ef800_max17043_init(context, &fuel_gauge_i2c);
	if (ret != 0) {
		LOG_WRN("MAX17048 deferred probe failed; SOC unavailable until restart: %d",
			ret);
	}
	return ret;
}

static int read_max17048(void *context,
	struct ef800_max17043_status *status)
{
	if (context == NULL) {
		return -ENODEV;
	}
	return ef800_max17043_read(context, status);
}
#endif

static const struct ef800_t6963_config lcd_config = {
	.data = {
		GPIO_DT_SPEC_GET_BY_IDX_OR(LCD_NODE, data_gpios, 0, {0}),
		GPIO_DT_SPEC_GET_BY_IDX_OR(LCD_NODE, data_gpios, 1, {0}),
		GPIO_DT_SPEC_GET_BY_IDX_OR(LCD_NODE, data_gpios, 2, {0}),
		GPIO_DT_SPEC_GET_BY_IDX_OR(LCD_NODE, data_gpios, 3, {0}),
		GPIO_DT_SPEC_GET_BY_IDX_OR(LCD_NODE, data_gpios, 4, {0}),
		GPIO_DT_SPEC_GET_BY_IDX_OR(LCD_NODE, data_gpios, 5, {0}),
		GPIO_DT_SPEC_GET_BY_IDX_OR(LCD_NODE, data_gpios, 6, {0}),
		GPIO_DT_SPEC_GET_BY_IDX_OR(LCD_NODE, data_gpios, 7, {0}),
	},
	.cd = GPIO_DT_SPEC_GET_OR(LCD_NODE, cd_gpios, {0}),
	.wr = GPIO_DT_SPEC_GET_OR(LCD_NODE, wr_gpios, {0}),
	.rd = GPIO_DT_SPEC_GET_OR(LCD_NODE, rd_gpios, {0}),
	.ce = GPIO_DT_SPEC_GET_OR(LCD_NODE, ce_gpios, {0}),
	.fs = GPIO_DT_SPEC_GET_OR(LCD_NODE, fs_gpios, {0}),
	.reset = GPIO_DT_SPEC_GET_OR(LCD_NODE, reset_gpios, {0}),
	.pixels_horizontal = DT_PROP_OR(LCD_NODE, pixels_horizontal, 0),
	.pixels_vertical = DT_PROP_OR(LCD_NODE, pixels_vertical, 0),
	.font_width = DT_PROP_OR(LCD_NODE, font_width, 0),
	.memory_kb = DT_PROP_OR(LCD_NODE, memory_kb, 0),
};

static int configure_keyboard_uart(void)
{
	const struct uart_config config = {
		.baudrate = KB_UART_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	return uart_configure(keyboard_uart, &config);
}

static int cmd_keyboard_upload(const struct shell *shell, size_t argc,
	char **argv);

static int keyboard_upload_read(uint8_t *value, uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() < deadline) {
		if (ef800_console_read(value) == 0) {
			return 0;
		}
		k_msleep(1U);
	}
	return -ETIMEDOUT;
}

static int keyboard_upload_read_u32(uint32_t *value)
{
	uint8_t bytes[4];

	for (size_t i = 0U; i < sizeof(bytes); ++i) {
		if (keyboard_upload_read(&bytes[i], 3000U) != 0) {
			return -ETIMEDOUT;
		}
	}
	*value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
	return 0;
}

static int keyboard_upload_read_magic(uint8_t magic[4])
{
	static const uint8_t expected[] = {0x45U, 0x46U, 0x4BU, 0x42U};
	uint8_t value;
	size_t matched = 0U;

	while (matched < ARRAY_SIZE(expected)) {
		if (keyboard_upload_read(&value, 3000U) != 0) {
			return -ETIMEDOUT;
		}
		if (value == expected[matched]) {
			magic[matched++] = value;
		} else {
			matched = value == expected[0] ? 1U : 0U;
			if (matched != 0U) {
				magic[0] = value;
			}
		}
	}
	return 0;
}

static int keyboard_upload_validate_file(void)
{
	struct fs_file_t file;
	struct fs_dirent entry;
	struct ef800_keyboard_package_header header;
	uint8_t buffer[256];
	uint32_t crc = 0U;
	uint32_t remaining;
	int result;

	result = fs_stat(EF800_KEYBOARD_PACKAGE_PATH, &entry);
	if (result != 0 || entry.type != FS_DIR_ENTRY_FILE ||
		entry.size < EF800_KEYBOARD_PACKAGE_HEADER_SIZE) {
		return -EINVAL;
	}
	fs_file_t_init(&file);
	result = fs_open(&file, EF800_KEYBOARD_PACKAGE_PATH, FS_O_READ);
	if (result != 0) {
		return result;
	}
	result = fs_read(&file, &header, sizeof(header)) == sizeof(header) &&
		ef800_keyboard_package_valid(&header, entry.size) ? 0 : -EINVAL;
	if (result == 0) {
		remaining = header.image_size;
		while (remaining != 0U) {
			size_t count = MIN(sizeof(buffer), remaining);
			if (fs_read(&file, buffer, count) != (ssize_t)count) {
				result = -EIO;
				break;
			}
			crc = ef800_crc32_update(crc, buffer, count);
			remaining -= count;
		}
		if (result == 0 && crc != header.image_crc32) {
			result = -EBADMSG;
		}
	}
	(void)fs_close(&file);
	return result;
}

static int cmd_keyboard_upload(const struct shell *shell, size_t argc,
	char **argv)
{
	struct fs_file_t file;
	uint8_t buffer[256];
	uint8_t header[4];
	uint8_t reserved[4];
	uint32_t size;
	uint32_t expected_crc;
	uint32_t crc = 0U;
	uint32_t received = 0U;
	int result = 0;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (keyboard_update_active || !ef800_storage_is_mounted() ||
		ef800_main_usb_msd_requested()) {
		shell_error(shell, "keyboard upload unavailable: storage busy");
		return -EBUSY;
	}
	keyboard_update_active = true;
	ef800_shell_set_binary_mode(true);
	(void)ef800_console_write((const uint8_t *)"keyboard upload: ready\r\n",
		strlen("keyboard upload: ready\r\n"));
	/* Let the shell worker finish consuming the command line before the
	 * transport switches to the raw package stream. */
	k_msleep(100U);
	{
		uint8_t acknowledgment;

		if (keyboard_upload_read(&acknowledgment, 3000U) != 0 ||
			acknowledgment != EF800_KEYBOARD_UPLOAD_ACK) {
			result = -EPROTO;
			goto done;
		}
		(void)ef800_console_write((const uint8_t *)"A", 1U);
	}
	if (keyboard_upload_read_magic(header) != 0) {
		(void)ef800_console_write((const uint8_t *)"H", 1U);
		result = -EPROTO;
		goto done;
	}
	if (memcmp(header, (uint8_t[]){0x45U, 0x46U, 0x4BU, 0x42U},
		sizeof(header)) != 0) {
		(void)ef800_console_write((const uint8_t *)"M", 1U);
		result = -EPROTO;
		goto done;
	}
	if (keyboard_upload_read_u32(&size) != 0 ||
		keyboard_upload_read_u32(&expected_crc) != 0) {
		(void)ef800_console_write((const uint8_t *)"D", 1U);
		result = -EPROTO;
		goto done;
	}
	for (size_t i = 0U; i < sizeof(reserved); ++i) {
		if (keyboard_upload_read(&reserved[i], 3000U) != 0) {
			result = -ETIMEDOUT;
			goto done;
		}
	}
	if (size == 0U || size > EF800_KEYBOARD_UPLOAD_MAX_SIZE) {
		result = -EFBIG;
		goto done;
	}
	fs_file_t_init(&file);
	(void)fs_mkdir("/DATA:/fwu");
	result = fs_open(&file, EF800_KEYBOARD_PACKAGE_PATH,
		FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (result != 0) {
		goto done;
	}
	(void)ef800_console_write((const uint8_t *)"M", 1U);
	while (received < size) {
		size_t count = MIN(sizeof(buffer), size - received);
		for (size_t i = 0U; i < count; ++i) {
			if (keyboard_upload_read(&buffer[i], 3000U) != 0) {
				result = -ETIMEDOUT;
				goto close_file;
			}
		}
		if (fs_write(&file, buffer, count) != (ssize_t)count) {
			result = -EIO;
			goto close_file;
		}
		crc = ef800_crc32_update(crc, buffer, count);
		received += count;
	}
	(void)fs_close(&file);
	(void)ef800_console_write((const uint8_t *)"R", 1U);
	if (crc != expected_crc || keyboard_upload_validate_file() != 0) {
		result = -EBADMSG;
		(void)ef800_console_write((const uint8_t *)"E", 1U);
		goto done;
	}
	(void)ef800_console_write((const uint8_t *)"K", 1U);
	shell_print(shell, "keyboard package upload successful (%u bytes)", size);
	goto done;

close_file:
	(void)fs_close(&file);
done:
	ef800_shell_set_binary_mode(false);
	keyboard_update_active = false;
	if (result != 0) {
		(void)ef800_console_write((const uint8_t *)"E", 1U);
		shell_error(shell, "keyboard package upload failed: %d; file retained",
			result);
	}
	return result;
}

static int keyboard_update_read_package(struct fs_file_t *file,
	struct ef800_keyboard_package_header *header, size_t file_size)
{
	uint8_t buffer[1024];
	uint32_t crc = 0U;
	uint32_t remaining;

	if (fs_seek(file, 0, FS_SEEK_SET) != 0 ||
		fs_read(file, header, sizeof(*header)) != sizeof(*header) ||
		!ef800_keyboard_package_valid(header, file_size)) {
		return -EINVAL;
	}
	if (fs_seek(file, header->header_size, FS_SEEK_SET) != 0) {
		return -EIO;
	}
	remaining = header->image_size;
	while (remaining != 0U) {
		size_t count = MIN(sizeof(buffer), remaining);
		if (fs_read(file, buffer, count) != (ssize_t)count) {
			return -EIO;
		}
		crc = ef800_crc32_update(crc, buffer, count);
		remaining -= count;
	}
	return crc == header->image_crc32 ? 0 : -EBADMSG;
}

#define KEYBOARD_UPDATE_RESPONSE_TIMEOUT_MS 30000U
#define KEYBOARD_UPDATE_POLL_MS 10U

static uint32_t keyboard_update_get_le32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void keyboard_update_put_le16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static void keyboard_update_put_le32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static int keyboard_update_request(uint16_t address, uint8_t command,
					   const uint8_t *payload, size_t payload_length,
					   uint8_t *response, size_t response_length,
					   uint32_t timeout_ms)
{
	uint8_t frame[EF800_KB_UPDATE_REQUEST_MAX];
	size_t frame_length;
	int64_t deadline;
	int result;

	if (!device_is_ready(keyboard_i2c) || response == NULL ||
	    response_length == 0U) {
		return -ENODEV;
	}
	frame_length = ef800_kb_update_frame_encode(command, payload,
		payload_length, frame, sizeof(frame));
	if (frame_length == 0U) {
		return -EINVAL;
	}
	result = i2c_write(keyboard_i2c, frame, frame_length, address);
	if (result != 0) {
		return result;
	}
	deadline = k_uptime_get() + timeout_ms;
	do {
		memset(response, 0xFF, response_length);
		result = i2c_read(keyboard_i2c, response, response_length, address);
		if (result == 0) {
			if (response[0] == EF800_KB_PROTOCOL_BUSY) {
				k_msleep(KEYBOARD_UPDATE_POLL_MS);
				continue;
			}
			if (response[0] == EF800_KB_PROTOCOL_NACK) {
				return -EPROTO;
			}
			return ef800_kb_update_response_valid(response, response_length) ?
				0 : -EBADMSG;
		}
		k_msleep(KEYBOARD_UPDATE_POLL_MS);
	} while (k_uptime_get() < deadline);
	return result != 0 ? result : -ETIMEDOUT;
}

static int keyboard_update_query_version(uint16_t address, uint8_t *version,
						uint32_t timeout_ms)
{
	uint8_t command = EF800_KB_UPDATE_VERSION_COMMAND;
	int64_t deadline = k_uptime_get() + timeout_ms;
	int result = -EIO;

	/* The bootloader may acknowledge the probe just before the updater has
	 * registered its target address. Retry command writes as well as reads. */
	do {
		result = i2c_write(keyboard_i2c, &command, sizeof(command), address);
		if (result == 0) {
			break;
		}
		k_msleep(KEYBOARD_UPDATE_POLL_MS);
	} while (k_uptime_get() < deadline);
	if (result != 0) {
		return result;
	}
	do {
		result = i2c_read(keyboard_i2c, version,
			EF800_KB_UPDATE_VERSION_SIZE, address);
		if (result == 0) {
			if (version[0] == EF800_KB_PROTOCOL_BUSY) {
				k_msleep(KEYBOARD_UPDATE_POLL_MS);
				continue;
			}
			return version[0] == command &&
				version[4] == EF800_KB_UPDATE_VERSION_ACK ? 0 : -EBADMSG;
		}
		k_msleep(KEYBOARD_UPDATE_POLL_MS);
	} while (k_uptime_get() < deadline);
	return result != 0 ? result : -ETIMEDOUT;
}

static int keyboard_update_enter_bootloader_fallback(uint8_t *version)
{
	int result;
	int release_result;

	if (!keyboard_updater_request_ready || keyboard_reset.port == NULL ||
	    !device_is_ready(keyboard_reset.port)) {
		return -ENODEV;
	}
	/* JP7 carries PE0 low to the L431's pulled-up PB12 during reset. */
	result = gpio_pin_set_dt(&keyboard_updater_request, 1);
	if (result == 0) {
		result = gpio_pin_set_dt(&keyboard_reset, 0);
	}
	if (result == 0) {
		k_msleep(10U);
		result = gpio_pin_set_dt(&keyboard_reset, 1);
	}
	if (result == 0) {
		result = keyboard_update_query_version(
			EF800_KEYBOARD_UPDATER_I2C_ADDRESS, version, 2000U);
	}
	release_result = gpio_pin_set_dt(&keyboard_updater_request, 0);
	if (result == 0 && release_result != 0) {
		result = release_result;
	}
	if (result == 0) {
		LOG_INF("keyboard updater fallback: PB12 strap booted updater %u.%u.%u",
			version[1], version[2], version[3]);
	}
	return result;
}

static int keyboard_update_enter_resident(void)
{
	uint8_t enter = EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER;
	uint8_t enter_response[2];
	uint8_t version[EF800_KB_UPDATE_VERSION_SIZE];
	int64_t deadline;
	int application_result = -EIO;
	int updater_result = -EIO;
	bool entry_acknowledged = false;

	deadline = k_uptime_get() + KEYBOARD_UPDATE_RESPONSE_TIMEOUT_MS;
	do {
		/* The application may already have left 0x2a after accepting a
		 * previous request, even if the controller reported an I/O error
		 * while that transaction was completing. Prefer the updater's live
		 * VERSION response before retrying the idempotent entry command. */
		updater_result = keyboard_update_query_version(
			EF800_KEYBOARD_UPDATER_I2C_ADDRESS, version, 100U);
		if (updater_result == 0) {
			return 0;
		}
		if (!entry_acknowledged) {
			application_result = i2c_write_read(keyboard_i2c,
				EF800_KEYBOARD_I2C_ADDRESS, &enter, sizeof(enter),
				enter_response, sizeof(enter_response));
			if (application_result == 0) {
				if (enter_response[0] != enter ||
				    enter_response[1] != EF800_KEYBOARD_I2C_ACK) {
					LOG_ERR("keyboard updater entry: invalid application ACK "
						"%02x %02x", enter_response[0],
						enter_response[1]);
					application_result = -EPROTO;
					goto bootloader_fallback;
				}
				entry_acknowledged = true;
			}
		}
		k_msleep(20U);
	} while (k_uptime_get() < deadline);
	LOG_ERR("keyboard updater entry timed out: application transfer=%d, "
		"updater transfer=%d response=%02x %02x %02x %02x %02x",
		application_result, updater_result, version[0], version[1],
		version[2], version[3], version[4]);

bootloader_fallback:
	updater_result = keyboard_update_enter_bootloader_fallback(version);
	if (updater_result != 0) {
		LOG_ERR("keyboard updater bootloader fallback failed: %d "
			"(direct entry=%d)", updater_result, application_result);
	}
	return updater_result;
}

static int keyboard_update_identify(uint16_t address)
{
	uint8_t get[12] = {0};
	uint8_t id[5] = {0};
	size_t get_length = address == EF800_KEYBOARD_I2C_ADDRESS ? 11U : 12U;
	uint8_t command_count = address == EF800_KEYBOARD_I2C_ADDRESS ? 7U : 8U;
	bool found_id = false;
	int result;

	result = keyboard_update_request(address, EF800_KB_UPDATE_CMD_GET,
		NULL, 0U, get, get_length, 2000U);
	if (result != 0) {
		LOG_ERR("keyboard update identify GET at 0x%02x failed: %d "
			"response=%02x %02x %02x %02x", address, result,
			get[0], get[1], get[2], get[3]);
		return result;
	}
	if (get[1] != 1U || get[2] != command_count ||
	    get[get_length - 1U] != ef800_kb_update_checksum(get,
		get_length - 1U)) {
		LOG_ERR("keyboard update identify GET at 0x%02x invalid: "
			"response=%02x %02x %02x %02x", address,
			get[0], get[1], get[2], get[3]);
		return -ENOTSUP;
	}
	for (size_t i = 0U; i < get[2]; ++i) {
		found_id |= get[3U + i] == EF800_KB_UPDATE_CMD_GET_ID;
	}
	if (!found_id) {
		return -ENOTSUP;
	}
	result = keyboard_update_request(address, EF800_KB_UPDATE_CMD_GET_ID,
		NULL, 0U, id, sizeof(id), 2000U);
	if (result != 0) {
		LOG_ERR("keyboard update identify GET_ID at 0x%02x failed: %d "
			"response=%02x %02x %02x %02x %02x", address, result,
			id[0], id[1], id[2], id[3], id[4]);
		return result;
	}
	if (id[1] != 2U || id[2] != 0x04U || id[3] != 0x35U) {
		LOG_ERR("keyboard update identify GET_ID at 0x%02x invalid: "
			"response=%02x %02x %02x %02x %02x", address,
			id[0], id[1], id[2], id[3], id[4]);
		return -ENODEV;
	}
	return 0;
}

static int keyboard_update_wait_app(
	const struct ef800_keyboard_package_header *header)
{
	uint8_t command = EF800_KEYBOARD_I2C_CMD_STATUS;
	uint8_t status[EF800_KEYBOARD_I2C_STATUS_SIZE];
	int64_t deadline = k_uptime_get() + KEYBOARD_UPDATE_RESPONSE_TIMEOUT_MS;
	int result;

	do {
		result = i2c_write_read(keyboard_i2c, EF800_KEYBOARD_I2C_ADDRESS,
			&command, 1U, status, sizeof(status));
		if (result == 0 && status[0] == command &&
		    status[4] == EF800_KEYBOARD_I2C_MODE_APPLICATION &&
		    status[5] == EF800_KEYBOARD_I2C_ACK &&
		    (header->image_kind != EF800_KEYBOARD_IMAGE_APPLICATION ||
		     (status[1] == header->version_major &&
		      status[2] == header->version_minor &&
		      status[3] == header->version_patch))) {
			return 0;
		}
		k_msleep(100U);
	} while (k_uptime_get() < deadline);
	return -ETIMEDOUT;
}

static int keyboard_update_wait_application_service(void)
{
	int64_t deadline = k_uptime_get() +
		CONFIG_EF800_MAIN_KEYBOARD_STARTUP_TIMEOUT_MS;

	/* The keyboard emits 0xaa only after registering its I2C target. Wait
	 * for that ISR-observed signal before identifying its updater service. */
	while (atomic_get(&keyboard_ready_indication) == 0 &&
	       k_uptime_get() < deadline) {
		k_msleep(5U);
	}
	return atomic_get(&keyboard_ready_indication) != 0 ? 0 : -ETIMEDOUT;
}

static int keyboard_update_run(void)
{
	struct fs_dirent entry;
	struct fs_file_t file;
	struct ef800_keyboard_package_header header;
	uint8_t data[EF800_KB_UPDATE_BLOCK_MAX];
	uint8_t payload[EF800_KB_UPDATE_BLOCK_MAX + 7U];
	uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX];
	uint32_t vector[2];
	uint32_t image_base;
	size_t image_limit;
	uint32_t source_crc = 0U;
	uint16_t target;
	uint16_t service_address;
	int result;
	bool package_invalid = false;

	if (keyboard_update_active || !ef800_storage_is_mounted() ||
		ef800_main_usb_msd_requested()) {
		return -EAGAIN;
	}
	result = fs_stat(EF800_KEYBOARD_PACKAGE_PATH, &entry);
	if (result == -ENOENT) {
		return 0;
	}
	if (result != 0 || entry.type != FS_DIR_ENTRY_FILE) {
		return result != 0 ? result : -EINVAL;
	}
	fs_file_t_init(&file);
	result = fs_open(&file, EF800_KEYBOARD_PACKAGE_PATH, FS_O_READ);
	if (result != 0) {
		return result;
	}
	keyboard_update_active = true;
	result = keyboard_update_read_package(&file, &header, entry.size);
	if (result != 0) {
		package_invalid = true;
		goto cleanup;
	}
	switch (header.image_kind) {
	case EF800_KEYBOARD_IMAGE_APPLICATION:
		image_base = EF800_KEYBOARD_PACKAGE_BASE;
		image_limit = EF800_KEYBOARD_PACKAGE_LIMIT;
		target = EF800_KB_UPDATE_TARGET_APPLICATION;
		service_address = EF800_KEYBOARD_UPDATER_I2C_ADDRESS;
		break;
	case EF800_KEYBOARD_IMAGE_BOOTLOADER:
		image_base = EF800_KEYBOARD_BOOT_BASE;
		image_limit = EF800_KEYBOARD_BOOT_LIMIT;
		target = EF800_KB_UPDATE_TARGET_BOOTLOADER;
		service_address = EF800_KEYBOARD_UPDATER_I2C_ADDRESS;
		break;
	case EF800_KEYBOARD_IMAGE_UPDATER:
		image_base = EF800_KEYBOARD_UPDATER_BASE;
		image_limit = EF800_KEYBOARD_UPDATER_LIMIT;
		target = EF800_KB_UPDATE_TARGET_UPDATER;
		service_address = EF800_KEYBOARD_I2C_ADDRESS;
		break;
	default:
		result = -EINVAL;
		package_invalid = true;
		goto cleanup;
	}
	if (header.image_size > image_limit ||
	    fs_seek(&file, header.header_size, FS_SEEK_SET) != 0 ||
	    fs_read(&file, vector, sizeof(vector)) != sizeof(vector) ||
	    !ef800_vector_table_valid((const uint32_t *)vector, image_base,
				      header.image_size, 0x20000000U, 0x0000F000U)) {
		result = -EINVAL;
		package_invalid = true;
		goto cleanup;
	}
	if (header.image_kind != EF800_KEYBOARD_IMAGE_UPDATER) {
		result = keyboard_update_enter_resident();
		if (result != 0) {
			LOG_ERR("keyboard update: resident updater handoff failed: %d", result);
			goto cleanup;
		}
	} else {
		/* The app services updater-slot writes at 0x2a. This worker runs
		 * just after reset release, so wait for its 0xaa startup indication
		 * (emitted after I2C target registration) before framed GET. */
		result = keyboard_update_wait_application_service();
		if (result != 0) {
			LOG_ERR("keyboard update: application service not ready: %d",
				result);
			goto cleanup;
		}
	}
	result = keyboard_update_identify(service_address);
	if (result != 0) {
		LOG_ERR("keyboard update: identify failed: %d", result);
		goto cleanup;
	}
	{
		uint8_t begin[9];
		uint8_t erase[2] = {(uint8_t)target,
			EF800_KB_UPDATE_ERASE_PARTITION};

		begin[0] = (uint8_t)target;
		keyboard_update_put_le32(&begin[1], header.image_size);
		keyboard_update_put_le32(&begin[5], header.image_crc32);
		result = keyboard_update_request(service_address,
			EF800_KB_UPDATE_CMD_BEGIN, begin, sizeof(begin), response,
			2U, 2000U);
		if (result == 0) {
			LOG_INF("keyboard update: partition ERASE started target=0x%02x",
				target);
			result = keyboard_update_request(service_address,
				EF800_KB_UPDATE_CMD_ERASE, erase, sizeof(erase),
				response, 2U, 30000U);
		}
		if (result != 0) {
			LOG_ERR("keyboard update: BEGIN/ERASE failed: %d", result);
			goto cleanup;
		}
	}
	for (uint32_t offset = 0U; offset < header.image_size;) {
		size_t length = MIN(sizeof(data), header.image_size - offset);

		if (fs_seek(&file, header.header_size + offset, FS_SEEK_SET) != 0 ||
		    fs_read(&file, data, length) != (ssize_t)length) {
			result = -EIO;
			goto cleanup;
		}
		source_crc = ef800_crc32_update(source_crc, data, length);
		payload[0] = (uint8_t)target;
		keyboard_update_put_le32(&payload[1], offset);
		keyboard_update_put_le16(&payload[5], (uint16_t)length);
		memcpy(&payload[7], data, length);
		result = keyboard_update_request(service_address,
			EF800_KB_UPDATE_CMD_WRITE_MEMORY, payload, length + 7U,
			response, 2U, 5000U);
		if (result != 0) {
			LOG_ERR("keyboard update: WRITE offset 0x%x failed: %d",
				offset, result);
			goto cleanup;
		}
		offset += length;
		if ((offset % 4096U) == 0U || offset == header.image_size) {
			LOG_INF("keyboard update: streamed %u/%u bytes", offset,
				header.image_size);
		}
	}
	if (source_crc != header.image_crc32) {
		result = -EBADMSG;
		goto cleanup;
	}
	{
		uint8_t check_target = (uint8_t)target;

		LOG_INF("keyboard update: Flash CHECK started target=0x%02x",
			target);
		result = keyboard_update_request(service_address,
			EF800_KB_UPDATE_CMD_CHECK, &check_target, 1U, response,
			7U, 30000U);
		if (result != 0 || keyboard_update_get_le32(&response[1]) !=
		    header.image_crc32 || response[5] != 1U) {
			result = result != 0 ? result : -EBADMSG;
			LOG_ERR("keyboard update: Flash CHECK failed: %d", result);
			goto cleanup;
		}
	}
	if (header.image_kind == EF800_KEYBOARD_IMAGE_UPDATER) {
		uint8_t version[EF800_KB_UPDATE_VERSION_SIZE];
		/* A direct application-to-updater branch leaves stale STM32 I2C
		 * peripheral state behind: CHECK can pass, but the updater's 0x48
		 * target then never answers. Reset through the resident bootloader
		 * with JP7 asserted so it performs a clean updater startup. */
		result = keyboard_update_enter_bootloader_fallback(version);
		if (result == 0 &&
		    (version[1] != header.version_major ||
		     version[2] != header.version_minor ||
		     version[3] != header.version_patch)) {
			LOG_ERR("keyboard updater version mismatch: expected %u.%u.%u, "
				"got %u.%u.%u", header.version_major,
				header.version_minor, header.version_patch,
				version[1], version[2], version[3]);
			result = -EBADMSG;
		}
	} else {
		result = keyboard_update_request(service_address,
			EF800_KB_UPDATE_CMD_RUN, NULL, 0U, response, 2U, 2000U);
		if (result == 0) {
			result = keyboard_update_wait_app(&header);
		}
	}
	if (result == 0) {
		result = fs_unlink(EF800_KEYBOARD_PACKAGE_PATH);
	}
	if (result == 0) {
		LOG_INF("keyboard update: transfer and verification complete");
	}
cleanup:
	(void)fs_close(&file);
	if (package_invalid) {
		(void)fs_unlink(EF800_KEYBOARD_PACKAGE_PATH);
	}
	keyboard_update_active = false;
	if (result != 0) {
		LOG_ERR("keyboard update: failed: %d", result);
	}
	return result;
}

static void keyboard_uart_send(const uint8_t *data, size_t length)
{
	size_t index;

	for (index = 0U; index < length; ++index) {
		uart_poll_out(keyboard_uart, data[index]);
	}
}

static int keyboard_send_command_locked(uint8_t command,
					const uint8_t *payload, size_t payload_length)
{
	uint8_t status;
	int result;

	if (!keyboard_uart_initialized || !ef800_keyboard_host_is_ready(&keyboard_host)) {
		return -ENODEV;
	}

	k_sem_reset(&keyboard_command_complete);
	keyboard_pending_command = command;
	keyboard_command_pending = true;
	if (command == CMD_KEYBOARD_VERSION) {
		LOG_INF("Keyboard version query: TX command 0x%02x", command);
	}
	keyboard_uart_send(&command, 1U);
	if (payload != NULL && payload_length != 0U) {
		keyboard_uart_send(payload, payload_length);
	}
	result = k_sem_take(&keyboard_command_complete,
		K_MSEC(KEYBOARD_COMMAND_TIMEOUT_MS));
	keyboard_command_pending = false;
	if (result != 0) {
		if (command == CMD_KEYBOARD_VERSION) {
			LOG_WRN("Keyboard version query timed out; RX response incomplete or absent");
		}
		return -ETIMEDOUT;
	}
	if (command == CMD_KEYBOARD_VERSION) {
		uint8_t version[3];

		return ef800_keyboard_host_get_version(&keyboard_host, version) ?
			0 : -EBADMSG;
	}
	status = keyboard_host.last_command_status;
	return status == CMD_ACK ? 0 : -EIO;
}

static int keyboard_send_command(uint8_t command,
				 const uint8_t *payload, size_t payload_length)
{
	int result;

	k_mutex_lock(&keyboard_command_mutex, K_FOREVER);
	result = keyboard_send_command_locked(command, payload, payload_length);
	k_mutex_unlock(&keyboard_command_mutex);
	return result;
}

static int keyboard_send_virtual_key(uint8_t action, const char *name)
{
	uint8_t packet[3U + KB_DEBUG_KEY_NAME_MAX];
	size_t length;
	int result;

	if (!keyboard_debug_active || name == NULL) {
		return -EACCES;
	}
	length = strlen(name);
	if (length == 0U || length > KB_DEBUG_KEY_NAME_MAX) {
		return -EINVAL;
	}

	packet[0] = CMD_KEYBOARD_DEBUG;
	packet[1] = action;
	packet[2] = (uint8_t)length;
	memcpy(&packet[3], name, length);

	k_mutex_lock(&keyboard_command_mutex, K_FOREVER);
	if (!keyboard_uart_initialized || !ef800_keyboard_host_is_ready(&keyboard_host)) {
		result = -ENODEV;
	} else {
		keyboard_uart_send(packet, 3U + length);
		result = 0;
	}
	k_mutex_unlock(&keyboard_command_mutex);
	return result;
}

static void display_raw_line(uint8_t row, const char *text)
{
	char line[STATUS_LINE_SIZE];
	int length;

	if (!lcd_ready) {
		return;
	}

	length = snprintf(line, sizeof(line), "%-40.40s", text == NULL ? "" : text);
	if (length < 0) {
		return;
	}
	(void)ef800_t6963_text_cursor(&lcd, 0U, row);
	(void)ef800_t6963_write_string(&lcd, line);
}

static void display_header(const char *title)
{
	struct ef800_runtime_snapshot snapshot;
	char line[LCD_COLUMNS + 1U];
	char clock_text[6];

	memset(line, ' ', sizeof(line) - 1U);
	line[sizeof(line) - 1U] = '\0';
	if (title != NULL) {
		snprintk(header_title, sizeof(header_title), "%.16s", title);
	}
	memcpy(line, header_title, strlen(header_title));
	if (runtime_state_ready &&
		ef800_runtime_state_snapshot(&runtime_state, &snapshot) == 0 &&
		snapshot.rtc_has_value) {
		snprintk(clock_text, sizeof(clock_text), "%02d:%02d",
			snapshot.local_time.tm_hour, snapshot.local_time.tm_min);
		memcpy(&line[17], clock_text, sizeof(clock_text) - 1U);
	}
	display_raw_line(0U, line);
}

static int draw_header_separator(void)
{
	return ef800_t6963_draw_horizontal_line(&lcd, 8U);
}

static void display_line(uint8_t row, const char *text)
{
	if (row == 0U) {
		display_header(text);
		return;
	}
	display_raw_line(row, text);
}

static void display_banner(void)
{
	clear_display();
	display_header("EF800");
	display_line(1U, "MainApplication");
	display_line(6U, "Keyboard startup...");
}

static void display_menu_entries(const char *title, const char *const *items,
				 const char *const *shortcuts, size_t count)
{
	char line[STATUS_LINE_SIZE];

	clear_display();

	display_line(0U, title);
	if (ef800_workspace_is_selected(&workspace)) {
		snprintk(line, sizeof(line), "Workspace: %s",
			ef800_workspace_get_name(&workspace));
	} else {
		snprintk(line, sizeof(line), "Workspace: none");
	}
	display_line(1U, line);
	display_line(2U, "");
	for (size_t index = 0U; index < 5U; ++index) {
		if (index < count) {
			snprintk(line, sizeof(line), "%c %s [%s]",
				index == menu_selection ? '>' : ' ', items[index],
				shortcuts[index]);
		} else {
			line[0] = '\0';
		}
		display_line((uint8_t)(3U + index), line);
	}
}

static void display_menu(void)
{
	display_menu_entries("Main menu", menu_items, menu_shortcuts,
				     ARRAY_SIZE(menu_items));
}

static void display_settings_menu(void)
{
	display_menu_entries("Settings", settings_items, settings_shortcuts,
				     ef800_lcd_contrast_settings_count(
					 lcd_contrast_composition_mode(),
					 ARRAY_SIZE(settings_items) - 1U));
}

static size_t settings_menu_count(void)
{
	return ef800_lcd_contrast_settings_count(lcd_contrast_composition_mode(),
		ARRAY_SIZE(settings_items) - 1U);
}

static void display_lcd_contrast_menu(void)
{
	enum ef800_lcd_contrast_mode mode;
	uint8_t duty_percent;
	char line[STATUS_LINE_SIZE];

	clear_display();
	display_line(0U, "LCD contrast");
	if (ef800_lcd_contrast_get(&lcd_contrast, &mode, &duty_percent) != 0 ||
	    mode != EF800_LCD_CONTRAST_PWM) {
		display_line(3U, "Contrast control disabled");
		display_line(7U, "LEFT back");
		return;
	}
	snprintk(line, sizeof(line), "Contrast: %u%%", duty_percent);
	display_line(3U, line);
	display_line(7U, "UP/RIGHT +1 DOWN -1 LEFT back");
}

static void adjust_lcd_contrast(int delta)
{
	enum ef800_lcd_contrast_mode mode;
	uint8_t duty_percent;
	int next;
	int ret;

	ret = ef800_lcd_contrast_get(&lcd_contrast, &mode, &duty_percent);
	if (ret != 0 || mode != EF800_LCD_CONTRAST_PWM) {
		display_line(6U, "Contrast control disabled");
		return;
	}
	next = (int)duty_percent + delta;
	if (next < (int)EF800_LCD_CONTRAST_MIN_PERCENT) {
		next = (int)EF800_LCD_CONTRAST_MIN_PERCENT;
	} else if (next > (int)EF800_LCD_CONTRAST_MAX_PERCENT) {
		next = (int)EF800_LCD_CONTRAST_MAX_PERCENT;
	}
	if ((uint8_t)next != duty_percent) {
		ret = ef800_lcd_contrast_set_duty(&lcd_contrast, (uint8_t)next);
		if (ret != 0) {
			display_line(6U, "Contrast save failed");
			return;
		}
	}
	display_lcd_contrast_menu();
}

static void display_workspace_menu(void)
{
	display_menu_entries("Workspace", workspace_items, workspace_view_shortcuts,
				     ARRAY_SIZE(workspace_items));
}

static void display_workspace_management_menu(void)
{
	display_menu_entries("Workspace management", workspace_management_items,
				     workspace_shortcuts,
				     ARRAY_SIZE(workspace_management_items));
}

static void workspace_view_reset(void)
{
	memset(workspace_view_lines, 0, sizeof(workspace_view_lines));
	memset(workspace_view_line_starts, 0, sizeof(workspace_view_line_starts));
	memset(workspace_view_line_ends, 0, sizeof(workspace_view_line_ends));
	workspace_view_line_count = 0U;
	workspace_view_start = 0U;
	workspace_view_end = 0U;
	workspace_view_file_size = 0U;
	workspace_view_selected_row = 0U;
	memset(&workspace_view_version, 0, sizeof(workspace_view_version));
	workspace_view_active = false;
	workspace_view_position_only = false;
}

static int workspace_view_set_row(size_t index,
				  const struct ef800_workspace_row *row)
{
	size_t text_length = row->text_end - row->start;
	size_t copy_length = MIN(text_length, WORKSPACE_VIEW_COLUMNS);
	size_t length = 0U;
	int result;

	if (index >= WORKSPACE_VIEW_ROWS || row->text_end < row->start) {
		return -EINVAL;
	}
	if (copy_length != 0U) {
		result = ef800_workspace_read_range(&workspace, row->start,
			row->start + copy_length, workspace_view_lines[index],
			WORKSPACE_VIEW_COLUMNS, &length);
		if (result != 0) {
			return result;
		}
		if (length != copy_length) {
			return -EIO;
		}
	}
	workspace_view_lines[index][copy_length] = '\0';
	workspace_view_line_starts[index] = row->start;
	workspace_view_line_ends[index] = row->end;
	return 0;
}

static void workspace_view_reverse_rows(void)
{
	for (size_t left = 0U; left < workspace_view_line_count / 2U; ++left) {
		size_t right = workspace_view_line_count - left - 1U;
		char line[WORKSPACE_VIEW_COLUMNS + 1U];
		size_t offset;

		memcpy(line, workspace_view_lines[left], sizeof(line));
		memcpy(workspace_view_lines[left], workspace_view_lines[right], sizeof(line));
		memcpy(workspace_view_lines[right], line, sizeof(line));
		offset = workspace_view_line_starts[left];
		workspace_view_line_starts[left] = workspace_view_line_starts[right];
		workspace_view_line_starts[right] = offset;
		offset = workspace_view_line_ends[left];
		workspace_view_line_ends[left] = workspace_view_line_ends[right];
		workspace_view_line_ends[right] = offset;
	}
}

static int workspace_view_find_last_row(struct ef800_workspace_row *row)
{
	uint8_t buffer[WORKSPACE_VIEW_SCAN_CHUNK];
	uint8_t last;
	size_t length;
	size_t search_end = workspace_view_file_size;
	int result;

	if (row == NULL || workspace_view_file_size == 0U) {
		return -ENODATA;
	}
	result = ef800_workspace_read_range(&workspace, workspace_view_file_size - 1U,
		workspace_view_file_size, &last, 1U, &length);
	if (result != 0) {
		return result;
	}
	if (length != 1U) {
		return -EIO;
	}
	if (last == '\r' || last == '\n') {
		return ef800_workspace_row_before(&workspace, workspace_view_file_size, row);
	}
	while (search_end > 0U) {
		size_t chunk_start = search_end > sizeof(buffer) ?
			search_end - sizeof(buffer) : 0U;

		result = ef800_workspace_read_range(&workspace, chunk_start, search_end,
			buffer, sizeof(buffer), &length);
		if (result != 0) {
			return result;
		}
		if (length != search_end - chunk_start) {
			return -EIO;
		}
		for (size_t index = length; index > 0U; --index) {
			if (buffer[index - 1U] == '\r' || buffer[index - 1U] == '\n') {
				return ef800_workspace_row_at(&workspace,
					chunk_start + index, row);
			}
		}
		search_end = chunk_start;
	}
	return ef800_workspace_row_at(&workspace, 0U, row);
}

static int workspace_view_add_older_rows(const struct ef800_workspace_row *last_row)
{
	struct ef800_workspace_row row = *last_row;
	int result;

	workspace_view_line_count = 0U;
	memset(workspace_view_lines, 0, sizeof(workspace_view_lines));
	memset(workspace_view_line_starts, 0, sizeof(workspace_view_line_starts));
	memset(workspace_view_line_ends, 0, sizeof(workspace_view_line_ends));
	for (size_t count = 0U; count < WORKSPACE_VIEW_ROWS; ++count) {
		result = workspace_view_set_row(workspace_view_line_count, &row);
		if (result != 0) {
			return result;
		}
		++workspace_view_line_count;
		if (row.start == 0U) {
			break;
		}
		result = ef800_workspace_row_before(&workspace, row.start, &row);
		if (result == -ENOENT) {
			break;
		}
		if (result != 0) {
			return result;
		}
	}
	if (workspace_view_line_count == 0U) {
		return -ENODATA;
	}
	workspace_view_reverse_rows();
	workspace_view_start = workspace_view_line_starts[0];
	workspace_view_end = workspace_view_line_ends[workspace_view_line_count - 1U];
	workspace_view_selected_row = workspace_view_line_count - 1U;
	return 0;
}

static int workspace_view_refresh_at(size_t anchor)
{
	struct ef800_workspace_row row;
	int result;

	result = ef800_workspace_get_version(&workspace, &workspace_view_version);
	if (result != 0) {
		return result;
	}
	result = ef800_workspace_size(&workspace, &workspace_view_file_size);
	if (result != 0) {
		return result;
	}
	workspace_view_line_count = 0U;
	workspace_view_selected_row = 0U;
	memset(workspace_view_lines, 0, sizeof(workspace_view_lines));
	memset(workspace_view_line_starts, 0, sizeof(workspace_view_line_starts));
	memset(workspace_view_line_ends, 0, sizeof(workspace_view_line_ends));
	if (workspace_view_file_size == 0U) {
		workspace_view_start = 0U;
		workspace_view_end = 0U;
		workspace_view_active = false;
		return -ENODATA;
	}
	if (anchor >= workspace_view_file_size) {
		result = workspace_view_find_last_row(&row);
	} else {
		result = ef800_workspace_row_at(&workspace, anchor, &row);
	}
	if (result != 0) {
		return result;
	}
	workspace_view_active = true;
	workspace_view_position_only = false;
	for (size_t index = 0U; index < WORKSPACE_VIEW_ROWS; ++index) {
		result = workspace_view_set_row(index, &row);
		if (result != 0) {
			return result;
		}
		++workspace_view_line_count;
		if (row.end >= workspace_view_file_size) {
			break;
		}
		result = ef800_workspace_row_at(&workspace, row.end, &row);
		if (result == -ENOENT) {
			break;
		}
		if (result != 0) {
			return result;
		}
	}
	workspace_view_start = workspace_view_line_starts[0];
	workspace_view_end = workspace_view_line_ends[workspace_view_line_count - 1U];
	return 0;
}

static int workspace_view_open_measurements(void)
{
	struct ef800_workspace_row last_row;
	int result;

	workspace_view_reset();
	if (!ef800_workspace_is_selected(&workspace)) {
		return -ENODEV;
	}
	result = ef800_workspace_get_version(&workspace, &workspace_view_version);
	if (result != 0) {
		return result;
	}
	result = ef800_workspace_size(&workspace, &workspace_view_file_size);
	if (result != 0 || workspace_view_file_size == 0U) {
		return result != 0 ? result : -ENODATA;
	}
	result = workspace_view_find_last_row(&last_row);
	if (result == 0) {
		result = workspace_view_add_older_rows(&last_row);
	}
	if (result == 0) {
		workspace_view_active = true;
	}
	return result;
}

static int workspace_view_move(int direction)
{
	struct ef800_workspace_row adjacent;
	size_t last_index;
	int result;

	if (!workspace_view_active || workspace_view_line_count == 0U ||
	    workspace_view_position_only) {
		return -EINVAL;
	}
	last_index = workspace_view_line_count - 1U;
	if (direction < 0) {
		if (workspace_view_selected_row > 0U) {
			--workspace_view_selected_row;
			return 0;
		}
		result = ef800_workspace_row_before(&workspace,
			workspace_view_line_starts[0], &adjacent);
		if (result != 0) {
			return result;
		}
		if (workspace_view_line_count < WORKSPACE_VIEW_ROWS) {
			++workspace_view_line_count;
		}
		for (size_t index = workspace_view_line_count - 1U; index > 0U; --index) {
			memcpy(workspace_view_lines[index], workspace_view_lines[index - 1U],
			       sizeof(workspace_view_lines[0]));
			workspace_view_line_starts[index] = workspace_view_line_starts[index - 1U];
			workspace_view_line_ends[index] = workspace_view_line_ends[index - 1U];
		}
		result = workspace_view_set_row(0U, &adjacent);
		if (result != 0) {
			return result;
		}
		workspace_view_selected_row = 0U;
	} else {
		if (workspace_view_selected_row < last_index) {
			++workspace_view_selected_row;
			return 0;
		}
		if (workspace_view_line_ends[last_index] >= workspace_view_file_size) {
			return -ENOENT;
		}
		result = ef800_workspace_row_at(&workspace,
			workspace_view_line_ends[last_index], &adjacent);
		if (result != 0) {
			return result;
		}
		if (workspace_view_line_count == WORKSPACE_VIEW_ROWS) {
			for (size_t index = 1U; index < WORKSPACE_VIEW_ROWS; ++index) {
				memcpy(workspace_view_lines[index - 1U],
					workspace_view_lines[index],
					sizeof(workspace_view_lines[0]));
				workspace_view_line_starts[index - 1U] =
					workspace_view_line_starts[index];
				workspace_view_line_ends[index - 1U] =
					workspace_view_line_ends[index];
			}
		} else {
			++workspace_view_line_count;
		}
		last_index = workspace_view_line_count - 1U;
		result = workspace_view_set_row(last_index, &adjacent);
		if (result != 0) {
			return result;
		}
		workspace_view_selected_row = last_index;
	}
	workspace_view_start = workspace_view_line_starts[0];
	workspace_view_end = workspace_view_line_ends[workspace_view_line_count - 1U];
	return 0;
}

static int workspace_view_find_position(void)
{
	struct ef800_workspace_row row;
	uint8_t first_character;
	char line[WORKSPACE_VIEW_COLUMNS + 1U];
	size_t offset = 0U;
	size_t file_size;
	int result;

	workspace_view_reset();
	if (!ef800_workspace_is_selected(&workspace)) {
		return -ENODEV;
	}
	result = ef800_workspace_size(&workspace, &workspace_view_file_size);
	if (result != 0) {
		return result;
	}
	file_size = workspace_view_file_size;
	while (offset < file_size) {
		size_t text_length;
		size_t copy_length;
		size_t length;

		result = ef800_workspace_row_at(&workspace, offset, &row);
		if (result != 0) {
			return result;
		}
		text_length = row.text_end - row.start;
		if (text_length != 0U) {
			result = ef800_workspace_read_range(&workspace, row.start,
				row.start + 1U, &first_character, 1U, &length);
			if (result != 0) {
				return result;
			}
			if (length != 1U) {
				return -EIO;
			}
			if (first_character == '-') {
				copy_length = MIN(text_length, WORKSPACE_VIEW_COLUMNS);
				result = ef800_workspace_read_range(&workspace, row.start,
					row.start + copy_length, line, sizeof(line), &length);
				if (result != 0) {
					return result;
				}
				if (length != copy_length) {
					return -EIO;
				}
				line[copy_length] = '\0';
				memcpy(workspace_view_lines[0], line, copy_length + 1U);
				workspace_view_line_count = 1U;
		}
		}
		if (row.end <= offset) {
			return -EIO;
		}
		offset = row.end;
	}
	workspace_view_active = true;
	workspace_view_position_only = true;
	return 0;
}

static void display_workspace_view(void)
{
	char workspace_line[STATUS_LINE_SIZE];
	char selected_line[WORKSPACE_VIEW_COLUMNS + 1U];

	clear_display();

	display_line(0U, workspace_view_position_only ? "Current Position" :
								"Show Measurements");
	if (ef800_workspace_is_selected(&workspace)) {
		snprintk(workspace_line, sizeof(workspace_line), "Workspace: %s",
				 ef800_workspace_get_name(&workspace));
		display_line(1U, workspace_line);
	} else {
		display_line(1U, "Workspace: none");
	}
	if (!workspace_view_active) {
		display_line(3U, workspace_view_file_size == 0U ?
			"No workspace data" : "Workspace unavailable");
		display_line(7U, "ESC back");
		return;
	}
	if (workspace_view_position_only) {
		display_line(3U, workspace_view_line_count == 0U ?
					"No position" : workspace_view_lines[0]);
		display_line(7U, "ESC back");
		return;
	}
	for (size_t row = 0U; row < WORKSPACE_VIEW_ROWS; ++row) {
		if (row >= workspace_view_line_count) {
			display_line((uint8_t)(3U + row), "");
			continue;
		}
		selected_line[0] = row == workspace_view_selected_row ? '>' : ' ';
		selected_line[1] = ' ';
		strncpy(&selected_line[2], workspace_view_lines[row],
			WORKSPACE_VIEW_COLUMNS - 2U);
		selected_line[WORKSPACE_VIEW_COLUMNS] = '\0';
		display_line((uint8_t)(3U + row), selected_line);
	}
	display_line(7U, "UP/DOWN select ENTER actions");
}

static void display_workspace_row_actions(void)
{
	char line[WORKSPACE_VIEW_COLUMNS + 1U];

	display_menu_entries("Workspace row", workspace_row_action_items,
		workspace_row_action_shortcuts, ARRAY_SIZE(workspace_row_action_items));
	if (workspace_view_selected_row < workspace_view_line_count) {
		snprintk(line, sizeof(line), "%.*s", (int)sizeof(line) - 1,
			workspace_view_lines[workspace_view_selected_row]);
		display_line(2U, line);
	}
	display_line(7U, "ENTER select ESC back");
}

static void display_workspace_row_delete_confirmation(void)
{
	char line[WORKSPACE_VIEW_COLUMNS + 1U];

	clear_display();
	display_line(0U, "Delete selected row?");
	if (workspace_view_selected_row < workspace_view_line_count) {
		snprintk(line, sizeof(line), "%.*s",
			(int)sizeof(line) - 1,
			workspace_view_lines[workspace_view_selected_row]);
		display_line(2U, line);
	}
	display_line(5U, "ENTER confirm delete");
	display_line(6U, "ESC cancel");
}

static void display_workspace_row_editor(void)
{
	char mode[WORKSPACE_VIEW_COLUMNS + 1U];
	char text_line[WORKSPACE_VIEW_COLUMNS + 1U];
	char caret_line[WORKSPACE_VIEW_COLUMNS + 1U];
	size_t view_width = WORKSPACE_VIEW_COLUMNS - 1U;
	size_t caret_column;
	size_t visible_length;

	if (workspace_row_editor.caret < workspace_row_editor.viewport_start) {
		workspace_row_editor.viewport_start = workspace_row_editor.caret;
	} else if (workspace_row_editor.caret >
		   workspace_row_editor.viewport_start + view_width) {
		workspace_row_editor.viewport_start =
			workspace_row_editor.caret - view_width;
	}
	clear_display();
	display_line(0U, "Modify workspace row");
	snprintk(mode, sizeof(mode), "Workspace: %s",
		ef800_workspace_get_name(&workspace));
	display_line(1U, mode);
	display_line(2U, workspace_row_editor.insert_mode ?
		"INSERT (INS toggles)" : "OVERWRITE (INS toggles)");
	visible_length = workspace_row_editor.length > workspace_row_editor.viewport_start ?
		MIN(view_width, workspace_row_editor.length -
			workspace_row_editor.viewport_start) : 0U;
	if (visible_length != 0U) {
		memcpy(text_line, &workspace_row_editor.text[
			workspace_row_editor.viewport_start], visible_length);
	}
	text_line[visible_length] = '\0';
	display_line(3U, text_line);
	memset(caret_line, ' ', view_width);
	caret_column = workspace_row_editor.caret -
		workspace_row_editor.viewport_start;
	if (caret_column > view_width) {
		caret_column = view_width;
	}
	caret_line[caret_column] = '^';
	caret_line[view_width] = '\0';
	display_line(4U, caret_line);
	display_line(6U, "BACKSPACE/DEL remove");
	display_line(7U, "ENTER save  ESC cancel");
}

static void workspace_row_editor_cancel(void)
{
	memset(&workspace_row_editor, 0, sizeof(workspace_row_editor));
	active_menu = APPLICATION_MENU_WORKSPACE_ROW_ACTIONS;
	display_workspace_row_actions();
}

static void workspace_row_editor_start(void)
{
	struct ef800_workspace_row row;
	struct ef800_workspace_version current_version;
	size_t length;
	int result;

	if (workspace_view_selected_row >= workspace_view_line_count) {
		return;
	}
	result = ef800_workspace_get_version(&workspace, &current_version);
	if (result != 0 || current_version.size != workspace_view_version.size ||
	    current_version.fingerprint != workspace_view_version.fingerprint) {
		active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
		result = workspace_view_open_measurements();
		display_workspace_view();
		display_line(6U, "Workspace changed; reloaded");
		return;
	}
	result = ef800_workspace_row_at(&workspace,
		workspace_view_line_starts[workspace_view_selected_row], &row);
	if (result != 0 || row.end !=
	    workspace_view_line_ends[workspace_view_selected_row]) {
		active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
		(void)workspace_view_open_measurements();
		display_workspace_view();
		display_line(6U, "Workspace changed; reloaded");
		return;
	}
	length = row.text_end - row.start;
	if (length > WORKSPACE_ROW_EDIT_MAX) {
		active_menu = APPLICATION_MENU_WORKSPACE_ROW_ACTIONS;
		display_workspace_row_actions();
		display_line(6U, "Row too long to modify");
		return;
	}
	memset(&workspace_row_editor, 0, sizeof(workspace_row_editor));
	workspace_row_editor.row = row;
	workspace_row_editor.length = length;
	workspace_row_editor.caret = length;
	workspace_row_editor.insert_mode = true;
	result = ef800_workspace_read_range(&workspace, row.start, row.text_end,
		workspace_row_editor.text, WORKSPACE_ROW_EDIT_MAX, &length);
	if (result != 0 || length != workspace_row_editor.length ||
	    memchr(workspace_row_editor.text, '\0', length) != NULL) {
		memset(&workspace_row_editor, 0, sizeof(workspace_row_editor));
		active_menu = APPLICATION_MENU_WORKSPACE_ROW_ACTIONS;
		display_workspace_row_actions();
		display_line(6U, "Row cannot be edited");
		return;
	}
	workspace_row_editor.text[workspace_row_editor.length] = '\0';
	active_menu = APPLICATION_MENU_WORKSPACE_ROW_EDITOR;
	display_workspace_row_editor();
}

static void workspace_row_editor_save(void)
{
	size_t anchor = workspace_row_editor.row.start;
	int result = ef800_workspace_replace_row(&workspace, &workspace_view_version,
		&workspace_row_editor.row, workspace_row_editor.text,
		workspace_row_editor.length, false);

	if (result == 0) {
		memset(&workspace_row_editor, 0, sizeof(workspace_row_editor));
		active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
		result = workspace_view_refresh_at(anchor);
		display_workspace_view();
		if (result != 0) {
			display_line(6U, "Saved; viewer refresh failed");
		}
	} else if (result == -EAGAIN) {
		memset(&workspace_row_editor, 0, sizeof(workspace_row_editor));
		active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
		(void)workspace_view_open_measurements();
		display_workspace_view();
		display_line(6U, "Workspace changed; reloaded");
	} else {
		display_workspace_row_editor();
		display_line(6U, "Save failed; ENTER retry");
	}
}

static void workspace_row_editor_key(uint8_t key)
{
	if (key == 0x1BU) {
		workspace_row_editor_cancel();
		return;
	}
	if (key == '\r' || key == '\n') {
		workspace_row_editor_save();
		return;
	}
	if (key == 0x08U) {
		if (workspace_row_editor.caret > 0U) {
			memmove(&workspace_row_editor.text[workspace_row_editor.caret - 1U],
				&workspace_row_editor.text[workspace_row_editor.caret],
				workspace_row_editor.length - workspace_row_editor.caret + 1U);
			--workspace_row_editor.caret;
			--workspace_row_editor.length;
		}
		display_workspace_row_editor();
		return;
	}
	if (key == 0x7FU) {
		if (workspace_row_editor.caret < workspace_row_editor.length) {
			memmove(&workspace_row_editor.text[workspace_row_editor.caret],
				&workspace_row_editor.text[workspace_row_editor.caret + 1U],
				workspace_row_editor.length - workspace_row_editor.caret);
			--workspace_row_editor.length;
		}
		display_workspace_row_editor();
		return;
	}
	if (key < 0x20U || key >= 0x7FU) {
		return;
	}
	if (workspace_row_editor.insert_mode) {
		if (workspace_row_editor.length >= WORKSPACE_ROW_EDIT_MAX) {
			display_line(6U, "Editor limit 255 bytes");
			return;
		}
		memmove(&workspace_row_editor.text[workspace_row_editor.caret + 1U],
			&workspace_row_editor.text[workspace_row_editor.caret],
			workspace_row_editor.length - workspace_row_editor.caret + 1U);
		workspace_row_editor.text[workspace_row_editor.caret++] = (char)key;
		++workspace_row_editor.length;
	} else if (workspace_row_editor.caret < workspace_row_editor.length) {
		workspace_row_editor.text[workspace_row_editor.caret++] = (char)key;
	} else if (workspace_row_editor.length < WORKSPACE_ROW_EDIT_MAX) {
		workspace_row_editor.text[workspace_row_editor.length++] = (char)key;
		workspace_row_editor.text[workspace_row_editor.length] = '\0';
		workspace_row_editor.caret = workspace_row_editor.length;
	} else {
		display_line(6U, "Editor limit 255 bytes");
		return;
	}
	display_workspace_row_editor();
}

static void workspace_view_open_row_actions(void)
{
	if (!workspace_view_active || workspace_view_position_only ||
	    workspace_view_line_count == 0U) {
		return;
	}
	active_menu = APPLICATION_MENU_WORKSPACE_ROW_ACTIONS;
	menu_selection = 0U;
	display_workspace_row_actions();
}

static void workspace_view_delete_selected_row(void)
{
	struct ef800_workspace_row row;
	char status[STATUS_LINE_SIZE];
	size_t anchor = 0U;
	int result;

	if (workspace_view_selected_row >= workspace_view_line_count) {
		result = -EINVAL;
	} else {
		anchor = workspace_view_line_starts[workspace_view_selected_row];
		result = ef800_workspace_row_at(&workspace,
			anchor, &row);
		if (result == 0 && row.end !=
		    workspace_view_line_ends[workspace_view_selected_row]) {
			result = -EAGAIN;
		}
		if (result == 0) {
			result = ef800_workspace_replace_row(&workspace,
				&workspace_view_version, &row, NULL, 0U, true);
		}
	}
	if (result == 0) {
		active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
		result = workspace_view_refresh_at(anchor);
		display_workspace_view();
		if (result != 0 && result != -ENODATA) {
			display_line(7U, "Deleted; viewer refresh failed");
		}
		return;
	}
	if (result == -EAGAIN) {
		active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
		(void)workspace_view_open_measurements();
		display_workspace_view();
		display_line(7U, "Workspace changed; reloaded");
		return;
	}
	active_menu = APPLICATION_MENU_WORKSPACE_ROW_DELETE_CONFIRMATION;
	display_workspace_row_delete_confirmation();
	snprintk(status, sizeof(status), "Delete failed (%d)", result);
	display_line(4U, status);
}

static void display_port_assignment_ports(void)
{
	char line[STATUS_LINE_SIZE];
	size_t first = menu_selection >= 3U ? menu_selection - 3U : 0U;

	clear_display();

	display_line(0U, "Port assignment");
	snprintk(line, sizeof(line), "Ports %u-%u of %u", (unsigned)(first + 1U),
		(unsigned)MIN(first + 4U, EF800_PORT_COUNT), (unsigned)EF800_PORT_COUNT);
	display_line(1U, line);
	for (size_t row = 0U; row < 4U; ++row) {
		size_t index = first + row;
		const struct ef800_port_descriptor *port = ef800_port_at(index);
		const struct ef800_port_device_descriptor *device = NULL;
		bool plugin_assigned = false;
		const struct ef800_port_connection_descriptor *connection = NULL;
		uint32_t plugin_id = EF800_PORT_BUILDER_NONE;

		if (port == NULL) {
			display_line((uint8_t)(3U + row), "");
			continue;
		}
		if (ef800_port_assignment_is_assigned(&port_assignments, port->id)) {
			plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments,
				port->id);
			if (ef800_port_assignment_get_builder(&port_assignments, port->id) ==
				EF800_PORT_BUILDER_NONE) {
				plugin_assigned = plugin_id != EF800_PORT_BUILDER_NONE;
			} else {
				device = ef800_port_device_get(
					ef800_port_assignment_get_builder(&port_assignments, port->id));
			}
			for (size_t connection_index = 0U;
				 connection_index < ef800_port_connection_count(port->id);
				 ++connection_index) {
				const struct ef800_port_connection_descriptor *candidate =
					ef800_port_connection_at(port->id, connection_index);
				if (candidate != NULL && candidate->capability ==
					ef800_port_assignment_get_connection(&port_assignments,
																	 port->id)) {
					connection = candidate;
					break;
				}
			}
		}
		snprintk(line, sizeof(line), "%c %c %s: %s%s%s%s",
			index == menu_selection ? '>' : ' ', port->shortcut, port->title,
			device != NULL ? device->title :
			plugin_assigned ? measurement_device_title(port->id) : "none",
			connection == NULL ? "" : " / ",
			connection == NULL ? "" : connection->name,
			device != NULL && ef800_port_uart_can_claim(
				&port_uart_registry, port->id,
				device->builder_id,
				ef800_port_assignment_get_connection(&port_assignments,
								     port->id)) != 0 ?
				" (UART unavailable)" : "");
		display_line((uint8_t)(3U + row), line);
	}
	display_line(7U, "ENTER edit  ESC back");
}

static size_t compatible_device_count(const struct ef800_port_descriptor *port)
{
	size_t count = 0U;

	if (port == NULL) {
		return 0U;
	}
	for (size_t index = 0U; index < ef800_port_device_count(); ++index) {
		const struct ef800_port_device_descriptor *device =
			ef800_port_device_at(index);

		if (ef800_port_device_is_compatible(port, device) &&
			(device->builder_id != EF800_PORT_BUILDER_MSD ||
			 usb_msd_composition_enabled())) {
			++count;
		}
	}
	return count;
}

static bool plugin_hardware_matches_port(
	const struct ef800_plugin_registry_entry *plugin,
	const struct ef800_port_descriptor *port)
{
	return plugin != NULL && port != NULL &&
		plugin->info.required_hardware != 0U &&
		(port->capabilities & plugin->info.required_hardware) ==
		plugin->info.required_hardware;
}

static bool selected_plugin_supports_connection(uint32_t connection)
{
	const struct ef800_plugin_registry_entry *plugin =
		ef800_plugin_registry_find(&plugin_registry, selected_plugin_id);
	uint32_t hardware;

	if (plugin == NULL) {
		return false;
	}
	switch (connection) {
	case EF800_PORT_CONNECTION_SERIAL:
		hardware = EF800_PLUGIN_HARDWARE_SERIAL;
		break;
	case EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL:
		hardware = EF800_PLUGIN_HARDWARE_SERIAL_FLOW_CONTROL;
		break;
	default:
		return false;
	}
	return (plugin->info.required_hardware & hardware) == hardware;
}

static size_t port_assignment_device_count(void)
{
	const struct ef800_port_descriptor *port =
		ef800_port_at(selected_port_index);

	size_t count = compatible_device_count(port) +
		(port != NULL && !port->fixed ? 1U : 0U);
	for (size_t index = 0U; index < ef800_plugin_registry_count(&plugin_registry);
		++index) {
		const struct ef800_plugin_registry_entry *plugin =
			ef800_plugin_registry_at(&plugin_registry, index);
		if (plugin != NULL && plugin->state == EF800_PLUGIN_REGISTRY_READY &&
			plugin->info.kind == EF800_PLUGIN_KIND_MEASUREMENT &&
			plugin_hardware_matches_port(plugin, port)) {
			++count;
		}
	}
	return count;
}

static const struct ef800_port_device_descriptor *device_at_selection(
	const struct ef800_port_descriptor *port, size_t selection)
{
	size_t compatible_index = 0U;

	if (port == NULL) {
		return NULL;
	}
	for (size_t index = 0U; index < ef800_port_device_count(); ++index) {
		const struct ef800_port_device_descriptor *device =
			ef800_port_device_at(index);
		if (ef800_port_device_is_compatible(port, device) &&
			(device->builder_id != EF800_PORT_BUILDER_MSD ||
			 usb_msd_composition_enabled())) {
			if (compatible_index++ == selection) {
				return device;
			}
		}
	}
	return NULL;
}

static const struct ef800_plugin_registry_entry *plugin_at_selection(
	const struct ef800_port_descriptor *port, size_t selection)
{
	size_t builtins = compatible_device_count(port);

	if (port == NULL || selection < builtins) {
		return NULL;
	}
	selection -= builtins;
	for (size_t index = 0U; index < ef800_plugin_registry_count(&plugin_registry);
		++index) {
		const struct ef800_plugin_registry_entry *plugin =
			ef800_plugin_registry_at(&plugin_registry, index);
		if (plugin != NULL && plugin->state == EF800_PLUGIN_REGISTRY_READY &&
			plugin->info.kind == EF800_PLUGIN_KIND_MEASUREMENT &&
			plugin_hardware_matches_port(plugin, port) &&
			selection-- == 0U) {
			return plugin;
		}
	}
	return NULL;
}

static void display_port_assignment_devices(void)
{
	const struct ef800_port_descriptor *port = ef800_port_at(selected_port_index);
	const struct ef800_port_device_descriptor *current;
	bool current_plugin_assigned = false;
	char line[STATUS_LINE_SIZE];
	size_t row = 3U;
	size_t device_index = 0U;
	uint32_t current_plugin_id;

	if (port == NULL) {
		return;
	}
	clear_display();
	display_line(0U, "Port assignment");
	snprintk(line, sizeof(line), "Port %u: %s", port->id, port->title);
	display_line(1U, line);
	current_plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments,
		port->id);
	if (ef800_port_assignment_get_builder(&port_assignments, port->id) ==
		EF800_PORT_BUILDER_NONE) {
		current = NULL;
		current_plugin_assigned = current_plugin_id != EF800_PORT_BUILDER_NONE;
	} else {
		current = ef800_port_device_get(ef800_port_assignment_get_builder(
			&port_assignments, port->id));
	}
	if (!ef800_port_assignment_is_assigned(&port_assignments, port->id)) {
		current = NULL;
		current_plugin_assigned = false;
	}
		snprintk(line, sizeof(line), "Current: %s%s",
		current != NULL ? current->title :
		current_plugin_assigned ? measurement_device_title(port->id) : "none",
		port->fixed ? " (fixed)" : "");
	display_line(2U, line);
	for (size_t index = 0U; index < ef800_port_device_count(); ++index) {
		const struct ef800_port_device_descriptor *device =
			ef800_port_device_at(index);
		if (!ef800_port_device_is_compatible(port, device) ||
			(device->builder_id == EF800_PORT_BUILDER_MSD &&
			 !usb_msd_composition_enabled())) {
			continue;
		}
		snprintk(line, sizeof(line), "%c %s [%c]",
			device_index == menu_selection ? '>' : ' ', device->title,
			device->shortcut);
		display_line((uint8_t)row++, line);
		++device_index;
	}
	for (size_t index = 0U; index < ef800_plugin_registry_count(&plugin_registry); ++index) {
		const struct ef800_plugin_registry_entry *plugin =
			ef800_plugin_registry_at(&plugin_registry, index);
		if (plugin == NULL || plugin->info.kind != EF800_PLUGIN_KIND_MEASUREMENT ||
			plugin->state != EF800_PLUGIN_REGISTRY_READY ||
			!plugin_hardware_matches_port(plugin, port)) {
			continue;
		}
		snprintk(line, sizeof(line), "%c %s [%u]",
			device_index == menu_selection ? '>' : ' ', plugin->info.name,
			(unsigned)plugin->info.plugin_id);
		display_line((uint8_t)row++, line);
		++device_index;
	}
	if (!port->fixed) {
		snprintk(line, sizeof(line), "%c Remove assignment [r]",
			menu_selection == device_index ? '>' : ' ');
		display_line((uint8_t)row, line);
	} else {
		display_line((uint8_t)row, "Fixed assignment");
	}
	display_line(7U, "ENTER select  ESC back");
}

static size_t port_assignment_connection_count(void)
{
	const struct ef800_port_descriptor *port = ef800_port_at(selected_port_index);
	size_t count = 0U;

	if (port == NULL) {
		return 0U;
	}
	for (size_t index = 0U; index < ef800_port_connection_count(port->id); ++index) {
		const struct ef800_port_connection_descriptor *connection =
			ef800_port_connection_at(port->id, index);
		if (connection != NULL &&
			(port->id != 4U ||
			 connection->capability != EF800_PORT_CONNECTION_USB_MSD ||
			 usb_msd_composition_enabled()) && (selected_plugin_id != 0U ?
			selected_plugin_supports_connection(connection->capability) :
			ef800_port_device_supports_connection(selected_builder_id,
				connection->capability))) {
			++count;
		}
	}
	return count;
}

static const struct ef800_port_connection_descriptor *connection_at_selection(
	size_t selection)
{
	const struct ef800_port_descriptor *port = ef800_port_at(selected_port_index);
	size_t compatible_index = 0U;

	if (port == NULL) {
		return NULL;
	}
	for (size_t index = 0U; index < ef800_port_connection_count(port->id); ++index) {
		const struct ef800_port_connection_descriptor *connection =
			ef800_port_connection_at(port->id, index);
		if (connection != NULL &&
			(port->id != 4U ||
			 connection->capability != EF800_PORT_CONNECTION_USB_MSD ||
			 usb_msd_composition_enabled()) && (selected_plugin_id != 0U ?
			selected_plugin_supports_connection(connection->capability) :
			ef800_port_device_supports_connection(selected_builder_id,
				connection->capability)) &&
			compatible_index++ == selection) {
			return connection;
		}
	}
	return NULL;
}

static void display_port_assignment_connections(void)
{
	const struct ef800_port_descriptor *port = ef800_port_at(selected_port_index);
	const struct ef800_port_connection_descriptor *selected_connection;
	char line[STATUS_LINE_SIZE];
	size_t row = 3U;
	size_t connection_index = 0U;
	size_t first;
	size_t count;

	if (port == NULL) {
		return;
	}
	clear_display();
	display_line(0U, "Port connection");
	snprintk(line, sizeof(line), "Port %u: %s", port->id, port->title);
	display_line(1U, line);
	snprintk(line, sizeof(line), "Device: %s", selected_plugin_id != 0U ?
		(ef800_plugin_registry_find(&plugin_registry, selected_plugin_id)->info.name) :
		ef800_port_device_get(selected_builder_id)->title);
	display_line(2U, line);
	count = port_assignment_connection_count();
	selected_connection = connection_at_selection(menu_selection);
	first = menu_selection >= 3U ? menu_selection - 3U : 0U;
	for (size_t index = 0U; index < ef800_port_connection_count(port->id); ++index) {
		const struct ef800_port_connection_descriptor *connection =
			ef800_port_connection_at(port->id, index);
		if (connection == NULL ||
			(port->id == 4U &&
			 connection->capability == EF800_PORT_CONNECTION_USB_MSD &&
			 !usb_msd_composition_enabled()) ||
			!ef800_port_device_supports_connection(
			selected_builder_id, connection->capability)) {
			continue;
		}
		if (connection_index >= first && row < 7U) {
			snprintk(line, sizeof(line), "%c %s",
				connection_index == menu_selection ? '>' : ' ', connection->name);
			display_line((uint8_t)row++, line);
		}
		++connection_index;
	}
	snprintk(line, sizeof(line), "Connections %u-%u of %u",
		(unsigned)(first + 1U), (unsigned)MIN(first + 4U, count),
		(unsigned)count);
	display_line(6U, line);
	snprintk(line, sizeof(line), "ENTER assign: %s  ESC back",
		selected_connection == NULL ? "none" : selected_connection->name);
	display_line(7U, line);
}

static void display_port_debug_confirmation(void)
{
	char line[STATUS_LINE_SIZE];
	const struct ef800_port_descriptor *port = ef800_port_at(selected_port_index);

	if (port == NULL) {
		return;
	}
	clear_display();
	snprintk(line, sizeof(line), "Assign DebugDevice to Port %u?", port->id);
	display_line(2U, line);
	display_line(4U, "Yes: Enter");
	display_line(5U, "No: Esc");
}

static const char *measurement_device_title(uint8_t port_id)
{
	uint32_t plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments, port_id);
	if (!ef800_port_assignment_is_assigned(&port_assignments, port_id) ||
		plugin_id == EF800_PORT_BUILDER_NONE) {
		return "none";
	}
	if (assigned_plugin_names[port_id - 1U][0] == '\0') {
		const struct ef800_plugin_registry_entry *entry =
			ef800_plugin_registry_find(&plugin_registry, plugin_id);
		if (entry != NULL) {
			strncpy(assigned_plugin_names[port_id - 1U], entry->info.name,
				sizeof(assigned_plugin_names[0]) - 1U);
			assigned_plugin_names[port_id - 1U][sizeof(assigned_plugin_names[0]) - 1U] = '\0';
		}
	}
	return assigned_plugin_names[port_id - 1U][0] == '\0' ?
		"assigned plugin" : assigned_plugin_names[port_id - 1U];
}

static int load_measurement_ports(void)
{
	measurement_port_count = 0U;
	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		uint32_t plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments, port_id);

		if (ef800_plugin_registry_find(&plugin_registry, plugin_id) != NULL &&
			ef800_plugin_registry_state_for(&plugin_registry, plugin_id) ==
			EF800_PLUGIN_REGISTRY_READY &&
			measurement_transports[port_id - 1U].uart != NULL) {
			measurement_port_ids[measurement_port_count++] = port_id;
		} else if (ef800_port_assignment_is_assigned(&port_assignments, port_id) &&
			plugin_id != EF800_PORT_BUILDER_NONE) {
			LOG_WRN("Measurement assignment not ready: port=%u plugin=%u registry=%u transport=%u",
				port_id, plugin_id,
				ef800_plugin_registry_state_for(&plugin_registry, plugin_id),
				measurement_transports[port_id - 1U].uart != NULL);
		}
	}
	return measurement_port_count == 0U ? -ENODEV : 0;
}

static void display_measurement_devices(void)
{
	char line[STATUS_LINE_SIZE];
	size_t first = menu_selection >= 3U ? menu_selection - 3U : 0U;

	clear_display();
	display_line(0U, "Measurement device");
	display_line(1U, "Select assigned device");
	for (size_t row = 0U; row < 4U; ++row) {
		size_t index = first + row;

		if (index >= measurement_port_count) {
			display_line((uint8_t)(3U + row), "");
			continue;
		}
		snprintk(line, sizeof(line), "%c %s",
			index == menu_selection ? '>' : ' ',
			measurement_device_title(measurement_port_ids[index]));
		display_line((uint8_t)(3U + row), line);
	}
	display_line(7U, "ENTER select  ESC back");
}

static void display_measurement_actions(void)
{
	static const char *const actions[] = {
		"Trigger measurement", "Save measurement", "Discard measurement",
		"Fixed measurement", "Device up", "Device down", "Device enter",
	};
	char line[STATUS_LINE_SIZE];
	size_t first = menu_selection >= 3U ? menu_selection - 3U : 0U;

	clear_display();
	display_line(0U, "Measurement");
	snprintk(line, sizeof(line), "%s",
		measurement_device_title(selected_measurement_port));
	display_line(1U, line);
	if (measurement_pending) {
		snprintk(line, sizeof(line), "No %ld H %ld V %ld D %ldmm",
			(long)measurement_record.number,
			(long)measurement_record.data.horizontal_gon_1e4,
			(long)measurement_record.data.vertical_gon_1e4,
			(long)measurement_record.data.distance_mm);
		display_line(2U, line);
	} else {
		display_line(2U, "No pending measurement");
	}
	for (size_t row = 0U; row < 4U; ++row) {
		size_t index = first + row;
		if (index < ARRAY_SIZE(actions)) {
			snprintk(line, sizeof(line), "%c %s",
				index == menu_selection ? '>' : ' ', actions[index]);
		} else {
			line[0] = '\0';
		}
		display_line((uint8_t)(3U + row), line);
	}
	display_line(7U, "ENTER select  ESC back");
}

static bool selected_measurement_has_submenu(void)
{
	uint32_t plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments,
		selected_measurement_port);
	const struct ef800_plugin_registry_entry *entry =
		ef800_plugin_registry_find(&plugin_registry, plugin_id);

	return entry != NULL && entry->state == EF800_PLUGIN_REGISTRY_READY &&
		entry->info.kind == EF800_PLUGIN_KIND_MEASUREMENT &&
		entry->info.measurement_actions != 0U;
}

static size_t measurement_plugin_action_count(void)
{
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (!plugin_display_valid) {
		return 0U;
	}
	uint32_t actions = plugin_display_cache.actions;
#else
	struct ef800_measurement_display display;

	if (!selected_measurement_has_submenu() ||
		ef800_plugin_host_measurement_session_render(
		&measurement_plugin_session, &display) != 0) {
		return 0U;
	}
	uint32_t actions = display.actions;
#endif
	size_t count = 0U;
	for (uint32_t action = 1U; action != 0U; action <<= 1U) {
		if ((actions & action) != 0U) {
			++count;
		}
	}
	return count;
}

static void close_measurement_plugin_session(void)
{
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (measurement_plugin_session.state != EF800_MEASUREMENT_SESSION_CLOSED) {
		const struct ef800_plugin_async_request request = {
			.operation = EF800_PLUGIN_ASYNC_SESSION_CLOSE,
			.session = &measurement_plugin_session,
		};
		if (submit_plugin_async(&request) != 0) {
			LOG_WRN("Unable to queue plugin session close");
		}
	}
#else
	ef800_plugin_host_measurement_session_close(&measurement_plugin_session);
#endif
}

#if !CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static void append_plugin_workspace_record(void)
{
	char record[256];
	size_t length;
	int result = ef800_plugin_host_measurement_session_workspace_record(
		&measurement_plugin_session, record, sizeof(record), &length);

	if (result == -ENODATA) {
		return;
	}
	if (result != 0) {
		display_line(6U, "Plugin record unavailable");
		return;
	}
	result = ef800_workspace_append(&workspace, record, length);
	(void)ef800_plugin_host_measurement_session_workspace_result(
		&measurement_plugin_session, result);
	display_line(6U, result == 0 ? "Measurement saved" : "Save failed");
}
#endif

static void display_measurement_plugin_menu(void)
{
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	struct ef800_measurement_display *display = &plugin_display_cache;
	char line[STATUS_LINE_SIZE];

	clear_display();
	if (!plugin_display_valid) {
		display_line(0U, "Measurement plugin");
		display_line(6U, plugin_async_ui_pending ? "Plugin working..." :
			"Plugin display unavailable");
		return;
	}
#else
	struct ef800_measurement_display display;
	char line[STATUS_LINE_SIZE];

	clear_display();
	if (ef800_plugin_host_measurement_session_render(
		&measurement_plugin_session, &display) != 0) {
		display_line(6U, "Plugin display unavailable");
		return;
	}
#endif
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	display_line(0U, display->title);
#else
	display_line(0U, display.title);
#endif
	snprintk(line, sizeof(line), "Port %u: %s", selected_measurement_port,
		measurement_device_title(selected_measurement_port));
	display_line(1U, line);
	display_line(2U, "Select action");
	for (size_t index = 0U;
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		index < display->row_count && index < 4U;
#else
		index < display.row_count && index < 4U;
#endif
		++index) {
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		snprintk(line, sizeof(line), "%c %s", index == display->selected_action ? '>' : ' ',
			display->rows[index]);
#else
		snprintk(line, sizeof(line), "%c %s", index == display.selected_action ? '>' : ' ',
			display.rows[index]);
#endif
		display_line((uint8_t)(3U + index), line);
	}
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	display_line(7U, display->footer);
#else
	display_line(7U, display.footer);
#endif
}

static void display_measurement_editor(void)
{
	char line[STATUS_LINE_SIZE];

	clear_display();
	display_line(0U, "Measurement details");
	snprintk(line, sizeof(line), "No %ld H %.4ld V %.4ld",
		(long)measurement_record.number,
		(long)measurement_record.data.horizontal_gon_1e4,
		(long)measurement_record.data.vertical_gon_1e4);
	display_line(1U, line);
	snprintk(line, sizeof(line), "%s", measurement_editor.field ==
		MEASUREMENT_EDITOR_CODE ? "Code" : "Staff height mm");
	display_line(4U, line);
	snprintk(line, sizeof(line), "> %s", measurement_editor.input);
	display_line(5U, line);
	display_line(7U, "ENTER next  ESC cancel");
}

static void start_measurement_editor(void)
{
	memset(&measurement_editor, 0, sizeof(measurement_editor));
	measurement_editor.active = true;
	measurement_editor.field = MEASUREMENT_EDITOR_CODE;
	strncpy(measurement_editor.input, measurement_code,
		sizeof(measurement_editor.input) - 1U);
	measurement_editor.length = strlen(measurement_editor.input);
	display_measurement_editor();
}

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static int recover_plugin_uart_route(void *context,
	const struct ef800_plugin_recovery_ledger *ledger)
{
	const struct ef800_port_uart_route *route;
	const struct device *device;
	struct ef800_measurement_serial_config config;
	uint8_t port_id;
	int ret;

	ARG_UNUSED(context);
	if (ledger == NULL || !ledger->route_claimed) {
		return 0;
	}
	port_id = ledger->route_port_id;
	if (port_id == 0U || port_id > EF800_PORT_COUNT ||
		ledger->route_transport !=
			&measurement_transports[port_id - 1U].transport ||
		!ef800_port_assignment_is_assigned(&port_assignments, port_id) ||
		ef800_port_assignment_get_plugin_id(&port_assignments, port_id) !=
			ledger->plugin_id ||
		ef800_port_assignment_get_connection(&port_assignments, port_id) !=
			ledger->route_connection) {
		return -ESTALE;
	}
	ret = ef800_plugin_host_serial_config(ledger->plugin_id, NULL, &config);
	if (ret != 0) {
		return ret;
	}
	route = ef800_port_uart_for_connection(port_id, ledger->route_connection);
	device = route == NULL ? NULL : port_uart_device(route->uart_id);
	if (device == NULL) {
		return -ENODEV;
	}
	ret = ef800_measurement_zephyr_init_with_config(
		&measurement_transports[port_id - 1U], device, &config);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_port_uart_release(&port_uart_registry, port_id,
		uart_owner_for_plugin(ledger->plugin_id), ledger->route_connection);
	if (ret == 0) {
		ef800_plugin_host_recovery_route_released(port_id,
			ledger->route_connection);
	}
	return ret;
}

static void set_plugin_async_route(
	struct ef800_plugin_async_request *request, uint8_t port_id,
	uint32_t plugin_id)
{
	if (request == NULL || port_id == 0U || port_id > EF800_PORT_COUNT) {
		return;
	}
	request->route_port_id = port_id;
	request->route_connection = ef800_port_assignment_get_connection(
		&port_assignments, port_id);
	request->route_claimed =
		ef800_port_assignment_is_assigned(&port_assignments, port_id) &&
		ef800_port_assignment_get_plugin_id(&port_assignments, port_id) ==
			plugin_id &&
		ef800_port_connection_is_valid(port_id,
			request->route_connection) &&
		measurement_transports[port_id - 1U].uart != NULL &&
		ef800_port_uart_is_claimed(&port_uart_registry, port_id);
}
#endif

static int trigger_measurement(bool fixed)
{
	uint32_t plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments,
		selected_measurement_port);
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	struct ef800_plugin_async_request request = {
		.operation = EF800_PLUGIN_ASYNC_MEASURE,
		.plugin_id = plugin_id,
		.transport = &measurement_transports[selected_measurement_port - 1U].transport,
	};
	set_plugin_async_route(&request, selected_measurement_port, plugin_id);
	plugin_measurement_fixed = fixed;
	return submit_plugin_async(&request) == 0 ? -EINPROGRESS : -EBUSY;
#else
	struct ef800_measurement_data data;
	int result;

	result = ef800_plugin_host_measure(plugin_id,
			&measurement_transports[selected_measurement_port - 1U].transport,
			&data);
	if (result != 0) {
		return result;
	}
	if (measurement_number == INT32_MAX) {
		return -ERANGE;
	}
	measurement_record.number = ++measurement_number;
	measurement_record.data = data;
	measurement_record.staff_height_mm = 0;
	strncpy(measurement_record.code, measurement_code,
		sizeof(measurement_record.code) - 1U);
	measurement_record.code[sizeof(measurement_record.code) - 1U] = '\0';
	measurement_pending = true;
	measurement_fixed = fixed;
	return 0;
#endif
}

static int send_measurement_input(uint32_t input)
{
	uint32_t plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments,
		selected_measurement_port);
	const struct ef800_measurement_transport *transport =
		&measurement_transports[selected_measurement_port - 1U].transport;

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	struct ef800_plugin_async_request request = {
		.operation = EF800_PLUGIN_ASYNC_INPUT,
		.plugin_id = plugin_id,
		.transport = transport,
		.input = input,
	};
	set_plugin_async_route(&request, selected_measurement_port, plugin_id);
	return submit_plugin_async(&request) == 0 ? -EINPROGRESS : -EBUSY;
#else
	return ef800_plugin_host_input(plugin_id, transport, input);
#endif
}

static int save_measurement(void)
{
	int result;

	if (!measurement_pending) {
		return -ENOENT;
	}
	result = ef800_measurement_append(&workspace, &measurement_record);
	if (result == 0) {
		measurement_pending = false;
	}
	return result;
}

static int parse_measurement_editor_value(void)
{
	char *end;
	long value;

	if (measurement_editor.length == 0U) {
		return -EINVAL;
	}
	if (measurement_editor.field == MEASUREMENT_EDITOR_CODE) {
		strncpy(measurement_code, measurement_editor.input,
			sizeof(measurement_code) - 1U);
		measurement_code[sizeof(measurement_code) - 1U] = '\0';
		if (!ef800_position_text_valid(measurement_code)) {
			return -EINVAL;
		}
		strncpy(measurement_record.code, measurement_code,
			sizeof(measurement_record.code) - 1U);
		return 0;
	}
	value = strtol(measurement_editor.input, &end, 10);
	if (*end != '\0' || value < 0 || value > INT32_MAX) {
		return -EINVAL;
	}
	measurement_record.staff_height_mm = (int32_t)value;
	return 0;
}

static void handle_measurement_editor_key(uint8_t key)
{
	int result;

	if (key == 0x1BU) {
		measurement_editor.active = false;
		active_menu = APPLICATION_MENU_MEASUREMENT_ACTIONS;
		menu_selection = 0U;
		display_active_menu();
		return;
	}
	if (key == 0x08U || key == 0x7FU) {
		if (measurement_editor.length > 0U) {
			measurement_editor.input[--measurement_editor.length] = '\0';
			display_measurement_editor();
		}
		return;
	}
	if (key == '\r' || key == '\n') {
		result = parse_measurement_editor_value();
		if (result != 0) {
			display_line(6U, "Invalid measurement value");
			return;
		}
		if (measurement_editor.field == MEASUREMENT_EDITOR_CODE) {
			measurement_editor.field = MEASUREMENT_EDITOR_STAFF_HEIGHT;
			measurement_editor.length = 0U;
			measurement_editor.input[0] = '\0';
			display_measurement_editor();
		} else {
			measurement_editor.active = false;
			active_menu = APPLICATION_MENU_MEASUREMENT_ACTIONS;
			menu_selection = 0U;
			display_active_menu();
		}
		return;
	}
	if (measurement_editor.field == MEASUREMENT_EDITOR_CODE ?
		ef800_position_text_char_valid((char)key) :
		(key >= '0' && key <= '9')) {
		if (measurement_editor.length + 1U < sizeof(measurement_editor.input)) {
			measurement_editor.input[measurement_editor.length++] = (char)key;
			measurement_editor.input[measurement_editor.length] = '\0';
			display_measurement_editor();
		}
	}
}

static size_t calculation_plugin_count(void)
{
	size_t count = 0U;

	for (size_t index = 0U; index < ef800_plugin_registry_count(&plugin_registry);
		++index) {
		const struct ef800_plugin_registry_entry *entry =
			ef800_plugin_registry_at(&plugin_registry, index);
		if (entry != NULL && entry->state == EF800_PLUGIN_REGISTRY_READY &&
			entry->info.kind == EF800_PLUGIN_KIND_CALCULATION) {
			++count;
		}
	}
	return count;
}

static const struct ef800_plugin_registry_entry *calculation_plugin_at(
	size_t selection)
{
	for (size_t index = 0U; index < ef800_plugin_registry_count(&plugin_registry);
		++index) {
		const struct ef800_plugin_registry_entry *entry =
			ef800_plugin_registry_at(&plugin_registry, index);
		if (entry != NULL && entry->state == EF800_PLUGIN_REGISTRY_READY &&
			entry->info.kind == EF800_PLUGIN_KIND_CALCULATION && selection-- == 0U) {
			return entry;
		}
	}
	return NULL;
}

static void display_calculation_menu(void)
{
	char line[STATUS_LINE_SIZE];
	size_t first = menu_selection >= 3U ? menu_selection - 3U : 0U;

	clear_display();
	display_line(0U, "Calculation");
	display_line(1U, calculation_plugin_count() == 0U ?
		"No calculation plugins" : "Select calculation");
	for (size_t row = 0U; row < 4U; ++row) {
		const struct ef800_plugin_registry_entry *entry =
			calculation_plugin_at(first + row);
		if (entry == NULL) {
			display_line((uint8_t)(3U + row), "");
			continue;
		}
		snprintk(line, sizeof(line), "%c %s", first + row == menu_selection ?
			'>' : ' ', entry->info.name);
		display_line((uint8_t)(3U + row), line);
	}
	display_line(7U, "ENTER select  ESC back");
}

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static void display_plugin_failure(void)
{
	const struct ef800_plugin_registry_entry *entry =
		ef800_plugin_registry_find(&plugin_registry, plugin_failure_plugin_id);
	char line[STATUS_LINE_SIZE];

	clear_display();
	display_line(0U, "Plugin failure");
	display_line(1U, entry == NULL ? "External plugin" : entry->info.name);
	switch (plugin_failure_class) {
	case EF800_PLUGIN_FAILURE_TIMEOUT:
		display_line(3U, "Plugin timed out");
		break;
	case EF800_PLUGIN_FAILURE_FAULT:
		display_line(3U, "Plugin faulted");
		break;
	case EF800_PLUGIN_FAILURE_RECOVERY:
		display_line(3U, "Recovery failed");
		break;
	default:
		display_line(3U, "Plugin call failed");
		break;
	}
	snprintk(line, sizeof(line), "Status %d", plugin_failure_status);
	display_line(4U, line);
	display_line(5U, entry != NULL &&
		(entry->state == EF800_PLUGIN_REGISTRY_FAULTED ||
		 entry->state == EF800_PLUGIN_REGISTRY_TIMED_OUT) ?
		"Disabled until reboot" : "Host retains resource control");
	display_line(7U, "ENTER/ESC return");
}

static void return_from_plugin_failure(void)
{
	active_menu = (enum application_menu)plugin_failure_parent_menu;
	menu_selection = plugin_failure_parent_selection;
	plugin_failure_context_valid = false;
	plugin_display_valid = false;
	display_active_menu();
}

static void show_plugin_failure(uint32_t plugin_id,
	enum ef800_plugin_failure_class failure_class, int status)
{
	plugin_failure_plugin_id = plugin_id;
	plugin_failure_status = status;
	plugin_failure_class = failure_class;
	if (!plugin_failure_context_valid) {
		plugin_failure_parent_menu = APPLICATION_MENU_MEASUREMENTS;
		plugin_failure_parent_selection = 0U;
	}
	active_menu = APPLICATION_MENU_PLUGIN_FAILURE;
	menu_selection = 0U;
	display_plugin_failure();
}

static int quarantine_plugin_failure(uint32_t plugin_id,
	enum ef800_plugin_failure_class failure_class)
{
	enum ef800_plugin_registry_state state;

	if (failure_class == EF800_PLUGIN_FAILURE_FAULT) {
		state = EF800_PLUGIN_REGISTRY_FAULTED;
	} else if (failure_class == EF800_PLUGIN_FAILURE_TIMEOUT) {
		state = EF800_PLUGIN_REGISTRY_TIMED_OUT;
	} else {
		return 0;
	}
	if (plugin_id == 0U) {
		return -EINVAL;
	}
	int ret = ef800_plugin_registry_set_state(&plugin_registry, plugin_id, state);
	if (ret == 0) {
		LOG_WRN("Plugin %u quarantined for this boot: %s",
			(unsigned)plugin_id, state == EF800_PLUGIN_REGISTRY_FAULTED ?
			"fault" : "timeout");
	}
	return ret;
}

static void display_calculation_confirmation(void)
{
	const struct ef800_plugin_registry_entry *entry =
		ef800_plugin_registry_find(&plugin_registry, plugin_failure_plugin_id);
	char line[STATUS_LINE_SIZE];

	clear_display();
	display_line(0U, "Calculation result");
	display_line(1U, entry == NULL ? "Plugin" : entry->info.name);
	snprintk(line, sizeof(line), "Result: %u bytes",
		(unsigned)plugin_async_ui_result.value.calculation.length);
	display_line(3U, line);
	display_line(5U, "Apply result to Workspace?");
	display_line(7U, "ENTER apply  ESC discard");
}

static void finish_calculation_confirmation(bool apply)
{
	int status = 0;

	if (apply) {
		const struct ef800_plugin_async_result *result =
			&plugin_async_ui_result;

		if (!calculation_result_pending ||
			result->value.calculation.data == NULL ||
			result->value.calculation.length == 0U ||
			result->value.calculation.length > EF800_PLUGIN_MAX_RESULT) {
			status = -EINVAL;
		} else {
			status = ef800_workspace_append(&workspace,
				result->value.calculation.data,
				result->value.calculation.length);
		}
	}
	(void)ef800_plugin_host_async_release(plugin_async_ui_token);
	calculation_result_pending = false;
	plugin_failure_context_valid = false;
	active_menu = APPLICATION_MENU_CALCULATIONS;
	display_active_menu();
	if (apply) {
		display_line(6U, status == 0 ? "Calculation applied" :
			"Calculation apply failed");
	} else {
		display_line(6U, "Calculation discarded");
	}
}
#endif

static void display_active_menu(void)
{
	switch (active_menu) {
	case APPLICATION_MENU_SETTINGS:
		display_settings_menu();
		break;
	case APPLICATION_MENU_LCD_CONTRAST:
		display_lcd_contrast_menu();
		break;
	case APPLICATION_MENU_WORKSPACE:
		display_workspace_menu();
		break;
	case APPLICATION_MENU_WORKSPACE_MANAGEMENT:
		display_workspace_management_menu();
		break;
	case APPLICATION_MENU_WORKSPACE_VIEW:
		display_workspace_view();
		break;
	case APPLICATION_MENU_WORKSPACE_ROW_ACTIONS:
		display_workspace_row_actions();
		break;
	case APPLICATION_MENU_WORKSPACE_ROW_EDITOR:
		display_workspace_row_editor();
		break;
	case APPLICATION_MENU_WORKSPACE_ROW_DELETE_CONFIRMATION:
		display_workspace_row_delete_confirmation();
		break;
	case APPLICATION_MENU_WORKSPACE_DELETE_CONFIRMATION:
		display_workspace_delete_confirmation();
		break;
	case APPLICATION_MENU_PORTS:
		display_port_assignment_ports();
		break;
	case APPLICATION_MENU_PORT_DEVICES:
		display_port_assignment_devices();
		break;
	case APPLICATION_MENU_PORT_CONNECTIONS:
		display_port_assignment_connections();
		break;
	case APPLICATION_MENU_PORT_DEBUG_CONFIRMATION:
		display_port_debug_confirmation();
		break;
	case APPLICATION_MENU_MEASUREMENTS:
		display_measurement_devices();
		break;
	case APPLICATION_MENU_MEASUREMENT_ACTIONS:
		if (selected_measurement_has_submenu()) {
			display_measurement_plugin_menu();
		} else {
			display_measurement_actions();
		}
		break;
	case APPLICATION_MENU_MEASUREMENT_EDITOR:
		display_measurement_editor();
		break;
	case APPLICATION_MENU_PLUGIN_FAILURE:
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		display_plugin_failure();
#else
		display_measurement_devices();
#endif
		break;
	case APPLICATION_MENU_CALCULATION_CONFIRMATION:
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		display_calculation_confirmation();
#else
		display_calculation_menu();
#endif
		break;
	case APPLICATION_MENU_CALCULATIONS:
		display_calculation_menu();
		break;
	case APPLICATION_MENU_WORKSPACE_SELECT:
	case APPLICATION_MENU_WORKSPACE_LIST:
		display_workspace_list();
		break;
	case APPLICATION_MENU_MAIN:
	default:
		display_menu();
		break;
	}
}

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static int submit_plugin_async(
	const struct ef800_plugin_async_request *request)
{
	uint32_t token;
	int ret;

	if (plugin_async_ui_pending) {
		return -EBUSY;
	}
	ret = ef800_plugin_host_async_submit(request, &token);
	if (ret == 0) {
		if (!plugin_failure_context_valid &&
			(request->operation == EF800_PLUGIN_ASYNC_SESSION_OPEN ||
			 request->operation == EF800_PLUGIN_ASYNC_MEASURE ||
			 request->operation == EF800_PLUGIN_ASYNC_INPUT ||
			 request->operation == EF800_PLUGIN_ASYNC_CALCULATE)) {
			plugin_failure_parent_menu = request->operation ==
				EF800_PLUGIN_ASYNC_SESSION_OPEN ?
				APPLICATION_MENU_MEASUREMENTS : active_menu;
			plugin_failure_parent_selection = menu_selection;
			plugin_failure_plugin_id = request->plugin_id;
			plugin_failure_context_valid = true;
		}
		plugin_async_ui_pending = true;
		plugin_async_ui_token = token;
		plugin_async_ui_operation = request->operation;
		display_line(6U, "Plugin working...");
	}
	return ret;
}

static int request_plugin_render(void)
{
	const struct ef800_plugin_async_request request = {
		.operation = EF800_PLUGIN_ASYNC_SESSION_RENDER,
		.session = &measurement_plugin_session,
	};
	return submit_plugin_async(&request);
}

static void accept_plugin_measurement(
	const struct ef800_measurement_data *data, bool fixed)
{
	if (data == NULL || measurement_number == INT32_MAX) {
		display_line(6U, data == NULL ? "Measurement failed" :
			"Measurement number full");
		return;
	}
	measurement_record.number = ++measurement_number;
	measurement_record.data = *data;
	measurement_record.staff_height_mm = 0;
	strncpy(measurement_record.code, measurement_code,
		sizeof(measurement_record.code) - 1U);
	measurement_record.code[sizeof(measurement_record.code) - 1U] = '\0';
	measurement_pending = true;
	measurement_fixed = fixed;
	active_menu = APPLICATION_MENU_MEASUREMENT_EDITOR;
	start_measurement_editor();
}

static void process_plugin_async_completion(void)
{
	struct ef800_plugin_async_result *result = &plugin_async_ui_result;
	int ret;

	if (!plugin_async_ui_pending) {
		return;
	}
	ret = ef800_plugin_host_async_poll(plugin_async_ui_token, result);
	if (ret == -EAGAIN) {
		return;
	}
	plugin_async_ui_pending = false;
	if (ret != 0) {
		LOG_WRN("Plugin supervisor completion rejected: %d", ret);
		display_line(6U, "Plugin operation failed");
		return;
	}
	if (result->recovery_required) {
		if (result->recovery_status != 0) {
			LOG_ERR("Plugin recovery could not prove safe state: op=%u status=%d",
				(unsigned)result->operation, result->recovery_status);
			sys_reboot(SYS_REBOOT_COLD);
			return;
		}
		ret = quarantine_plugin_failure(result->plugin_id,
			result->failure_class);
		if (ret != 0) {
			LOG_ERR("Cannot quarantine failed plugin %u: %d",
				(unsigned)result->plugin_id, ret);
			sys_reboot(SYS_REBOOT_COLD);
			return;
		}
		show_plugin_failure(result->plugin_id, result->failure_class,
			result->status);
		return;
	}
	if (result->status != 0 && !(result->operation ==
		EF800_PLUGIN_ASYNC_WORKSPACE_RECORD && result->status == -ENODATA)) {
		show_plugin_failure(result->plugin_id,
			result->failure_class == EF800_PLUGIN_FAILURE_NONE ?
				EF800_PLUGIN_FAILURE_CALLBACK : result->failure_class,
			result->status);
		return;
	}
	switch (result->operation) {
	case EF800_PLUGIN_ASYNC_SESSION_OPEN:
		plugin_display_valid = false;
		active_menu = APPLICATION_MENU_MEASUREMENT_ACTIONS;
		menu_selection = 0U;
		(void)request_plugin_render();
		break;
	case EF800_PLUGIN_ASYNC_SESSION_RENDER:
		if (result->status == 0) {
			plugin_display_cache = result->value.display;
			plugin_display_valid = true;
			display_active_menu();
		} else {
			plugin_display_valid = false;
			display_line(6U, "Plugin display unavailable");
		}
		break;
	case EF800_PLUGIN_ASYNC_SESSION_KEY:
		if (result->status == 0 && result->value.key.consumed) {
			const struct ef800_plugin_async_request request = {
				.operation = EF800_PLUGIN_ASYNC_WORKSPACE_RECORD,
				.session = &measurement_plugin_session,
			};
			if (submit_plugin_async(&request) != 0) {
				(void)request_plugin_render();
			}
		} else if (result->status == 0) {
			(void)request_plugin_render();
		}
		break;
	case EF800_PLUGIN_ASYNC_SESSION_INPUT:
	case EF800_PLUGIN_ASYNC_SESSION_MEASURE:
		if (result->status == 0 &&
			result->operation == EF800_PLUGIN_ASYNC_SESSION_MEASURE) {
			accept_plugin_measurement(&result->value.measurement, false);
		} else if (result->status == 0) {
			(void)request_plugin_render();
		}
		break;
	case EF800_PLUGIN_ASYNC_WORKSPACE_RECORD:
		if (result->status == -ENODATA) {
			(void)request_plugin_render();
		} else if (result->status == 0) {
			int workspace_status = ef800_workspace_append(&workspace,
				result->value.workspace_record.data,
				result->value.workspace_record.length);
			const struct ef800_plugin_async_request request = {
				.operation = EF800_PLUGIN_ASYNC_WORKSPACE_RESULT,
				.session = &measurement_plugin_session,
				.workspace_status = workspace_status,
			};
			display_line(6U, workspace_status == 0 ? "Measurement saved" :
				"Save failed");
			if (submit_plugin_async(&request) != 0) {
				(void)request_plugin_render();
			}
		} else {
			display_line(6U, "Plugin record unavailable");
		}
		break;
	case EF800_PLUGIN_ASYNC_WORKSPACE_RESULT:
		(void)request_plugin_render();
		break;
	case EF800_PLUGIN_ASYNC_MEASURE:
		accept_plugin_measurement(&result->value.measurement,
			plugin_measurement_fixed);
		plugin_failure_context_valid = false;
		break;
	case EF800_PLUGIN_ASYNC_INPUT:
		plugin_failure_context_valid = false;
		break;
	case EF800_PLUGIN_ASYNC_CALCULATE:
		LOG_INF("Calculation plugin %u completed (%u bytes)",
			(unsigned)result->plugin_id,
			(unsigned)result->value.calculation.length);
		calculation_result_pending = true;
		active_menu = APPLICATION_MENU_CALCULATION_CONFIRMATION;
		display_calculation_confirmation();
		break;
	case EF800_PLUGIN_ASYNC_SESSION_CLOSE:
		plugin_display_valid = false;
		plugin_failure_context_valid = false;
		break;
	default:
		LOG_WRN("Unexpected plugin completion operation %u",
			(unsigned)result->operation);
		break;
	}
}
#endif

static size_t active_menu_count(void)
{
	switch (active_menu) {
	case APPLICATION_MENU_SETTINGS:
		return settings_menu_count();
	case APPLICATION_MENU_LCD_CONTRAST:
		return 0U;
	case APPLICATION_MENU_WORKSPACE:
		return ARRAY_SIZE(workspace_items);
	case APPLICATION_MENU_WORKSPACE_MANAGEMENT:
		return ARRAY_SIZE(workspace_management_items);
	case APPLICATION_MENU_WORKSPACE_VIEW:
	case APPLICATION_MENU_WORKSPACE_ROW_EDITOR:
		return 0U;
	case APPLICATION_MENU_WORKSPACE_ROW_ACTIONS:
		return ARRAY_SIZE(workspace_row_action_items);
	case APPLICATION_MENU_WORKSPACE_ROW_DELETE_CONFIRMATION:
		return 1U;
	case APPLICATION_MENU_WORKSPACE_DELETE_CONFIRMATION:
		return 1U;
	case APPLICATION_MENU_WORKSPACE_SELECT:
	case APPLICATION_MENU_WORKSPACE_LIST:
		return workspace_name_count;
	case APPLICATION_MENU_PORTS:
		return EF800_PORT_COUNT;
	case APPLICATION_MENU_PORT_DEVICES:
		return port_assignment_device_count();
	case APPLICATION_MENU_PORT_CONNECTIONS:
		return port_assignment_connection_count();
	case APPLICATION_MENU_PORT_DEBUG_CONFIRMATION:
		return 1U;
	case APPLICATION_MENU_MEASUREMENTS:
		return measurement_port_count;
	case APPLICATION_MENU_MEASUREMENT_ACTIONS:
		return selected_measurement_has_submenu() ? measurement_plugin_action_count() : 7U;
	case APPLICATION_MENU_MEASUREMENT_EDITOR:
	case APPLICATION_MENU_PLUGIN_FAILURE:
	case APPLICATION_MENU_CALCULATION_CONFIRMATION:
		return 0U;
	case APPLICATION_MENU_CALCULATIONS:
		return calculation_plugin_count();
	case APPLICATION_MENU_MAIN:
	default:
		return ARRAY_SIZE(menu_items);
	}
}

static void move_menu_selection(int direction)
{
	const struct ef800_ui_render renderer = {
		.render = ui_render,
		.context = NULL,
	};

	ui_state.menu = (enum ef800_ui_menu)active_menu;
	ui_state.selection = menu_selection;
	ef800_ui_move(&ui_state, direction, ui_menu_count, NULL, &renderer);
}

static void return_to_workspace_menu(void)
{
	active_menu = APPLICATION_MENU_WORKSPACE;
	menu_selection = 0U;
	display_workspace_menu();
}

static void return_to_workspace_management_menu(void)
{
	active_menu = APPLICATION_MENU_WORKSPACE_MANAGEMENT;
	menu_selection = 0U;
	display_workspace_management_menu();
}

static void return_to_main_menu(void)
{
	active_menu = APPLICATION_MENU_MAIN;
	menu_selection = 0U;
	display_menu();
}

static void return_from_child_menu(void)
{
	if (active_menu == APPLICATION_MENU_PLUGIN_FAILURE) {
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		return_from_plugin_failure();
#else
		return_to_main_menu();
#endif
	} else if (active_menu == APPLICATION_MENU_CALCULATION_CONFIRMATION) {
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		finish_calculation_confirmation(false);
#else
		active_menu = APPLICATION_MENU_CALCULATIONS;
		display_calculation_menu();
#endif
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_VIEW) {
		workspace_view_reset();
		return_to_workspace_menu();
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_ROW_ACTIONS) {
		active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
		display_workspace_view();
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_ROW_EDITOR) {
		workspace_row_editor_cancel();
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_ROW_DELETE_CONFIRMATION) {
		active_menu = APPLICATION_MENU_WORKSPACE_ROW_ACTIONS;
		display_workspace_row_actions();
	} else if (active_menu == APPLICATION_MENU_WORKSPACE) {
		return_to_main_menu();
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_MANAGEMENT) {
		active_menu = APPLICATION_MENU_SETTINGS;
		menu_selection = 2U;
		display_settings_menu();
	} else if (active_menu == APPLICATION_MENU_SETTINGS) {
		return_to_main_menu();
	} else if (active_menu == APPLICATION_MENU_LCD_CONTRAST) {
		active_menu = APPLICATION_MENU_SETTINGS;
		menu_selection = ARRAY_SIZE(settings_items) - 1U;
		display_settings_menu();
	} else if (active_menu == APPLICATION_MENU_PORTS) {
		active_menu = APPLICATION_MENU_SETTINGS;
		menu_selection = 1U;
		display_settings_menu();
	} else if (active_menu == APPLICATION_MENU_PORT_DEVICES) {
		active_menu = APPLICATION_MENU_PORTS;
		menu_selection = selected_port_index;
		display_port_assignment_ports();
	} else if (active_menu == APPLICATION_MENU_PORT_CONNECTIONS) {
		active_menu = APPLICATION_MENU_PORT_DEVICES;
		menu_selection = 0U;
		display_port_assignment_devices();
	} else if (active_menu == APPLICATION_MENU_PORT_DEBUG_CONFIRMATION) {
		active_menu = APPLICATION_MENU_PORT_DEVICES;
		menu_selection = 0U;
		display_port_assignment_devices();
	} else if (active_menu == APPLICATION_MENU_MEASUREMENTS) {
		return_to_main_menu();
	} else if (active_menu == APPLICATION_MENU_MEASUREMENT_ACTIONS) {
		close_measurement_plugin_session();
		active_menu = APPLICATION_MENU_MEASUREMENTS;
		menu_selection = selected_measurement_port;
		display_measurement_devices();
	} else if (active_menu == APPLICATION_MENU_MEASUREMENT_EDITOR) {
		measurement_editor.active = false;
		active_menu = APPLICATION_MENU_MEASUREMENT_ACTIONS;
		menu_selection = 0U;
		display_active_menu();
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_DELETE_CONFIRMATION) {
		active_menu = APPLICATION_MENU_WORKSPACE_MANAGEMENT;
		menu_selection = 4U;
		display_workspace_management_menu();
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_CREATE ||
		   active_menu == APPLICATION_MENU_WORKSPACE_SELECT ||
		   active_menu == APPLICATION_MENU_WORKSPACE_LIST) {
		active_menu = APPLICATION_MENU_WORKSPACE_MANAGEMENT;
		menu_selection = 0U;
		display_workspace_management_menu();
	} else {
		return_to_main_menu();
	}
}

static void return_from_position_editor(void)
{
	position_editor.active = false;
	(void)ef800_t6963_clear_text_body(&lcd);
	active_menu = APPLICATION_MENU_MAIN;
	menu_selection = 1U;
	display_menu();
}

static void display_workspace_input(void)
{
	char line[STATUS_LINE_SIZE];

	clear_display();
	display_line(0U, "New workspace");
	display_line(1U, "Use letters, numbers, _ - .");
	snprintk(line, sizeof(line), "> %s", workspace_input);
	display_line(4U, line);
	display_line(6U, "ENTER create  ESC cancel");
}

static void display_workspace_delete_confirmation(void)
{
	char line[STATUS_LINE_SIZE];

	clear_display();
	display_line(0U, "Delete workspace?");
	snprintk(line, sizeof(line), "%s", ef800_workspace_get_name(&workspace) == NULL ?
		"No workspace selected" : ef800_workspace_get_name(&workspace));
	display_line(2U, line);
	display_line(4U, "ENTER delete");
	display_line(5U, "ESC cancel");
}

static void display_workspace_list(void)
{
	size_t first;

	clear_display();
	display_line(0U, active_menu == APPLICATION_MENU_WORKSPACE_SELECT ?
			"Load existing workspace" : "Workspaces");
	if (workspace_name_count == 0U) {
		display_line(4U, "No workspaces");
		display_line(6U, "ESC back");
		return;
	}

	first = menu_selection >= 3U ? menu_selection - 3U : 0U;
	for (size_t row = 0U; row < 4U; ++row) {
		char line[STATUS_LINE_SIZE];
		size_t index = first + row;

		if (index >= workspace_name_count) {
			display_line((uint8_t)(3U + row), "");
			continue;
		}
		snprintk(line, sizeof(line), "%c %s", index == menu_selection ? '>' : ' ',
				workspace_names[index]);
		display_line((uint8_t)(3U + row), line);
	}
	display_line(7U, active_menu == APPLICATION_MENU_WORKSPACE_SELECT ?
			"ENTER select  ESC back" : "ESC back");
}

static const char *position_editor_prompt(enum position_editor_field field)
{
	switch (field) {
	case POSITION_EDITOR_STP:
		return "STP";
	case POSITION_EDITOR_CODE:
		return "Code";
	case POSITION_EDITOR_HEIGHT:
		return "Height metres";
	default:
		return "";
	}
}

static void display_position_editor(void)
{
	char line[STATUS_LINE_SIZE];

	clear_display();
	display_line(0U, "Enter position");
	if (ef800_workspace_is_selected(&workspace)) {
		snprintk(line, sizeof(line), "Workspace: %s",
			ef800_workspace_get_name(&workspace));
	} else {
		snprintk(line, sizeof(line), "Workspace: none");
	}
	display_line(1U, line);
	snprintk(line, sizeof(line), "> %s: %s", position_editor_prompt(position_editor.field),
		position_editor.input);
	display_line(5U, line);
	display_line(7U, "ENTER next  ESC cancel");
}

static void start_position_editor(void)
{
	memset(&position_editor, 0, sizeof(position_editor));
	position_editor.active = true;
	position_editor.field = POSITION_EDITOR_STP;
	display_position_editor();
}

static void copy_position_field(void)
{
	switch (position_editor.field) {
	case POSITION_EDITOR_STP:
		strcpy(position_editor.entry.stp, position_editor.input);
		break;
	case POSITION_EDITOR_CODE:
		strcpy(position_editor.entry.code, position_editor.input);
		break;
	default:
		break;
	}
}

static void handle_position_editor_key(uint8_t key)
{
	int32_t height;
	int result;

	if (key == 0x1BU) {
		return_from_position_editor();
		return;
	}
	if (key == 0x08U || key == 0x7FU) {
		if (position_editor.length > 0U) {
			position_editor.input[--position_editor.length] = '\0';
			display_position_editor();
		}
		return;
	}
	if (key == '\r' || key == '\n') {
		if (position_editor.length == 0U) {
			display_line(6U, "Value is empty");
			return;
		}
		if (position_editor.field == POSITION_EDITOR_HEIGHT) {
			result = ef800_position_parse_height(position_editor.input, &height);
			if (result != 0) {
				display_line(6U, "Invalid height");
				return;
			}
			position_editor.entry.height_millimeters = height;
			result = ef800_position_append(&workspace, &position_editor.entry);
			if (result != 0) {
				display_line(6U, result == -ENODEV ?
					"Workspace not selected" : "Position save failed");
				LOG_WRN("Position append failed: %d", result);
				return;
			}
			display_line(6U, "Position saved");
			return_from_position_editor();
			return;
		}
		copy_position_field();
		position_editor.field++;
		position_editor.length = 0U;
		position_editor.input[0] = '\0';
		display_position_editor();
		return;
	}
	if (position_editor.field == POSITION_EDITOR_HEIGHT) {
		if ((key >= '0' && key <= '9') ||
		    ((key == '+' || key == '-') && position_editor.length == 0U) ||
		    (key == '.' && position_editor.length > 0U &&
		     strchr(position_editor.input, '.') == NULL)) {
			if (position_editor.length + 1U < sizeof(position_editor.input)) {
				position_editor.input[position_editor.length++] = (char)key;
				position_editor.input[position_editor.length] = '\0';
				display_position_editor();
			}
		}
	} else if (ef800_position_text_char_valid((char)key) &&
		   position_editor.length + 1U < sizeof(position_editor.input)) {
		position_editor.input[position_editor.length++] = (char)key;
		position_editor.input[position_editor.length] = '\0';
		display_position_editor();
	}
}

static int collect_workspace_name(const char *name, void *context)
{
	ARG_UNUSED(context);
	if (workspace_name_count >= ARRAY_SIZE(workspace_names)) {
		return -ENOSPC;
	}
	strcpy(workspace_names[workspace_name_count++], name);
	return 0;
}

static int load_workspace_names(void)
{
	workspace_name_count = 0U;
	return ef800_workspace_list(&workspace, collect_workspace_name, NULL);
}

static void display_workspace_result(const char *operation, int result)
{
	char line[STATUS_LINE_SIZE];

	clear_display();
	if (result == 0) {
		snprintk(line, sizeof(line), "%s: %s", operation,
			ef800_workspace_get_name(&workspace) == NULL ? "none" :
			ef800_workspace_get_name(&workspace));
	} else if (result == -ENOTSUP || result == -EBUSY) {
		snprintk(line, sizeof(line), "%s: storage unavailable", operation);
	} else {
		snprintk(line, sizeof(line), "%s failed (%d)", operation, result);
	}
	LOG_INF("%s", line);
	display_line(6U, line);
}

static void open_workspace_list(bool select_workspace)
{
	int result = load_workspace_names();

	if (result != 0) {
		display_workspace_result("Workspace list", result);
		return;
	}
	active_menu = select_workspace ? APPLICATION_MENU_WORKSPACE_SELECT :
		APPLICATION_MENU_WORKSPACE_LIST;
	menu_selection = 0U;
	display_workspace_list();
}

static void activate_workspace_entry(void)
{
	int result;

	switch (menu_selection) {
	case 0U:
		result = ef800_workspace_load_current(&workspace);
		display_workspace_result("Restore current", result);
		break;
	case 1U:
		open_workspace_list(true);
		break;
	case 2U:
		memset(workspace_input, 0, sizeof(workspace_input));
		workspace_input_length = 0U;
		active_menu = APPLICATION_MENU_WORKSPACE_CREATE;
		display_workspace_input();
		break;
	case 3U:
		open_workspace_list(false);
		break;
	case 4U:
		active_menu = APPLICATION_MENU_WORKSPACE_DELETE_CONFIRMATION;
		menu_selection = 0U;
		display_workspace_delete_confirmation();
		break;
	default:
		break;
	}
}

static void activate_workspace_view_entry(void)
{
	int result;

	workspace_view_position_only = menu_selection == 1U;
	result = workspace_view_position_only ? workspace_view_find_position() :
		workspace_view_open_measurements();
	workspace_view_position_only = menu_selection == 1U;
	active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
	menu_selection = 0U;
	if (result != 0) {
		workspace_view_reset();
		if (result == -ENODEV) {
			display_line(3U, "Workspace not selected");
		} else if (result == -ENODATA) {
			display_line(3U, "No workspace data");
		} else {
			display_line(3U, "Workspace read failed");
		}
		display_line(7U, "ESC back");
		return;
	}
	display_workspace_view();
}

static void display_port_assignment_result(const char *operation, int result)
{
	char line[STATUS_LINE_SIZE];

	clear_display();
	if (result == 0) {
		snprintk(line, sizeof(line), "%s", operation);
	} else if (result == -ENOTSUP || result == -ENODEV || result == -EBUSY) {
		snprintk(line, sizeof(line), "%s: storage unavailable", operation);
	} else {
		snprintk(line, sizeof(line), "%s failed (%d)", operation, result);
	}
	LOG_INF("%s", line);
	display_line(6U, line);
}

static enum ef800_usb_personality usb_personality_from_assignment(
	uint8_t builder_id, uint32_t connection);

static int apply_usb_assignment_runtime(uint8_t old_builder_id,
	uint32_t old_connection, uint8_t new_builder_id, uint32_t new_connection,
	bool *restart_required)
{
	enum ef800_usb_personality desired = usb_personality_from_assignment(
		new_builder_id, new_connection);
	enum ef800_usb_transition transition = ef800_usb_personality_transition(
		usb_personality_from_assignment(old_builder_id, old_connection), desired);

	if (desired == EF800_USB_PERSONALITY_MSC &&
		!usb_msd_composition_enabled()) {
		return -ENOTSUP;
	}
	*restart_required = transition == EF800_USB_TRANSITION_RESTART;
	if (*restart_required) {
		return 0;
	}
	if (desired == EF800_USB_PERSONALITY_MTP) {
		/* MSC owns the FAT volume until the controlled restart. Do not try
		 * to prepare MTP roots while the raw-disk personality is active. */
		if (ef800_main_usb_msd_requested()) {
			*restart_required = true;
			return 0;
		}
		int ret = ef800_storage_prepare_mtp_roots();

		if (ret != 0) {
			return ret;
		}
	}
	return ef800_usb_manager_set(desired);
}

static enum ef800_usb_personality usb_personality_from_assignment(
	uint8_t builder_id, uint32_t connection)
{
	if (builder_id == EF800_PORT_BUILDER_MSD &&
		connection == EF800_PORT_CONNECTION_USB_MSD) {
		return EF800_USB_PERSONALITY_MSC;
	}
	if (builder_id == EF800_PORT_BUILDER_MTP &&
		connection == EF800_PORT_CONNECTION_USB_MTP) {
		return EF800_USB_PERSONALITY_MTP;
	}
	if (builder_id == EF800_PORT_BUILDER_DEBUG &&
		connection == EF800_PORT_CONNECTION_USB_CDC) {
		return EF800_USB_PERSONALITY_DEBUG_CDC;
	}
	return EF800_USB_PERSONALITY_NONE;
}

static void display_usb_reboot_hint(void)
{
	clear_display();
	LOG_WRN("USB MSC ownership changed; target rebooting");
	display_line(6U, "Target is now rebooting");
	k_msleep(2000U);
	sys_reboot(SYS_REBOOT_COLD);
}

static int set_port_assignment_runtime(uint8_t port_id, uint8_t builder_id,
						uint32_t connection)
{
	int result;

	if (builder_id == EF800_PORT_BUILDER_MSD &&
		!usb_msd_composition_enabled()) {
		return -ENOTSUP;
	}

	result = builder_id == EF800_PORT_BUILDER_DEBUG ?
		ef800_port_assignment_move_debug(&port_assignments, port_id,
									connection) :
		ef800_port_assignment_set_with_connection(&port_assignments, port_id,
									builder_id, connection);
	return result;
}

#ifdef CONFIG_BOARD_NATIVE_SIM
static int virtual_assign_plugin(void *context, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection)
{
	const struct ef800_plugin_registry_entry *entry;
	const struct ef800_port_descriptor *port;
	uint8_t builder_id;
	int result;

	ARG_UNUSED(context);
	if (plugin_id == 0U || port_id == 0U ||
		(connection != EF800_PORT_CONNECTION_SERIAL &&
		 connection != EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL)) {
		return -EINVAL;
	}
	entry = ef800_plugin_registry_find(&plugin_registry, plugin_id);
	port = ef800_port_at(port_id);
	if (entry == NULL || port == NULL ||
		!ef800_port_connection_is_valid(port_id, connection) ||
		(entry->info.required_hardware != 0U &&
		 (port->capabilities & entry->info.required_hardware) !=
		 entry->info.required_hardware)) {
		return -ENOENT;
	}
	builder_id = uart_owner_for_plugin(plugin_id);
	result = ef800_port_uart_claim(&port_uart_registry, port_id, builder_id,
		connection);
	if (result != 0) {
		LOG_WRN("Native plugin UART claim failed: %d", result);
		return result;
	}
	result = ef800_port_assignment_set_plugin_with_connection(&port_assignments,
		port_id, plugin_id, connection);
	LOG_INF("Native plugin assignment record result: %d", result);
	if (result == 0) {
		result = initialize_virtual_measurement_transport(port_id, plugin_id);
		LOG_INF("Native plugin transport initialization result: %d", result);
	}
	/* Native-sim UART device readiness is intentionally provided by the
	 * virtual frontpanel, not by the physical-board routing probe.  The claim
	 * above establishes ownership; invoking the board reconciliation pass
	 * here would reject an otherwise valid virtual assignment with -ENOSYS. */
	if (result != 0) {
		LOG_WRN("Native plugin assignment failed: %d", result);
	}
	if (result != 0) {
		(void)ef800_port_uart_release(&port_uart_registry, port_id, builder_id,
			connection);
	}
	return result;
}

static int virtual_execute_plugin(void *context, uint8_t port_id)
{
	uint32_t plugin_id;
	struct ef800_measurement_data data;
	int result;

	ARG_UNUSED(context);
	if (port_id == 0U || port_id > EF800_PORT_COUNT ||
		!ef800_port_assignment_is_assigned(&port_assignments, port_id)) {
		LOG_WRN("Native plugin execute: port not assigned");
		return -EINVAL;
	}
	plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments, port_id);
	if (ef800_plugin_registry_find(&plugin_registry, plugin_id) == NULL ||
		ef800_plugin_registry_state_for(&plugin_registry, plugin_id) !=
		EF800_PLUGIN_REGISTRY_READY) {
		LOG_WRN("Native plugin execute: plugin %u unavailable", (unsigned)plugin_id);
		return -ENOENT;
	}
	if (measurement_transports[port_id - 1U].uart == NULL) {
		result = initialize_virtual_measurement_transport(port_id, plugin_id);
		if (result != 0) {
			LOG_WRN("Native plugin execute: UART init failed: %d", result);
			return result;
		}
	}
	result = ef800_plugin_host_measure(plugin_id,
		&measurement_transports[port_id - 1U].transport, &data);
	if (result == 0) {
		char line[STATUS_LINE_SIZE];
		snprintk(line, sizeof(line), "Plugin %u: H%ld V%ld D%ld",
			(unsigned)plugin_id, (long)data.horizontal_gon_1e4,
			(long)data.vertical_gon_1e4, (long)data.distance_mm);
		display_line(0U, "Native plugin result");
		display_line(1U, line);
	}
	if (result != 0) {
		LOG_WRN("Native plugin execute: measure failed: %d", result);
	}
	return result;
}
#endif

static int remove_port_assignment_runtime(uint8_t port_id)
{
	int result = ef800_port_assignment_remove(&port_assignments, port_id);

	if (result == -ENOTSUP) {
		result = ef800_port_assignment_import_with_connection(&port_assignments,
													 port_id,
													 EF800_PORT_BUILDER_NONE,
													 EF800_PORT_CONNECTION_NONE);
	}
	return result;
}

static int restore_port_assignment(uint8_t port_id, uint8_t builder_id,
					   uint32_t connection)
{
	return builder_id == EF800_PORT_BUILDER_NONE ?
		remove_port_assignment_runtime(port_id) :
		set_port_assignment_runtime(port_id, builder_id, connection);
}

static void activate_port_assignment_entry(void)
{
	const struct ef800_port_descriptor *port =
		ef800_port_at(selected_port_index);
	const struct ef800_port_device_descriptor *device;
	uint8_t old_builder_id;
	uint32_t old_connection;
	bool usb_restart_required = false;
	int result;

	if (port == NULL) {
		return;
	}
	if (!port->fixed && menu_selection == port_assignment_device_count() - 1U) {
		old_builder_id = ef800_port_assignment_get_builder(&port_assignments,
			port->id);
		old_connection = ef800_port_assignment_get_connection(&port_assignments,
			port->id);
		if (old_builder_id != EF800_PORT_BUILDER_NONE) {
			result = ef800_port_uart_release(&port_uart_registry, port->id,
							 old_builder_id, old_connection);
			if (result != 0) {
				display_port_assignment_result("UART release failed", result);
				return;
			}
		}
		result = remove_port_assignment_runtime(port->id);
		if (result == 0) {
			result = reconcile_port_uart_state();
		}
		if (result == 0 && port->id == 4U) {
			result = apply_usb_assignment_runtime(old_builder_id, old_connection,
				EF800_PORT_BUILDER_NONE, EF800_PORT_CONNECTION_NONE,
				&usb_restart_required);
		}
		if (result != 0 && old_builder_id != EF800_PORT_BUILDER_NONE) {
			(void)restore_port_assignment(port->id, old_builder_id, old_connection);
			(void)ef800_port_uart_claim(&port_uart_registry, port->id,
							     old_builder_id, old_connection);
		}
		display_port_assignment_result("Assignment removed", result);
		if (result == 0) {
			active_menu = APPLICATION_MENU_PORTS;
			menu_selection = selected_port_index;
			display_port_assignment_ports();
			if (port->id == 4U && usb_restart_required) {
				display_usb_reboot_hint();
			}
		}
		return;
	}
	device = device_at_selection(port, menu_selection);
	if (device == NULL) {
		const struct ef800_plugin_registry_entry *plugin =
			plugin_at_selection(port, menu_selection);
		if (plugin != NULL) {
			selected_plugin_id = plugin->info.plugin_id;
			selected_builder_id = 0U;
			menu_selection = 0U;
			active_menu = APPLICATION_MENU_PORT_CONNECTIONS;
			display_port_assignment_connections();
			return;
		}
	}
	if (device == NULL) {
		display_port_assignment_result("Invalid device selection", -EINVAL);
		return;
	}
	selected_builder_id = device->builder_id;
	/* Built-in devices and external plugins use separate selection state.
	 * Treating the MSD builder ID as a plugin ID hides its USB-MSD connection
	 * and makes Enter fail with -EINVAL. */
	selected_plugin_id = 0U;
	if (selected_builder_id == EF800_PORT_BUILDER_DEBUG) {
		active_menu = APPLICATION_MENU_PORT_DEBUG_CONFIRMATION;
		menu_selection = 0U;
		display_port_debug_confirmation();
		return;
	}
	menu_selection = 0U;
	active_menu = APPLICATION_MENU_PORT_CONNECTIONS;
	display_port_assignment_connections();
}

static void commit_port_assignment_connection(void)
{
	const struct ef800_port_descriptor *port = ef800_port_at(selected_port_index);
	const struct ef800_port_connection_descriptor *connection =
		connection_at_selection(menu_selection);
	uint8_t old_builder_id;
	uint8_t claim_builder_id;
	uint32_t old_plugin_id;
	uint32_t old_connection;
	uint8_t old_debug_port = 0U;
	uint32_t old_debug_connection = EF800_PORT_CONNECTION_NONE;
	bool usb_restart_required = false;
	int result;

	if (port == NULL || connection == NULL) {
		display_port_assignment_result("Invalid connection selection", -EINVAL);
		return;
	}
	old_builder_id = ef800_port_assignment_get_builder(&port_assignments, port->id);
	old_plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments, port->id);
	old_connection = ef800_port_assignment_get_connection(&port_assignments, port->id);
	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		if (ef800_port_assignment_get_builder(&port_assignments, port_id) ==
			EF800_PORT_BUILDER_DEBUG) {
			old_debug_port = port_id;
			old_debug_connection = ef800_port_assignment_get_connection(
				&port_assignments, port_id);
			break;
		}
	}
	if (selected_builder_id == EF800_PORT_BUILDER_DEBUG) {
		result = set_port_assignment_runtime(port->id, selected_builder_id,
							 connection->capability);
		if (result == 0) {
			result = reconcile_port_uart_state();
		}
		if (result == 0 && port->id == 4U) {
			result = apply_usb_assignment_runtime(old_builder_id, old_connection,
				EF800_PORT_BUILDER_DEBUG, connection->capability,
				&usb_restart_required);
		}
		if (result != 0) {
			(void)restore_port_assignment(port->id, old_builder_id, old_connection);
			if (old_debug_port != 0U && old_debug_port != port->id) {
				(void)restore_port_assignment(old_debug_port,
					EF800_PORT_BUILDER_DEBUG, old_debug_connection);
			}
		}
		display_port_assignment_result(result == 0 ? "DebugConsole moved" :
			"DebugConsole move failed", result);
		if (result == 0) {
			active_menu = APPLICATION_MENU_PORTS;
			menu_selection = selected_port_index;
			display_port_assignment_ports();
			if ((port->id == 4U && usb_restart_required) ||
				(old_debug_port == 4U && old_debug_port != port->id)) {
				display_usb_reboot_hint();
			}
		}
		return;
	}
	/* A plugin selection uses builder_id zero while the selected plugin ID is
	 * still a small integer (for example native-sim plugin 2 or 3).  Do not
	 * reinterpret that plugin ID as a built-in device builder such as MSD;
	 * measurement plugins claim the DebugDevice-compatible serial route. */
	claim_builder_id = selected_builder_id == 0U ?
		uart_owner_for_plugin(selected_plugin_id) : selected_builder_id;
	if (old_builder_id != EF800_PORT_BUILDER_NONE) {
		result = ef800_port_uart_release(&port_uart_registry, port->id,
							 old_builder_id, old_connection);
		if (result != 0) {
			display_port_assignment_result("UART release failed", result);
			return;
		}
	}
	result = ef800_port_uart_claim(&port_uart_registry, port->id,
						       claim_builder_id,
						       connection->capability);
	if (result != 0) {
		if (old_builder_id != EF800_PORT_BUILDER_NONE) {
			(void)ef800_port_uart_claim(&port_uart_registry, port->id,
							     old_builder_id, old_connection);
		}
		display_port_assignment_result("Connection unavailable", result);
		return;
	}
	result = selected_builder_id == 0U ?
		ef800_port_assignment_set_plugin_with_connection(&port_assignments,
			port->id, selected_plugin_id, connection->capability) :
		set_port_assignment_runtime(port->id, selected_builder_id,
			connection->capability);
	LOG_INF("Port 4 assignment set builder=%u connection=%u result=%d",
		(unsigned)selected_builder_id, (unsigned)connection->capability, result);
	if (result == 0) {
#ifdef CONFIG_BOARD_NATIVE_SIM
		/* Native-sim plugin assignments use the virtual frontpanel UART
		 * boundary; physical console routing is intentionally unavailable. */
		if (selected_builder_id == 0U) {
			result = initialize_virtual_measurement_transport(port->id,
				selected_plugin_id);
		} else if (port->id == 4U &&
			selected_builder_id == EF800_PORT_BUILDER_MTP &&
			connection->capability == EF800_PORT_CONNECTION_USB_MTP) {
			/* Port 4 MTP is entirely device-next USB; there is no native UART
			 * claim to reconcile before the USB personality can be started. */
			result = 0;
		} else {
			result = reconcile_port_uart_state();
		}
	#else
		result = reconcile_port_uart_state();
	#endif
		LOG_INF("Port UART reconciliation result=%d", result);
	}
	if (result == 0 && port->id == 4U) {
		result = apply_usb_assignment_runtime(old_builder_id, old_connection,
			selected_builder_id, connection->capability, &usb_restart_required);
		LOG_INF("USB assignment runtime result=%d restart=%d", result,
			(int)usb_restart_required);
	}
	if (result == 0) {
		char line[STATUS_LINE_SIZE];
		const struct ef800_plugin_registry_entry *assigned_plugin =
			ef800_plugin_registry_find(&plugin_registry, selected_plugin_id);
		if (assigned_plugin != NULL && port->id <= EF800_PORT_COUNT) {
			strncpy(assigned_plugin_names[port->id - 1U],
				assigned_plugin->info.name,
				sizeof(assigned_plugin_names[0]) - 1U);
			assigned_plugin_names[port->id - 1U][sizeof(assigned_plugin_names[0]) - 1U] = '\0';
		}
			snprintk(line, sizeof(line), "Assigned plugin %u to port %u",
			(unsigned)selected_plugin_id, port->id);
		display_port_assignment_result(line, 0);
		if (port->id == 4U && usb_restart_required) {
			display_usb_reboot_hint();
		}
		active_menu = APPLICATION_MENU_PORTS;
		menu_selection = selected_port_index;
		display_port_assignment_ports();
	} else {
		(void)ef800_port_uart_release(&port_uart_registry, port->id,
						       claim_builder_id, connection->capability);
		(void)ef800_port_assignment_import_plugin_with_connection(&port_assignments,
			port->id, old_plugin_id, old_connection);
		if (old_builder_id != EF800_PORT_BUILDER_NONE) {
			(void)ef800_port_uart_claim(&port_uart_registry, port->id,
							     old_builder_id, old_connection);
		}
		display_port_assignment_result("Assignment failed", result);
	}
}

static void activate_menu_entry(void)
{
	if (active_menu == APPLICATION_MENU_WORKSPACE_VIEW) {
		workspace_view_open_row_actions();
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_ROW_ACTIONS) {
		if (menu_selection == 1U) {
			active_menu = APPLICATION_MENU_WORKSPACE_ROW_DELETE_CONFIRMATION;
			display_workspace_row_delete_confirmation();
		} else if (menu_selection == 2U) {
			active_menu = APPLICATION_MENU_WORKSPACE_VIEW;
			display_workspace_view();
		} else {
			workspace_row_editor_start();
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_ROW_DELETE_CONFIRMATION) {
		workspace_view_delete_selected_row();
		return;
	}
	if (active_menu == APPLICATION_MENU_MAIN) {
		switch (menu_selection) {
		case 0U:
			active_menu = APPLICATION_MENU_WORKSPACE;
			menu_selection = 0U;
			display_workspace_menu();
			break;
		case 1U:
			if (!ef800_workspace_is_selected(&workspace)) {
				display_line(6U, "Workspace not selected");
				LOG_INF("Position entry requires a selected Workspace");
			} else {
				start_position_editor();
			}
			break;
		case 2U:
			if (load_measurement_ports() != 0) {
				display_line(6U, "No measurement device ready");
				LOG_INF("No assigned measurement device is ready");
			} else {
				active_menu = APPLICATION_MENU_MEASUREMENTS;
				menu_selection = 0U;
				display_measurement_devices();
			}
			break;
		case 3U:
			active_menu = APPLICATION_MENU_CALCULATIONS;
			menu_selection = 0U;
			LOG_INF("Calculation menu opened with %u plugin(s)",
				(unsigned)calculation_plugin_count());
			display_calculation_menu();
			break;
		case 4U:
			active_menu = APPLICATION_MENU_SETTINGS;
			menu_selection = 0U;
			display_settings_menu();
			break;
		default:
			display_line(6U, "Not implemented yet");
			LOG_INF("Menu entry not implemented: %s", menu_items[menu_selection]);
			break;
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_SETTINGS) {
		if (menu_selection == 0U) {
			start_time_editor();
		} else if (menu_selection == 1U) {
			active_menu = APPLICATION_MENU_PORTS;
			menu_selection = 0U;
			display_port_assignment_ports();
		} else if (menu_selection == 2U) {
			active_menu = APPLICATION_MENU_WORKSPACE_MANAGEMENT;
			menu_selection = 0U;
			display_workspace_management_menu();
		} else if (menu_selection == 3U &&
			lcd_contrast_composition_mode() == EF800_LCD_CONTRAST_PWM) {
			active_menu = APPLICATION_MENU_LCD_CONTRAST;
			display_lcd_contrast_menu();
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE) {
		activate_workspace_view_entry();
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_MANAGEMENT) {
		activate_workspace_entry();
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_DELETE_CONFIRMATION) {
		int result = ef800_workspace_delete_selected(&workspace);
		display_workspace_result("Delete workspace", result);
		if (result == 0) {
			active_menu = APPLICATION_MENU_WORKSPACE_MANAGEMENT;
			menu_selection = 4U;
			display_workspace_management_menu();
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_PORTS) {
		selected_port_index = menu_selection;
		active_menu = APPLICATION_MENU_PORT_DEVICES;
		menu_selection = 0U;
		display_port_assignment_devices();
		return;
	}
	if (active_menu == APPLICATION_MENU_PORT_DEVICES) {
		activate_port_assignment_entry();
		return;
	}
	if (active_menu == APPLICATION_MENU_PORT_CONNECTIONS) {
		commit_port_assignment_connection();
		return;
	}
	if (active_menu == APPLICATION_MENU_PORT_DEBUG_CONFIRMATION) {
		const uint32_t connection = ef800_port_assignment_default_connection(
			ef800_port_at(selected_port_index)->id, EF800_PORT_BUILDER_DEBUG);
		size_t index = 0U;

		for (size_t item = 0U; item < ef800_port_connection_count(
			ef800_port_at(selected_port_index)->id); ++item) {
			const struct ef800_port_connection_descriptor *candidate =
				ef800_port_connection_at(ef800_port_at(selected_port_index)->id, item);

			if (candidate != NULL && candidate->capability == connection) {
				break;
			}
			if (candidate != NULL && ef800_port_device_supports_connection(
				EF800_PORT_BUILDER_DEBUG, candidate->capability)) {
				++index;
			}
		}
		menu_selection = index;
		selected_builder_id = EF800_PORT_BUILDER_DEBUG;
		active_menu = APPLICATION_MENU_PORT_CONNECTIONS;
		commit_port_assignment_connection();
		return;
	}
	if (active_menu == APPLICATION_MENU_CALCULATIONS) {
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		const struct ef800_plugin_registry_entry *entry =
			calculation_plugin_at(menu_selection);
		size_t snapshot_length = 0U;
		int result;

		if (entry == NULL) {
			display_line(6U, "Calculation unavailable");
			return;
		}
		LOG_INF("Calculation selection %u maps to plugin %u",
			(unsigned)menu_selection, (unsigned)entry->info.plugin_id);
		if (!ef800_workspace_is_selected(&workspace)) {
			LOG_WRN("Calculation plugin %u not run: no Workspace selected",
				(unsigned)entry->info.plugin_id);
			display_line(6U, "Workspace not selected");
			return;
		}
		result = ef800_workspace_snapshot(&workspace, calculation_snapshot,
			sizeof(calculation_snapshot), &snapshot_length);
		if (result != 0 || snapshot_length == 0U) {
			LOG_WRN("Calculation plugin %u not run: Workspace snapshot unavailable (%d, %u)",
				(unsigned)entry->info.plugin_id, result,
				(unsigned)snapshot_length);
			display_line(6U, "Workspace snapshot unavailable");
			return;
		}
		const struct ef800_plugin_async_request request = {
			.operation = EF800_PLUGIN_ASYNC_CALCULATE,
			.plugin_id = entry->info.plugin_id,
			.workspace = &workspace,
			.snapshot = calculation_snapshot,
			.snapshot_length = snapshot_length,
			.output_capacity = EF800_PLUGIN_MAX_RESULT,
		};
		if (submit_plugin_async(&request) != 0) {
			LOG_WRN("Calculation plugin %u request rejected",
				(unsigned)entry->info.plugin_id);
			display_line(6U, "Calculation unavailable");
		}
#else
		display_line(6U, "Calculation worker unavailable");
#endif
		return;
	}
	if (active_menu == APPLICATION_MENU_MEASUREMENTS) {
		if (menu_selection < measurement_port_count) {
			selected_measurement_port = measurement_port_ids[menu_selection];
			if (selected_measurement_has_submenu()) {
				uint32_t plugin_id = ef800_port_assignment_get_plugin_id(&port_assignments,
					selected_measurement_port);
				struct ef800_measurement_context context = {
					.struct_size = sizeof(context),
					.point_number = ef800_workspace_current_point_number(&workspace),
				};
				const char *current_code = ef800_workspace_current_code(&workspace);
				if (current_code != NULL) {
					strncpy(context.code, current_code, sizeof(context.code) - 1U);
				}
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
				struct ef800_plugin_async_request request = {
					.operation = EF800_PLUGIN_ASYNC_SESSION_OPEN,
					.session = &measurement_plugin_session,
					.plugin_id = plugin_id,
					.transport = &measurement_transports[
						selected_measurement_port - 1U].transport,
					.context = context,
				};
				set_plugin_async_route(&request, selected_measurement_port,
					plugin_id);
				if (submit_plugin_async(&request) != 0) {
					display_line(6U, "Plugin unavailable");
				}
				return;
#else
				int session_result = ef800_plugin_host_measurement_session_open(
						&measurement_plugin_session, plugin_id,
						&measurement_transports[selected_measurement_port - 1U].transport);
				if (session_result == 0) {
					session_result = ef800_plugin_host_measurement_session_set_context(
						&measurement_plugin_session, &context);
				}
				if (session_result != 0) {
					display_line(6U, "Plugin unavailable");
					return;
				}
#endif
			}
			active_menu = APPLICATION_MENU_MEASUREMENT_ACTIONS;
			menu_selection = 0U;
			display_active_menu();
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_MEASUREMENT_ACTIONS) {
		int result;
		if (selected_measurement_has_submenu()) {
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
			const struct ef800_plugin_async_request request = {
				.operation = EF800_PLUGIN_ASYNC_SESSION_KEY,
				.session = &measurement_plugin_session,
				.key = '\r',
			};
			if (submit_plugin_async(&request) != 0) {
				display_line(6U, "Plugin action unavailable");
			}
#else
			bool consumed = false;
			result = ef800_plugin_host_measurement_session_key(
				&measurement_plugin_session, '\r', &consumed);
			if (result == 0 && consumed) {
				append_plugin_workspace_record();
				display_active_menu();
			} else {
				LOG_WRN("Plugin menu action failed: result=%d consumed=%d",
					result, consumed);
				display_line(6U, "Plugin action failed");
			}
#endif
			return;
		}

		switch (menu_selection) {
		case 0U:
		case 3U:
			result = trigger_measurement(menu_selection == 3U);
			if (result == 0) {
				active_menu = APPLICATION_MENU_MEASUREMENT_EDITOR;
				start_measurement_editor();
			} else if (result == -EINPROGRESS) {
				display_line(6U, "Measuring...");
			} else {
				display_line(6U, "Measurement failed");
				LOG_WRN("Measurement trigger failed: %d", result);
			}
			break;
		case 1U:
			result = save_measurement();
			display_line(6U, result == 0 ? "Measurement saved" :
				(result == -ENODEV ? "Workspace not selected" : "Save failed"));
			if (result == 0 && measurement_fixed) {
				result = trigger_measurement(true);
				if (result == 0) {
					active_menu = APPLICATION_MENU_MEASUREMENT_EDITOR;
					start_measurement_editor();
				} else if (result == -EINPROGRESS) {
					display_line(6U, "Measuring...");
				} else {
					measurement_fixed = false;
					LOG_WRN("Fixed measurement trigger failed: %d", result);
				}
			}
			break;
		case 2U:
			measurement_pending = false;
			measurement_fixed = false;
			display_line(6U, "Measurement discarded");
			break;
		case 4U:
			result = send_measurement_input(EF800_MEASUREMENT_INPUT_UP);
			display_line(6U, result == 0 ? "Device up sent" :
				result == -EINPROGRESS ? "Plugin working..." : "Device up failed");
			break;
		case 5U:
			result = send_measurement_input(EF800_MEASUREMENT_INPUT_DOWN);
			display_line(6U, result == 0 ? "Device down sent" :
				result == -EINPROGRESS ? "Plugin working..." : "Device down failed");
			break;
		case 6U:
			result = send_measurement_input(EF800_MEASUREMENT_INPUT_ENTER);
			display_line(6U, result == 0 ? "Device enter sent" :
				result == -EINPROGRESS ? "Plugin working..." : "Device enter failed");
			break;
		default:
			break;
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE) {
		activate_workspace_entry();
	}
}

static bool workspace_input_character_valid(uint8_t key)
{
	return (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
	       (key >= '0' && key <= '9') || key == '_' || key == '-' || key == '.';
}

static void handle_workspace_dialog_key(uint8_t key)
{
	int result;

	if (active_menu == APPLICATION_MENU_WORKSPACE_CREATE) {
		if (key == 0x1BU) {
			return_to_workspace_management_menu();
			return;
		}
		if (key == '\r' || key == '\n') {
			if (workspace_input_length == 0U) {
				display_line(6U, "Workspace name is empty");
				return;
			}
			result = ef800_workspace_create(&workspace, workspace_input);
			display_workspace_result("Create workspace", result);
			if (result == 0) {
				return_to_workspace_management_menu();
			}
			return;
		}
		if (key == 0x08U || key == 0x7FU) {
			if (workspace_input_length == 0U) {
				return;
			}
			workspace_input[--workspace_input_length] = '\0';
			display_workspace_input();
			return;
		}
		if (workspace_input_character_valid(key) &&
		    workspace_input_length + 1U < sizeof(workspace_input)) {
			workspace_input[workspace_input_length++] = (char)key;
			workspace_input[workspace_input_length] = '\0';
			display_workspace_input();
		}
		/* Workspace-name entry is modal. Never pass a character to menu
		 * shortcut handling, even when it is invalid or the buffer is full. */
		return;
	}
	if (key == 0x1BU || key == 0x08U || key == 0x7FU) {
		return_to_workspace_management_menu();
		return;
	}
	if (key == '\r' || key == '\n') {
		if (active_menu == APPLICATION_MENU_WORKSPACE_SELECT &&
		    workspace_name_count > 0U) {
			result = ef800_workspace_select(&workspace,
						workspace_names[menu_selection]);
			display_workspace_result("Load workspace", result);
			if (result == 0) {
				return_to_workspace_management_menu();
			}
		}
		return;
	}
}

static bool handle_application_key(uint8_t key)
{
	const char *const *shortcuts;
	size_t count;

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (plugin_async_ui_pending) {
		return true;
	}
	if (active_menu == APPLICATION_MENU_PLUGIN_FAILURE) {
		if (key == '\r' || key == '\n' || key == 0x1BU || key == 0x08U ||
			key == 0x7FU) {
			return_from_plugin_failure();
		}
		return true;
	}
	if (active_menu == APPLICATION_MENU_CALCULATION_CONFIRMATION) {
		if (key == '\r' || key == '\n') {
			finish_calculation_confirmation(true);
		} else if (key == 0x1BU || key == 0x08U || key == 0x7FU) {
			finish_calculation_confirmation(false);
		}
		return true;
	}
#endif
	if (active_menu == APPLICATION_MENU_WORKSPACE_CREATE) {
		handle_workspace_dialog_key(key);
		return true;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_ROW_EDITOR) {
		workspace_row_editor_key(key);
		return true;
	}
	if (position_editor.active) {
		handle_position_editor_key(key);
		return true;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_VIEW) {
		if (key == 0x1BU || key == 0x08U || key == 0x7FU) {
			return_from_child_menu();
			return true;
		}
		if (key == '\r' || key == '\n') {
			workspace_view_open_row_actions();
			return true;
		}
		if (key == 'u' || key == 'U') {
			if (workspace_view_move(-1) == 0) {
				display_workspace_view();
			}
			return true;
		}
		if (key == 'd' || key == 'D') {
			if (workspace_view_move(1) == 0) {
				display_workspace_view();
			}
			return true;
		}
		return false;
	}
	if (measurement_editor.active) {
		handle_measurement_editor_key(key);
		return true;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_SELECT ||
	    active_menu == APPLICATION_MENU_WORKSPACE_LIST) {
		handle_workspace_dialog_key(key);
		return true;
	}
	if (key == 0x1BU || key == 0x08U || key == 0x7FU) {
		if (active_menu == APPLICATION_MENU_MAIN) {
			display_line(6U, "Already at main menu");
		} else {
			return_from_child_menu();
		}
		return true;
	}
	if (active_menu == APPLICATION_MENU_MEASUREMENT_ACTIONS &&
		measurement_plugin_session.state == EF800_MEASUREMENT_SESSION_ACTIVE &&
		key != '\r' && key != '\n') {
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		const struct ef800_plugin_async_request request = {
			.operation = EF800_PLUGIN_ASYNC_SESSION_KEY,
			.session = &measurement_plugin_session,
			.key = key,
		};
		(void)submit_plugin_async(&request);
		return true;
#else
		bool consumed = false;
		int result = ef800_plugin_host_measurement_session_key(
			&measurement_plugin_session, key, &consumed);
		if (consumed) {
			display_active_menu();
			if (result != 0) display_line(6U, "Plugin action failed");
			return true;
		}
#endif
	}
	if (key == '\r' || key == '\n') {
		activate_menu_entry();
		return true;
	}
	if (active_menu == APPLICATION_MENU_MAIN) {
		shortcuts = menu_shortcuts;
		count = ARRAY_SIZE(menu_items);
	} else if (active_menu == APPLICATION_MENU_SETTINGS) {
		shortcuts = settings_shortcuts;
		count = settings_menu_count();
	} else if (active_menu == APPLICATION_MENU_WORKSPACE) {
		shortcuts = workspace_view_shortcuts;
		count = ARRAY_SIZE(workspace_items);
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_MANAGEMENT) {
		shortcuts = workspace_shortcuts;
		count = ARRAY_SIZE(workspace_management_items);
	} else if (active_menu == APPLICATION_MENU_WORKSPACE_ROW_ACTIONS) {
		shortcuts = workspace_row_action_shortcuts;
		count = ARRAY_SIZE(workspace_row_action_items);
	} else if (active_menu == APPLICATION_MENU_PORTS) {
		for (size_t index = 0U; index < ef800_port_count(); ++index) {
			if (key == (uint8_t)ef800_port_at(index)->shortcut) {
				menu_selection = (uint8_t)index;
				activate_menu_entry();
				return true;
			}
		}
		return false;
	} else if (active_menu == APPLICATION_MENU_PORT_DEVICES) {
		const struct ef800_port_descriptor *port =
			ef800_port_at(selected_port_index);
		for (size_t index = 0U; index < ef800_port_device_count(); ++index) {
			const struct ef800_port_device_descriptor *device =
				ef800_port_device_at(index);
			if (ef800_port_device_is_compatible(port, device) &&
				(device->builder_id != EF800_PORT_BUILDER_MSD ||
				 usb_msd_composition_enabled()) &&
				key == (uint8_t)device->shortcut) {
				/* The displayed list contains only compatible devices. */
				size_t selection = 0U;
				for (size_t candidate = 0U; candidate < index; ++candidate) {
					const struct ef800_port_device_descriptor *candidate_device =
						ef800_port_device_at(candidate);
					if (ef800_port_device_is_compatible(port, candidate_device) &&
						(candidate_device->builder_id != EF800_PORT_BUILDER_MSD ||
						 usb_msd_composition_enabled())) {
						++selection;
					}
				}
				menu_selection = (uint8_t)selection;
				activate_menu_entry();
				return true;
			}
		}
		if (key == 'r') {
			menu_selection = (uint8_t)(port_assignment_device_count() - 1U);
			activate_menu_entry();
			return true;
		}
		return false;
	} else if (active_menu == APPLICATION_MENU_PORT_CONNECTIONS) {
		return false;
	} else if (active_menu == APPLICATION_MENU_MEASUREMENTS ||
		active_menu == APPLICATION_MENU_MEASUREMENT_ACTIONS) {
		return false;
	} else {
		shortcuts = workspace_shortcuts;
		count = 0U;
	}
	for (size_t index = 0U; index < count; ++index) {
		if (key == (uint8_t)shortcuts[index][0]) {
			menu_selection = (uint8_t)index;
			activate_menu_entry();
			return true;
		}
	}
	return false;
}

static int read_adc_battery(void *context,
	struct ef800_max17043_status *status)
{
	struct ef800_battery_soc_table table;
	struct adc_sequence sequence;
	int16_t raw_sample;
	int32_t pin_mv;
#ifdef CONFIG_BOARD_NATIVE_SIM
	uint16_t pin_mv_input;
#endif
	int result;

	ARG_UNUSED(context);
	if (status == NULL || !battery_adc_ready || !battery_soc_store_ready) {
		return -ENODEV;
	}
	result = ef800_battery_soc_store_get_table(&battery_soc_store, &table);
	if (result != 0) {
		return result;
	}
#ifdef CONFIG_BOARD_NATIVE_SIM
	if (!ef800_virtual_profile_available(4U)) {
		return -ENODEV;
	}
	result = ef800_battery_pin_mv_from_soc(&table,
		ef800_virtual_profile_battery_soc(), &pin_mv_input);
	if (result != 0) {
		return result;
	}
	result = adc_emul_const_value_set(battery_adc.dev,
		battery_adc.channel_id, pin_mv_input);
	if (result != 0) {
		return result;
	}
#endif
	result = adc_sequence_init_dt(&battery_adc, &sequence);
	if (result != 0) {
		return result;
	}
	sequence.buffer = &raw_sample;
	sequence.buffer_size = sizeof(raw_sample);
	result = adc_read_dt(&battery_adc, &sequence);
	if (result != 0) {
		return result;
	}
	pin_mv = raw_sample;
	result = adc_raw_to_millivolts_dt(&battery_adc, &pin_mv);
	if (result != 0 || pin_mv < 0 ||
	    pin_mv > (int32_t)EF800_BATTERY_SOC_ADC_MAX_MV) {
		return result != 0 ? result : -ERANGE;
	}
	result = ef800_battery_soc_from_pin_mv(&table, (uint16_t)pin_mv,
		&status->soc_percent);
	if (result == 0) {
		status->voltage_uv = (uint32_t)pin_mv * 1000U;
	}
	return result;
}

static void draw_battery_icon(uint8_t soc)
{
	static const uint16_t outline[][2] = {
		{228U, 1U}, {229U, 1U}, {230U, 1U}, {231U, 1U},
		{232U, 1U}, {233U, 1U}, {228U, 2U}, {233U, 2U},
		{234U, 2U}, {228U, 3U}, {234U, 3U}, {228U, 4U},
		{233U, 4U}, {234U, 4U}, {228U, 5U}, {229U, 5U},
		{230U, 5U}, {231U, 5U}, {232U, 5U}, {233U, 5U},
	};

	for (size_t index = 0U; index < ARRAY_SIZE(outline); ++index) {
		(void)ef800_t6963_write_pixel(&lcd, outline[index][0],
						      outline[index][1], true);
	}
			for (uint16_t x = 229U; x <= 233U; ++x) {
		bool filled = soc > (uint8_t)((x - 228U) * 20U);
				for (uint16_t y = 2U; y <= 4U; ++y) {
					(void)ef800_t6963_write_pixel(&lcd, x, y, filled);
				}
			}
			for (uint16_t y = 2U; y <= 4U; ++y) {
				(void)ef800_t6963_write_pixel(&lcd, 233U, y, soc > 95U);
			}
}

static void clear_battery_icon(void)
{
	for (uint16_t x = 228U; x <= 234U; ++x) {
		for (uint16_t y = 1U; y <= 5U; ++y) {
			(void)ef800_t6963_write_pixel(&lcd, x, y, false);
		}
	}
}

static void update_lcd_status(void)
{
	struct ef800_runtime_snapshot snapshot;
	static uint8_t last_soc = UINT8_MAX;

	if (!lcd_ready || !runtime_state_ready ||
		ef800_runtime_state_snapshot(&runtime_state, &snapshot) != 0) {
		return;
	}

	display_header(NULL);
	if (snapshot.battery_has_value &&
		snapshot.battery_soc_percent != last_soc) {
		draw_battery_icon(snapshot.battery_soc_percent);
		last_soc = snapshot.battery_soc_percent;
	} else if (!snapshot.battery_has_value && last_soc != UINT8_MAX) {
		clear_battery_icon();
		last_soc = UINT8_MAX;
	}
}

static const char *time_editor_prompt(enum time_editor_field field)
{
	switch (field) {
	case TIME_EDITOR_WEEKDAY:
		return "Weekday Mon-Sun";
	case TIME_EDITOR_DATE:
		return "Date 1-31";
	case TIME_EDITOR_MONTH:
		return "Month 1-12";
	case TIME_EDITOR_YEAR:
		return "Year 2000-2099";
	case TIME_EDITOR_HOUR:
		return "Hour 0-23";
	case TIME_EDITOR_MINUTE:
		return "Minute 0-59";
	case TIME_EDITOR_SECOND:
		return "Second 0-59";
	case TIME_EDITOR_OFFSET:
		return "UTC offset hours or HH:MM";
	case TIME_EDITOR_DST:
		return "DST 0=off 1=on";
	default:
		return "";
	}
}

static const char *time_editor_weekday_name(int weekday)
{
	static const char *const weekdays[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
	};

	if (weekday < 0 || weekday >= (int)ARRAY_SIZE(weekdays)) {
		return "Sun";
	}
	return weekdays[weekday];
}

static void time_editor_adjust_weekday(int direction)
{
	int weekday = time_editor.local.tm_wday;

	weekday = (weekday + direction + 7) % 7;
	time_editor.local.tm_wday = weekday;
	display_time_editor();
}

static void display_time_editor(void)
{
	char line[STATUS_LINE_SIZE];

	if (!lcd_ready) {
		return;
	}
	(void)ef800_t6963_clear_text_body(&lcd);
	display_header("RTC setup");
	snprintk(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d",
		time_editor.local.tm_year + 1900, time_editor.local.tm_mon + 1,
		time_editor.local.tm_mday, time_editor.local.tm_hour,
		time_editor.local.tm_min, time_editor.local.tm_sec);
	display_line(1U, line);
	snprintk(line, sizeof(line), "Offset %+d DST %d",
		time_editor.settings.utc_offset_minutes,
		time_editor.settings.daylight_saving ? 1 : 0);
	display_line(3U, line);
	display_line(5U, time_editor_prompt(time_editor.field));
	if (time_editor.field == TIME_EDITOR_WEEKDAY) {
		snprintk(line, sizeof(line), "> %s",
			time_editor_weekday_name(time_editor.local.tm_wday));
	} else {
		snprintk(line, sizeof(line), "> %s", time_editor.input);
	}
	display_line(6U, line);
	display_line(7U, time_editor.field == TIME_EDITOR_WEEKDAY ?
		"UP/DOWN change ENTER next" : "ENTER next  ESC cancel");
}

static void start_time_editor(void)
{
	struct rtc_time current;

	memset(&time_editor, 0, sizeof(time_editor));
	time_editor.active = true;
	time_editor.field = TIME_EDITOR_WEEKDAY;
	time_editor.settings = *ef800_main_get_time_settings();
	if (ef800_main_rtc_get_local_time(&current) == 0) {
		time_editor.local = current;
	} else {
		time_editor.local = (struct rtc_time){
			.tm_year = 124,
			.tm_mon = 0,
			.tm_mday = 1,
			.tm_hour = 0,
			.tm_min = 0,
			.tm_sec = 0,
			.tm_wday = 0,
			.tm_yday = -1,
			.tm_isdst = -1,
		};
	}
	display_time_editor();
}

static int parse_time_editor_value(int *value)
{
	char *end;
	long parsed;

	if (time_editor.length == 0U) {
		return -EINVAL;
	}
	if (time_editor.field == TIME_EDITOR_OFFSET &&
		strchr(time_editor.input, ':') != NULL) {
		int32_t offset_minutes;
		if (ef800_parse_utc_offset(time_editor.input, &offset_minutes) != 0) {
			return -EINVAL;
		}
		*value = offset_minutes;
		return 0;
	}
	parsed = strtol(time_editor.input, &end, 10);
	if (*end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
		return -EINVAL;
	}
	*value = time_editor.field == TIME_EDITOR_OFFSET ? (int)parsed * 60 :
		(int)parsed;
	return 0;
}

static int commit_time_editor_value(void)
{
	int value;
	int ret;

	if (time_editor.field == TIME_EDITOR_WEEKDAY) {
		return time_editor.local.tm_wday >= 0 &&
			time_editor.local.tm_wday <= 6 ? 0 : -ERANGE;
	}
	ret = parse_time_editor_value(&value);

	if (ret != 0) {
		return ret;
	}
	switch (time_editor.field) {
	case TIME_EDITOR_DATE:
		time_editor.local.tm_mday = value;
		break;
	case TIME_EDITOR_MONTH:
		if (value < 1 || value > 12) return -ERANGE;
		time_editor.local.tm_mon = value - 1;
		break;
	case TIME_EDITOR_YEAR:
		if (value < 2000 || value > 2099) return -ERANGE;
		time_editor.local.tm_year = value - 1900;
		break;
	case TIME_EDITOR_HOUR:
		if (value < 0 || value > 23) return -ERANGE;
		time_editor.local.tm_hour = value;
		break;
	case TIME_EDITOR_MINUTE:
		if (value < 0 || value > 59) return -ERANGE;
		time_editor.local.tm_min = value;
		break;
	case TIME_EDITOR_SECOND:
		if (value < 0 || value > 59) return -ERANGE;
		time_editor.local.tm_sec = value;
		break;
	case TIME_EDITOR_OFFSET:
		if (value < -24 * 60 || value > 24 * 60) return -ERANGE;
		time_editor.settings.utc_offset_minutes = value;
		break;
	case TIME_EDITOR_DST:
		if (value != 0 && value != 1) return -ERANGE;
		time_editor.settings.daylight_saving = value != 0;
		break;
	default:
		return -EINVAL;
	}
	/* The complete date is checked when the final DST field is submitted.
	 * This allows the user to change month and day in either order. */
	return 0;
}

static void finish_time_editor(void)
{
	int ret = ef800_main_rtc_set_local_time(&time_editor.local,
						&time_editor.settings);

	if (ret == 0) {
		LOG_INF("RTC updated from local time editor");
		ef800_runtime_state_refresh_rtc(&runtime_state);
		display_line(6U, "RTC updated successfully");
		time_editor.active = false;
		(void)ef800_t6963_clear_text_body(&lcd);
		display_banner();
		active_menu = APPLICATION_MENU_SETTINGS;
		menu_selection = 0U;
		display_settings_menu();
	} else {
		LOG_ERR("RTC update failed: %d", ret);
		display_line(6U, "RTC update failed; retry");
		time_editor.length = 0U;
		time_editor.input[0] = '\0';
	}
}

static void handle_time_editor_key(uint8_t key)
{
	int ret;

	if (key == 0x1BU) {
		time_editor.active = false;
		(void)ef800_t6963_clear_text_body(&lcd);
		display_banner();
		active_menu = APPLICATION_MENU_SETTINGS;
		menu_selection = 0U;
		display_settings_menu();
		return;
	}
	if (key == 0x08U || key == 0x7FU) {
		if (time_editor.length != 0U) {
			time_editor.input[--time_editor.length] = '\0';
			display_time_editor();
		}
		return;
	}
	if (key == '\r' || key == '\n') {
		ret = commit_time_editor_value();
		if (ret != 0) {
			display_line(6U, "Invalid value, try again");
			time_editor.length = 0U;
			time_editor.input[0] = '\0';
			return;
		}
		if (time_editor.field == TIME_EDITOR_DST) {
			finish_time_editor();
			return;
		}
		time_editor.field++;
		time_editor.length = 0U;
		time_editor.input[0] = '\0';
		display_time_editor();
		return;
	}
	if (time_editor.field == TIME_EDITOR_WEEKDAY) {
		return;
	}
	if ((key >= '0' && key <= '9') ||
		(time_editor.field == TIME_EDITOR_OFFSET && key == '+' &&
		 time_editor.length == 0U) ||
		(time_editor.field == TIME_EDITOR_OFFSET && key == ':' &&
		 time_editor.length > 0U &&
		 strchr(time_editor.input, ':') == NULL) ||
		(time_editor.field == TIME_EDITOR_OFFSET && key == '-' &&
		 time_editor.length == 0U)) {
		if (time_editor.length + 1U < sizeof(time_editor.input)) {
			time_editor.input[time_editor.length++] = (char)key;
			time_editor.input[time_editor.length] = '\0';
			display_time_editor();
		}
	}
}

static int cmd_rtc_get(const struct shell *shell, size_t argc, char **argv)
{
	struct rtc_time local;
	const struct ef800_time_settings *settings;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (ef800_main_rtc_get_local_time(&local) != 0) {
		shell_error(shell, "RTC unavailable");
		return -ENODEV;
	}
	settings = ef800_main_get_time_settings();
	shell_print(shell, "%04d-%02d-%02dT%02d:%02d:%02d offset=%d dst=%d",
		local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
		local.tm_hour, local.tm_min, local.tm_sec,
		settings->utc_offset_minutes, settings->daylight_saving ? 1 : 0);
	return 0;
}

static int cmd_rtc_set(const struct shell *shell, size_t argc, char **argv)
{
	struct rtc_time local = {
		.tm_wday = 0,
		.tm_yday = -1,
		.tm_isdst = -1,
	};
	struct ef800_time_settings settings;
	int year;
	int month;
	int offset;
	int dst;
	char *offset_end;
	int time_fields;

	if (argc != 4) {
		shell_error(shell,
			"usage: rtc set YYYY-MM-DDTHH:MM:SS offset_hours_or_HH:MM dst");
		return -EINVAL;
	}
	time_fields = sscanf(argv[1], "%d-%d-%dT%d:%d:%d",
		&year, &month, &local.tm_mday, &local.tm_hour,
		&local.tm_min, &local.tm_sec);
	offset = (int)strtol(argv[2], &offset_end, 10);
	if (time_fields != 6 ||
		(*offset_end != '\0' && strchr(argv[2], ':') == NULL) ||
		sscanf(argv[3], "%d", &dst) != 1) {
		shell_error(shell,
			"usage: rtc set YYYY-MM-DDTHH:MM:SS offset_hours_or_HH:MM dst");
		return -EINVAL;
	}
	local.tm_year = year - 1900;
	local.tm_mon = month - 1;
	if (strchr(argv[2], ':') != NULL) {
		int32_t parsed_offset;
		if (ef800_parse_utc_offset(argv[2], &parsed_offset) != 0) {
			shell_error(shell, "invalid UTC offset");
			return -EINVAL;
		}
		offset = parsed_offset;
	} else {
		if (offset < -24 || offset > 24) {
			shell_error(shell, "UTC offset hours must be between -24 and 24");
			return -EINVAL;
		}
		offset *= 60;
	}
	settings.utc_offset_minutes = offset;
	settings.daylight_saving = dst != 0;
	if ((dst != 0 && dst != 1) ||
		!ef800_rtc_time_valid(&local) ||
		offset < -24 * 60 || offset > 24 * 60) {
		shell_error(shell, "invalid date/time, offset, or DST value");
		return -EINVAL;
	}
	if (ef800_main_rtc_set_local_time(&local, &settings) != 0) {
		shell_error(shell, "RTC update failed");
		return -EIO;
	}
	shell_print(shell, "RTC updated");
	return 0;
}

static int cmd_keyboard_version(const struct shell *shell, size_t argc,
				const char *argv[])
{
	uint8_t request = EF800_KEYBOARD_I2C_CMD_STATUS;
	uint8_t response[EF800_KEYBOARD_I2C_STATUS_SIZE];
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	k_mutex_lock(&keyboard_command_mutex, K_FOREVER);
	if (!device_is_ready(keyboard_i2c)) {
		result = -ENODEV;
	} else {
		result = i2c_write_read(keyboard_i2c,
			EF800_KEYBOARD_I2C_ADDRESS, &request, sizeof(request),
			response, sizeof(response));
	}
	if (result == 0 && response[0] == EF800_KEYBOARD_I2C_CMD_STATUS &&
	    response[4] == EF800_KEYBOARD_I2C_MODE_APPLICATION &&
	    response[5] == EF800_KEYBOARD_I2C_ACK) {
		shell_print(shell, "Keyboard firmware version %u.%u.%u",
			response[1], response[2], response[3]);
	} else {
		result = result != 0 ? result : -EBADMSG;
		shell_error(shell, "Keyboard I2C version query failed: %d", result);
	}
	k_mutex_unlock(&keyboard_command_mutex);
	return result;
}

static int cmd_keyboard_updater_version(const struct shell *shell, size_t argc,
					       const char *argv[])
{
	uint8_t response[EF800_KB_UPDATE_VERSION_SIZE];
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!device_is_ready(keyboard_i2c)) {
		result = -ENODEV;
	} else {
		result = keyboard_update_query_version(
			EF800_KEYBOARD_UPDATER_I2C_ADDRESS, response, 2000U);
	}
	if (result == 0) {
		shell_print(shell, "Keyboard updater version %u.%u.%u",
			response[1], response[2], response[3]);
		return 0;
	}
	result = result != 0 ? result : -EBADMSG;
	shell_error(shell, "Keyboard updater I2C version query failed: %d", result);
	return result;
}

static int cmd_keyboard_boot_probe(const struct shell *shell, size_t argc,
				  const char *argv[])
{
	uint8_t version[EF800_KB_UPDATE_VERSION_SIZE] = {0};
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(shell, "Asserting JP7 updater request and resetting keyboard");
	result = keyboard_update_enter_bootloader_fallback(version);
	if (result == 0) {
		shell_print(shell, "Bootloader strap PASS; updater version %u.%u.%u",
			version[1], version[2], version[3]);
		return 0;
	}
	shell_error(shell, "bootloader strap handoff failed: %d", result);
	return result;
}

static int cmd_keyboard_debug_start(const struct shell *shell,
					    size_t argc, char **argv)
{
	uint8_t enabled = 1U;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (keyboard_debug_active) {
		shell_print(shell, "keyboard debug already active");
		return 0;
	}
	result = keyboard_send_command(CMD_KEYBOARD_DEACTIVATE, NULL, 0U);
	if (result != 0) {
		shell_error(shell, "keyboard deactivate failed: %d", result);
		return result;
	}
	result = keyboard_send_command(CMD_KEYBOARD_SET_DEBUG, &enabled, 1U);
	if (result != 0) {
		shell_error(shell, "keyboard debug enable failed: %d", result);
		return result;
	}
	keyboard_debug_active = true;
	shell_print(shell, "keyboard debug active");
	return 0;
}

static int cmd_keyboard_debug_key(const struct shell *shell,
					 size_t argc, char **argv, uint8_t action)
{
	int result;

	if (argc != 2U) {
		shell_error(shell, "usage: keyboard debug %s <key-name>",
			action == KB_DEBUG_ACTION_PRESS ? "press" : "release");
		return -EINVAL;
	}
	result = keyboard_send_virtual_key(action, argv[1]);
	if (result != 0) {
		shell_error(shell, "keyboard debug request failed: %d", result);
	}
	return result;
}

static int cmd_keyboard_debug_press(const struct shell *shell,
					    size_t argc, char **argv)
{
	return cmd_keyboard_debug_key(shell, argc, argv, KB_DEBUG_ACTION_PRESS);
}

static int cmd_keyboard_debug_release(const struct shell *shell,
					      size_t argc, char **argv)
{
	return cmd_keyboard_debug_key(shell, argc, argv, KB_DEBUG_ACTION_RELEASE);
}

static int cmd_keyboard_debug_stop(const struct shell *shell,
					   size_t argc, char **argv)
{
	uint8_t disabled = 0U;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!keyboard_debug_active) {
		shell_print(shell, "keyboard debug is not active");
		return 0;
	}
	result = keyboard_send_command(CMD_KEYBOARD_SET_DEBUG, &disabled, 1U);
	if (result != 0) {
		shell_error(shell, "keyboard debug disable failed: %d", result);
		return result;
	}
	result = keyboard_send_command(CMD_KEYBOARD_ACTIVATE, NULL, 0U);
	if (result != 0) {
		shell_error(shell, "keyboard activate failed: %d", result);
		return result;
	}
	keyboard_debug_active = false;
	shell_print(shell, "keyboard debug stopped");
	return 0;
}

static int cmd_battery_soc_table_show(const struct shell *shell, size_t argc,
	char **argv)
{
	struct ef800_battery_soc_table table;
	char hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	size_t hex_length;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!battery_soc_store_ready) {
		shell_error(shell, "battery SOC calibration storage unavailable");
		return -ENOTSUP;
	}
	ret = ef800_battery_soc_store_get_table(&battery_soc_store, &table);
	if (ret == -ENOENT) {
		shell_print(shell, "no battery SOC calibration table is installed");
		return 0;
	}
	if (ret != 0) {
		shell_error(shell, "battery SOC table read failed: %d", ret);
		return ret;
	}
	ret = ef800_battery_soc_store_get_hex(&battery_soc_store, hex, sizeof(hex),
		&hex_length);
	if (ret != 0) {
		shell_error(shell, "battery SOC table encoding failed: %d", ret);
		return ret;
	}
	shell_print(shell, "battery SOC table chemistry=%u points=%u",
		(unsigned int)table.chemistry, (unsigned int)table.point_count);
	for (size_t index = 0U; index < table.point_count; ++index) {
		shell_print(shell, "%u mV -> %u%%",
			(unsigned int)table.points[index].pin_mv,
			(unsigned int)table.points[index].soc_percent);
	}
	shell_print(shell, "hex=%s", hex);
	ARG_UNUSED(hex_length);
	return 0;
}

static int cmd_mcumgr_start(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argv);
	if (argc != 1U) {
		shell_error(shell, "usage: mcumgr start");
		return -EINVAL;
	}
	if (mcumgr_transport_active) {
		shell_print(shell, "MCUmgr raw FS transport is already active");
		return 0;
	}
	if (!ef800_storage_is_mounted()) {
		shell_error(shell, "MCUmgr requires mounted EF800 storage");
		return -ENOTSUP;
	}
	ret = ef800_mcumgr_transport_init();
	if (ret != 0) {
		shell_error(shell, "MCUmgr transport start failed: %d", ret);
		return ret;
	}
	mcumgr_transport_active = true;
	shell_print(shell,
		"MCUmgr raw FS transport active; reset MainApplication to restore shell input");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mcumgr_commands,
	SHELL_CMD_ARG(start, NULL, "Start raw serial FS transport (shell pauses)",
		cmd_mcumgr_start, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mcumgr, &mcumgr_commands,
	"Raw serial filesystem transport", NULL);

static int cmd_battery_soc_table_set(const struct shell *shell, size_t argc,
	char **argv)
{
	int ret;

	if (argc != 2U) {
		shell_error(shell, "usage: battery table set <hex-string>");
		return -EINVAL;
	}
	if (!battery_soc_store_ready || !ef800_storage_configuration_is_mounted()) {
		shell_error(shell, "battery SOC calibration storage unavailable");
		return -ENOTSUP;
	}
	ret = ef800_battery_soc_store_set_hex(&battery_soc_store, argv[1],
		strlen(argv[1]));
	if (ret != 0) {
		shell_error(shell, "battery SOC table rejected or not stored: %d", ret);
		return ret;
	}
	shell_print(shell, "battery SOC table verified and stored");
	return 0;
}

static int cmd_battery_soc_source_get(const struct shell *shell, size_t argc,
	char **argv)
{
	const struct ef800_composition_catalog_entry *entry;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!composition_service_ready) {
		shell_error(shell, "COMPOSITION service unavailable");
		return -ENOTSUP;
	}
	entry = battery_soc_composition_entry();
	shell_print(shell, "active BATSOC source: %s%s",
		entry != NULL ? entry->token : "BATSOC2",
		ef800_composition_service_restart_required(&composition_service) ?
			" (restart required for committed selection)" : "");
	return 0;
}

static int cmd_battery_soc_source_set(const struct shell *shell, size_t argc,
	char **argv)
{
	const struct ef800_composition_catalog_entry *active;
	int ret;

	if (argc != 2U || (strcmp(argv[1], "BATSOC0") != 0 &&
		strcmp(argv[1], "BATSOC1") != 0 &&
		strcmp(argv[1], "BATSOC2") != 0)) {
		shell_error(shell, "usage: battery source set <BATSOC0|BATSOC1|BATSOC2>");
		return -EINVAL;
	}
	if (!composition_service_ready ||
	    !ef800_storage_configuration_is_mounted()) {
		shell_error(shell, "COMPOSITION Configuration storage unavailable");
		return -ENOTSUP;
	}
	if (ef800_composition_service_restart_required(&composition_service)) {
		shell_error(shell, "a COMPOSITION change is already committed; restart first");
		return -EALREADY;
	}
	active = battery_soc_composition_entry();
	if (active != NULL && strcmp(active->token, argv[1]) == 0) {
		shell_print(shell, "%s is already active", argv[1]);
		return 0;
	}
	ret = ef800_composition_service_stage_group_selection(&composition_service,
		"BATSOC", argv[1]);
	if (ret == 0) {
		ret = ef800_composition_service_commit(&composition_service);
	}
	if (ret != 0) {
		ef800_composition_service_discard(&composition_service);
		shell_error(shell, "BATSOC source selection failed: %d", ret);
		return ret;
	}
#ifdef CONFIG_BOARD_NATIVE_SIM
	publish_virtual_composition_state();
#endif
	shell_print(shell, "%s committed; restart required", argv[1]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(battery_source_commands,
	SHELL_CMD_ARG(get, NULL, "Show active BATSOC source",
		cmd_battery_soc_source_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, "Stage and commit a BATSOC source",
		cmd_battery_soc_source_set, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(battery_table_commands,
	SHELL_CMD_ARG(show, NULL, "Show active battery SOC calibration table",
		cmd_battery_soc_table_show, 1, 0),
	SHELL_CMD_ARG(set, NULL, "Set battery SOC calibration from a hex string",
		cmd_battery_soc_table_set, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(battery_commands,
	SHELL_CMD(table, &battery_table_commands, "ADC SOC lookup-table calibration", NULL),
	SHELL_CMD(source, &battery_source_commands, "BATSOC COMPOSITION selection", NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(battery, &battery_commands, "Battery configuration", NULL);

static int cmd_usb_msd_get(const struct shell *shell, size_t argc, char **argv)
{
	const struct ef800_composition_snapshot *snapshot;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!composition_service_ready) {
		shell_error(shell, "COMPOSITION service unavailable");
		return -ENOTSUP;
	}
	snapshot = ef800_composition_service_active(&composition_service);
	if (snapshot != NULL) {
		for (size_t index = 0U; index < snapshot->effective_count; ++index) {
			const struct ef800_composition_catalog_entry *entry =
				snapshot->effective[index].catalog_entry;

			if (entry != NULL && strcmp(entry->group, "USBMSD") == 0) {
				shell_print(shell, "active USBMSD mode: %s%s", entry->token,
					ef800_composition_service_restart_required(
						&composition_service) ? " (restart required)" : "");
				return 0;
			}
		}
	}
	shell_error(shell, "USBMSD composition selection unavailable");
	return -ENODEV;
}

static int cmd_usb_msd_mode_set(const struct shell *shell, size_t argc,
	char **argv)
{
	const struct ef800_composition_snapshot *snapshot;
	int ret;

	if (argc != 2U || (strcmp(argv[1], "USBMSD0") != 0 &&
		strcmp(argv[1], "USBMSD1") != 0)) {
		shell_error(shell, "usage: usbmsd mode <USBMSD0|USBMSD1>");
		return -EINVAL;
	}
	if (!composition_service_ready ||
	    !ef800_storage_configuration_is_mounted()) {
		shell_error(shell, "COMPOSITION Configuration storage unavailable");
		return -ENOTSUP;
	}
	if (ef800_composition_service_restart_required(&composition_service)) {
		shell_error(shell, "a COMPOSITION change is already committed; restart first");
		return -EALREADY;
	}
	snapshot = ef800_composition_service_active(&composition_service);
	if (snapshot != NULL) {
		for (size_t index = 0U; index < snapshot->effective_count; ++index) {
			const struct ef800_composition_catalog_entry *entry =
				snapshot->effective[index].catalog_entry;

			if (entry != NULL && strcmp(entry->group, "USBMSD") == 0 &&
			    strcmp(entry->token, argv[1]) == 0) {
				shell_print(shell, "%s is already active", argv[1]);
				return 0;
			}
		}
	}
	ret = ef800_composition_service_stage_group_selection(&composition_service,
		"USBMSD", argv[1]);
	if (ret == 0) {
		ret = ef800_composition_service_commit(&composition_service);
	}
	if (ret != 0) {
		ef800_composition_service_discard(&composition_service);
		shell_error(shell, "USBMSD composition commit failed: %d", ret);
		return ret;
	}
#ifdef CONFIG_BOARD_NATIVE_SIM
	publish_virtual_composition_state();
#endif
	shell_print(shell, "%s committed; restart required", argv[1]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(usb_msd_commands,
	SHELL_CMD_ARG(get, NULL, "Show active USB MSD composition mode",
		cmd_usb_msd_get, 1, 0),
	SHELL_CMD_ARG(mode, NULL, "Select USBMSD0 (off) or USBMSD1 (on)",
		cmd_usb_msd_mode_set, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(usbmsd, &usb_msd_commands,
	"USB Mass Storage COMPOSITION selection", NULL);

static int cmd_storage_format_qspi(const struct shell *shell, size_t argc,
	char **argv)
{
	int ret;

	if (argc == 1U) {
		qspi_format_confirmation_pending = true;
		qspi_format_confirmation_deadline_ms = k_uptime_get() + 30000;
		shell_warn(shell, "This erases the entire 4 MiB QSPI: boot-state NVS and both FAT volumes.");
		shell_print(shell, "It recreates and verifies Configuration and Shared Data FAT volumes.");
		shell_print(shell, "Type 'storage format-qspi confirm' within 30 seconds to continue.");
		return 0;
	}
	if (argc != 2U) {
		shell_error(shell, "usage: storage format-qspi [confirm|cancel]");
		return -EINVAL;
	}
	if (strcmp(argv[1], "cancel") == 0) {
		qspi_format_confirmation_pending = false;
		shell_print(shell, "QSPI format cancelled; no media changes made.");
		return 0;
	}
	if (strcmp(argv[1], "confirm") != 0 || !qspi_format_confirmation_pending ||
	    k_uptime_get() > qspi_format_confirmation_deadline_ms) {
		qspi_format_confirmation_pending = false;
		shell_error(shell, "confirmation missing or expired; run storage format-qspi first");
		return -EACCES;
	}
	qspi_format_confirmation_pending = false;

	if (ef800_main_usb_msd_requested() ||
	    ef800_usb_manager_get() == EF800_USB_PERSONALITY_MSC) {
		shell_error(shell, "USB MSC owns the shared disk; disconnect/disable MSC before formatting");
		return -EBUSY;
	}
	if (ef800_usb_manager_get() != EF800_USB_PERSONALITY_NONE) {
		ret = ef800_usb_manager_set(EF800_USB_PERSONALITY_NONE);
		if (ret != 0) {
			shell_error(shell, "USB storage shutdown failed: %d", ret);
			return ret;
		}
	}
	if (runtime_state_ready) {
		ef800_runtime_state_stop(&runtime_state);
	}
	ret = ef800_storage_unmount_all();
	if (ret != 0) {
		shell_error(shell, "QSPI unmount failed; nothing erased: %d", ret);
		return ret;
	}
	ret = ef800_storage_format_qspi_layout();
	if (ret != 0) {
		shell_error(shell, "QSPI erase/format/verification failed: %d; do not use the volumes",
			ret);
		return ret;
	}
	shell_print(shell, "QSPI erase and both FAT boot sectors verified; rebooting.");
	k_msleep(100U);
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(storage_commands,
	SHELL_CMD_ARG(format-qspi, NULL,
		"Erase all QSPI and recreate both FAT volumes (requires confirmation)",
		cmd_storage_format_qspi, 1, 1),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(storage, &storage_commands,
	"External QSPI storage maintenance", NULL);

static int cmd_lcdcontrast_get(const struct shell *shell, size_t argc, char **argv)
{
	enum ef800_lcd_contrast_mode mode;
	uint8_t duty_percent;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = ef800_lcd_contrast_get(&lcd_contrast, &mode, &duty_percent);
	if (ret != 0) {
		shell_error(shell, "LCD contrast state unavailable: %d", ret);
		return ret;
	}
	if (mode != EF800_LCD_CONTRAST_PWM) {
		shell_print(shell, "LCDCONTR0: static contrast; PWM is disabled");
	} else {
		shell_print(shell, "LCDCONTR1: contrast %u%%", duty_percent);
	}
	return 0;
}

static int cmd_lcdcontrast_set(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t duty_percent;
	int ret;

	if (argc != 2U) {
		shell_error(shell, "usage: lcdcontrast set <0..100>");
		return -EINVAL;
	}
	ret = ef800_lcd_contrast_parse_duty(argv[1], strlen(argv[1]),
		&duty_percent);
	if (ret != 0) {
		shell_error(shell, "contrast duty must be an integer from 0 to 100");
		return -EINVAL;
	}
	ret = ef800_lcd_contrast_set_duty(&lcd_contrast, duty_percent);
	if (ret == -EACCES) {
		shell_error(shell, "LCDCONTR0 is active; contrast is not controllable");
		return ret;
	}
	if (ret != 0) {
		shell_error(shell, "contrast update failed: %d", ret);
		return ret;
	}
	shell_print(shell, "LCD contrast set to %u%%", (unsigned int)duty_percent);
	return 0;
}

static int cmd_lcdcontrast_mode(const struct shell *shell, size_t argc, char **argv)
{
	const struct ef800_composition_snapshot *active;
	int ret;

	if (argc != 2U || (strcmp(argv[1], "LCDCONTR0") != 0 &&
		strcmp(argv[1], "LCDCONTR1") != 0)) {
		shell_error(shell, "usage: lcdcontrast mode <LCDCONTR0|LCDCONTR1>");
		return -EINVAL;
	}
	if (!composition_service_ready ||
	    !ef800_storage_configuration_is_mounted()) {
		shell_error(shell, "COMPOSITION Configuration storage unavailable");
		return -ENOTSUP;
	}
	if (ef800_composition_service_restart_required(&composition_service)) {
		shell_error(shell, "a COMPOSITION change is already committed; restart first");
		return -EALREADY;
	}
	active = ef800_composition_service_active(&composition_service);
	if (active == NULL) {
		return -ENODEV;
	}
	if (lcd_contrast_composition_mode() ==
	    (strcmp(argv[1], "LCDCONTR1") == 0 ? EF800_LCD_CONTRAST_PWM :
		EF800_LCD_CONTRAST_STATIC)) {
		shell_print(shell, "%s is already active", argv[1]);
		return 0;
	}
	ret = ef800_composition_service_stage_group_selection(&composition_service,
		"LCDCONTR", argv[1]);
	if (ret != 0) {
		shell_error(shell, "LCDCONTR composition staging failed: %d", ret);
		return ret;
	}
	ret = ef800_composition_service_commit(&composition_service);
	if (ret != 0) {
		shell_error(shell, "LCDCONTR composition commit failed: %d", ret);
		return ret;
	}
	shell_print(shell, "%s committed; restart required", argv[1]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(lcdcontrast_commands,
	SHELL_CMD_ARG(get, NULL, "Show active LCD contrast mode and duty",
		cmd_lcdcontrast_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, "Set LCD contrast duty (0..100)",
		cmd_lcdcontrast_set, 2, 0),
	SHELL_CMD_ARG(mode, NULL, "Stage LCDCONTR0/1 composition (restart required)",
		cmd_lcdcontrast_mode, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(lcdcontrast, &lcdcontrast_commands,
	"LCD contrast controls", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(keyboard_debug_commands,
	SHELL_CMD_ARG(start, NULL, "Disable physical scanning and enable debug input",
			cmd_keyboard_debug_start, 1, 0),
	SHELL_CMD_ARG(press, NULL, "Press a named virtual key",
			cmd_keyboard_debug_press, 2, 0),
	SHELL_CMD_ARG(release, NULL, "Release a named virtual key",
			cmd_keyboard_debug_release, 2, 0),
	SHELL_CMD_ARG(stop, NULL, "Release held virtual keys and restore scanning",
			cmd_keyboard_debug_stop, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(keyboard_commands,
	SHELL_CMD_ARG(version, NULL, "Read keyboard firmware version",
			cmd_keyboard_version, 1, 0),
	SHELL_CMD_ARG(updater-version, NULL, "Read live updater version at 0x48",
			cmd_keyboard_updater_version, 1, 0),
	SHELL_CMD_ARG(boot-probe, NULL,
			"Reset keyboard and test bootloader 0x48 updater entry",
			cmd_keyboard_boot_probe, 1, 0),
	SHELL_CMD_ARG(upload, NULL, "Upload EF800_KEYBOARD.pkg via DebugConsole",
			cmd_keyboard_upload, 1, 0),
	SHELL_CMD(debug, &keyboard_debug_commands, "Keyboard debug input", NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(keyboard, &keyboard_commands, "Keyboard commands", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(rtc_commands,
	SHELL_CMD_ARG(get, NULL, "Read local RTC time", cmd_rtc_get, 1, 0),
	SHELL_CMD_ARG(set, NULL,
		"Set local time, offset hours/minutes, and DST flag",
		cmd_rtc_set, 4, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(rtc, &rtc_commands, "EF800 RTC commands", NULL);

static void handle_keyboard_event(uint32_t event)
{
#ifdef CONFIG_BOARD_NATIVE_SIM
	if (virtual_power_off) {
		if ((event & KB_EVENT_ON) != 0U) {
			LOG_INF("Virtual EF800 power on: restarting MainApplication");
			exit(42);
		}
		return;
	}
	if ((event & KB_EVENT_ON) != 0U) {
		/* The ON key is inert while the virtual target is already powered. */
		return;
	}
	if ((event & KB_EVENT_OFF) != 0U) {
		int text_clear = lcd_ready ? ef800_t6963_clear_text(&lcd) : -ENODEV;
		int graphics_clear = lcd_ready ? ef800_t6963_clear_graphic(&lcd) : -ENODEV;

		virtual_power_off = true;
		if (text_clear != 0 || graphics_clear != 0) {
			LOG_WRN("Virtual EF800 power-off LCD clear failed: text=%d graphics=%d",
				text_clear, graphics_clear);
		}
		LOG_INF("Virtual EF800 powered off; waiting for ON");
		return;
	}
#else
	if ((event & KB_EVENT_ON) != 0U) {
		return;
	}
	if (active_menu == APPLICATION_MENU_MEASUREMENT_ACTIONS &&
		measurement_plugin_session.state == EF800_MEASUREMENT_SESSION_ACTIVE) {
		/* Arrow events navigate the host-owned action list; activation remains host-owned. */
		if ((event & KB_EVENT_ARROWUP) != 0U ||
			(event & KB_EVENT_ARROWDOWN) != 0U ||
			(event & KB_EVENT_ARROWRIGHT) != 0U ||
			(event & KB_EVENT_ARROWLEFT) != 0U) {
			/* Continue through the normal precedence path below. */
		}
	}
	if ((event & KB_EVENT_OFF) != 0U) {
		int ret = power_down.port == NULL ? -ENODEV :
			gpio_pin_set_dt(&power_down, 1);

		if (ret != 0) {
			LOG_WRN("Keyboard OFF power-down output failed: %d", ret);
		} else {
			LOG_INF("Keyboard OFF: PB5 power-down output asserted");
			display_line(6U, "Keyboard OFF: power-down");
		}
		return;
	}
#endif
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (plugin_async_ui_pending) {
		return;
	}
	if (active_menu == APPLICATION_MENU_PLUGIN_FAILURE) {
		if ((event & (KB_EVENT_ARROWLEFT | KB_EVENT_ARROWRIGHT)) != 0U) {
			return_from_plugin_failure();
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_CALCULATION_CONFIRMATION) {
		if ((event & KB_EVENT_ARROWRIGHT) != 0U) {
			finish_calculation_confirmation(true);
		} else if ((event & KB_EVENT_ARROWLEFT) != 0U) {
			finish_calculation_confirmation(false);
		}
		return;
	}
#endif
	if (active_menu == APPLICATION_MENU_WORKSPACE_CREATE) {
		if ((event & KB_EVENT_ARROWLEFT) != 0U) {
			return_to_workspace_management_menu();
		}
		/* Name entry is modal. Other navigation events must not activate or
		 * move through the menu behind the editor. */
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_ROW_EDITOR) {
		if ((event & KB_EVENT_INSERT) != 0U) {
			workspace_row_editor.insert_mode = !workspace_row_editor.insert_mode;
		}
		if ((event & KB_EVENT_ARROWLEFT) != 0U &&
			workspace_row_editor.caret > 0U) {
			--workspace_row_editor.caret;
		}
		if ((event & KB_EVENT_ARROWRIGHT) != 0U &&
			workspace_row_editor.caret < workspace_row_editor.length) {
			++workspace_row_editor.caret;
		}
		if ((event & (KB_EVENT_INSERT | KB_EVENT_ARROWLEFT |
			KB_EVENT_ARROWRIGHT)) != 0U) {
			display_workspace_row_editor();
		}
		/* The row editor owns its arrows and Insert event. */
		return;
	}
	if (time_editor.active) {
		if (time_editor.field == TIME_EDITOR_WEEKDAY) {
			if ((event & KB_EVENT_ARROWUP) != 0U) {
				time_editor_adjust_weekday(-1);
			} else if ((event & KB_EVENT_ARROWDOWN) != 0U) {
				time_editor_adjust_weekday(1);
			}
		}
		return;
	}
	if (measurement_editor.active) {
		if ((event & KB_EVENT_ARROWLEFT) != 0U) {
			return_from_child_menu();
		}
		return;
	}
	if (position_editor.active) {
		if ((event & KB_EVENT_ARROWLEFT) != 0U) {
			return_from_position_editor();
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_LCD_CONTRAST) {
		if ((event & (KB_EVENT_ARROWUP | KB_EVENT_ARROWRIGHT)) != 0U) {
			adjust_lcd_contrast(1);
		} else if ((event & KB_EVENT_ARROWDOWN) != 0U) {
			adjust_lcd_contrast(-1);
		} else if ((event & KB_EVENT_ARROWLEFT) != 0U) {
			return_from_child_menu();
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_VIEW) {
		int direction = (event & KB_EVENT_ARROWUP) != 0U ? -1 :
			(event & KB_EVENT_ARROWDOWN) != 0U ? 1 : 0;
		if (direction != 0 && workspace_view_move(direction) == 0) {
			display_workspace_view();
		}
		if (direction != 0) {
			return;
		}
	}
	if (active_menu == APPLICATION_MENU_MEASUREMENT_ACTIONS &&
		measurement_plugin_session.state == EF800_MEASUREMENT_SESSION_ACTIVE) {
		uint32_t input = (event & KB_EVENT_ARROWUP) != 0U ?
			EF800_MEASUREMENT_INPUT_UP :
			(event & KB_EVENT_ARROWDOWN) != 0U ? EF800_MEASUREMENT_INPUT_DOWN :
		(event & KB_EVENT_ARROWRIGHT) != 0U ? EF800_MEASUREMENT_INPUT_ENTER : 0U;
		if (input != 0U) {
			#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
			if (input == EF800_MEASUREMENT_INPUT_ENTER) {
				const struct ef800_plugin_async_request request = {
					.operation = EF800_PLUGIN_ASYNC_SESSION_KEY,
					.session = &measurement_plugin_session,
					.key = '\r',
				};
				(void)submit_plugin_async(&request);
			} else {
				const struct ef800_plugin_async_request request = {
					.operation = EF800_PLUGIN_ASYNC_SESSION_INPUT,
					.session = &measurement_plugin_session,
					.input = input,
				};
				(void)submit_plugin_async(&request);
			}
			return;
			#else
			int result;
			if (input == EF800_MEASUREMENT_INPUT_ENTER) {
				bool consumed = false;
				result = ef800_plugin_host_measurement_session_key(
					&measurement_plugin_session, '\r', &consumed);
				if (result == 0 && consumed) append_plugin_workspace_record();
			} else {
				result = ef800_plugin_host_measurement_session_input(
					&measurement_plugin_session, input);
			}
			display_active_menu();
			return;
			#endif
		}
	}
	if ((event & KB_EVENT_ARROWUP) != 0U) {
		move_menu_selection(-1);
		return;
	}
	if ((event & KB_EVENT_ARROWDOWN) != 0U) {
		move_menu_selection(1);
		return;
	}
	if ((event & KB_EVENT_ARROWLEFT) != 0U) {
		if (active_menu != APPLICATION_MENU_MAIN) {
			LOG_INF("Menu back");
			return_from_child_menu();
		}
		return;
	}
	if ((event & KB_EVENT_ARROWRIGHT) != 0U) {
		activate_menu_entry();
		return;
	}

	LOG_INF("Keyboard event: 0x%08x", event);
}

static void keyboard_uart_isr(const struct device *dev, void *user_data)
{
	uint8_t byte;

	ARG_UNUSED(user_data);
	if (uart_irq_update(dev) <= 0 || !uart_irq_rx_ready(dev)) {
		return;
	}
	while (uart_fifo_read(dev, &byte, 1U) == 1) {
		if (byte == 0xAAU) {
			atomic_set(&keyboard_ready_indication, 1);
		}
		(void)k_msgq_put(&keyboard_rx_msgq, &byte, K_NO_WAIT);
	}
}

static void process_keyboard_output(void)
{
	uint8_t key;
	uint32_t event;

	while (ef800_keyboard_host_get_key(&keyboard_host, &key) == 0) {
#ifdef CONFIG_BOARD_NATIVE_SIM
		if (virtual_power_off) {
			continue;
		}
#endif
		if (time_editor.active) {
			handle_time_editor_key(key);
			continue;
		}
		LOG_INF("Keyboard key: 0x%02x", key);
		if (lcd_ready) {
			char line[STATUS_LINE_SIZE];
			snprintk(line, sizeof(line), "Key 0x%02x '%c'", key,
				(key >= 0x20U && key < 0x7FU) ? key : '.');
			display_line(7U, line);
		}
		(void)handle_application_key(key);
	}
	while (ef800_keyboard_host_get_event(&keyboard_host, &event) == 0) {
		handle_keyboard_event(event);
	}
	if (ef800_keyboard_host_take_overflow(&keyboard_host)) {
		LOG_WRN("Keyboard host output queue overflowed");
	}
}

static void process_keyboard_byte(uint8_t byte, uint32_t now_ms)
{
	enum ef800_host_feed_result result;

	if (keyboard_command_pending &&
	    keyboard_pending_command == CMD_KEYBOARD_VERSION) {
		LOG_INF("Keyboard version query: RX byte 0x%02x", byte);
	}
	result = ef800_keyboard_host_feed_at(&keyboard_host, byte, now_ms);
	switch (result) {
	case EF800_HOST_FEED_READY:
		LOG_INF("Keyboard host ready indication received");
		break;
	case EF800_HOST_FEED_COMMAND:
		LOG_DBG("Keyboard command 0x%02x status 0x%02x",
			keyboard_host.last_command,
			keyboard_host.last_command_status);
		if (keyboard_command_pending &&
			keyboard_host.last_command == keyboard_pending_command) {
			k_sem_give(&keyboard_command_complete);
		}
		break;
	case EF800_HOST_FEED_ECHO:
		LOG_DBG("Keyboard echo indication received");
		break;
	case EF800_HOST_FEED_ERROR:
		LOG_WRN("Keyboard host rejected byte 0x%02x", byte);
		break;
	default:
		break;
	}

	process_keyboard_output();
}

static bool wait_for_keyboard_startup(void)
{
	uint8_t byte;
	int64_t deadline = k_uptime_get() +
		CONFIG_EF800_MAIN_KEYBOARD_STARTUP_TIMEOUT_MS;

	while (!ef800_keyboard_host_is_ready(&keyboard_host) &&
	       k_uptime_get() < deadline) {
		while (k_msgq_get(&keyboard_rx_msgq, &byte, K_NO_WAIT) == 0) {
			process_keyboard_byte(byte, k_uptime_get_32());
		}
		k_msleep(10);
	}

	if (!ef800_keyboard_host_is_ready(&keyboard_host)) {
		LOG_WRN("Keyboard startup timed out after %d ms; continuing",
			CONFIG_EF800_MAIN_KEYBOARD_STARTUP_TIMEOUT_MS);
		return false;
	}

	LOG_INF("Keyboard startup completed");
	return true;
}

int main(void)
{
	uint8_t byte;
	bool mcp_controls_ready = false;
	bool keyboard_rx_armed = false;
	bool startup_mtp_pending = false;
	bool plugin_discovery_ready = false;
	int result;
	uint16_t mcp_port;

	/* Buffer output until the persisted port assignments select DebugConsole. */
	ef800_ui_init(&ui_state);
	(void)ef800_console_prepare();
	/* Select the persisted target before slow foundation/QSPI initialization;
	 * reconciliation below will correct it after all assignments are loaded. */
	(void)ef800_console_init(false);
	{
		static const char diagnostic[] = "console initial target selected\r\n";
		(void)ef800_console_write((const uint8_t *)diagnostic, sizeof(diagnostic) - 1U);
	}
	(void)ef800_shell_init();
	printk("ef800 main application: entry\r\n");

	(void)ef800_main_foundation_init();
#ifdef CONFIG_BOARD_NATIVE_SIM
	if (ef800_main_usb_msd_requested() || !ef800_storage_data_is_mounted()) {
		LOG_ERR("USBMSD0 invariant failed: MSC request=%u data-mounted=%u",
			ef800_main_usb_msd_requested(),
			ef800_storage_data_is_mounted());
	} else {
		LOG_INF("USBMSD0 verified: Main MSC excluded; shared-data FAT remains mounted");
	}
#endif
	ef800_plugin_registry_init(&plugin_registry);
	result = ef800_plugin_registry_register_builtins(&plugin_registry);
	if (result != 0) {
		LOG_ERR("Built-in plugin registry initialization failed: %d", result);
	}
#ifdef CONFIG_BOARD_NATIVE_SIM
	ef800_virtual_frontpanel_set_composition_callback(
		virtual_set_composition, NULL);
#endif
	result = ef800_persistence_init(&persistence,
				       ef800_persistence_zephyr_backend());
		if (result != 0) {
			LOG_ERR("Persistence service initialization failed: %d", result);
#ifdef CONFIG_BOARD_NATIVE_SIM
			publish_virtual_composition_state();
#endif
		} else {
			result = ef800_battery_soc_store_init(&battery_soc_store,
				&persistence);
			if (result == 0) {
				battery_soc_store_ready = true;
				if (ef800_battery_soc_store_has_table(&battery_soc_store)) {
					LOG_INF("Battery SOC calibration table generation %u loaded",
						(unsigned int)battery_soc_store.generation);
				} else if (battery_soc_store.load_error != 0 &&
					   battery_soc_store.load_error != -ENOENT) {
					LOG_WRN("Battery SOC calibration unavailable: %d",
						battery_soc_store.load_error);
				}
			} else {
				LOG_WRN("Battery SOC calibration store initialization failed: %d",
					result);
			}
			result = ef800_composition_service_init(&composition_service, &persistence,
				&composition_catalog);
		if (result != 0) {
			composition_service_ready = false;
			LOG_WRN("Target composition initialization failed; current behavior retained: %d",
				result);
		} else {
			composition_service_ready = true;
			const struct ef800_composition_snapshot *composition =
				ef800_composition_service_active(&composition_service);
			enum ef800_composition_load_status status =
				ef800_composition_service_load_status(&composition_service);

			if (status == EF800_COMPOSITION_LOAD_STORED) {
				LOG_INF("Target composition generation %u loaded (%u stored, %u unknown, empty effective catalog)",
					(unsigned int)composition->stored.generation,
					(unsigned int)composition->stored.token_count,
					(unsigned int)composition->unknown_count);
			} else if (status == EF800_COMPOSITION_LOAD_CONFLICT) {
				LOG_WRN("Target composition slots conflict; current default behavior retained");
			} else if (status == EF800_COMPOSITION_LOAD_INVALID) {
				LOG_WRN("Target composition records invalid; current default behavior retained");
			} else if (status == EF800_COMPOSITION_LOAD_UNAVAILABLE) {
				LOG_INF("Target composition storage unavailable; current default behavior retained");
			} else {
				LOG_INF("No target composition profile; current default behavior retained");
			}
		}
#ifdef CONFIG_BOARD_NATIVE_SIM
		publish_virtual_composition_state();
#endif
		/* Foundation initialization mounts the data filesystem before plugin
		 * discovery. Only a completed scan can prove an assignment is stale. */
		if (ef800_storage_data_is_mounted()) {
			result = ef800_plugin_host_init(&plugin_registry);
			if (result != 0) {
				LOG_WRN("External plugin discovery skipped: %d", result);
			} else {
				plugin_discovery_ready = true;
			}
		}
		if (ef800_storage_configuration_is_mounted()) {
			result = ef800_legacy_migrate_all(&persistence);
			if (result != 0) {
				LOG_WRN("Legacy Mbed settings migration incomplete: %d", result);
			}
			(void)ef800_main_reload_time_settings();
		}
		result = ef800_port_assignment_init(&port_assignments, &persistence);
		if (result != 0) {
			LOG_ERR("Port assignment service initialization failed: %d", result);
		} else {
#ifdef CONFIG_BOARD_NATIVE_SIM
			ef800_virtual_frontpanel_set_plugin_assign_callback(
				virtual_assign_plugin, NULL);
			ef800_virtual_frontpanel_set_plugin_execute_callback(
				virtual_execute_plugin, NULL);
#endif
			result = ef800_workspace_init(&workspace, &persistence);
			if (result != 0) {
				LOG_ERR("Workspace service initialization failed: %d", result);
			} else if (ef800_storage_data_is_mounted()) {
				result = ef800_workspace_load_current(&workspace);
				if (result != 0 && result != -ENOENT) {
					LOG_WRN("Current Workspace unavailable: %d", result);
				}
			}
			if (ef800_storage_configuration_is_mounted()) {
				result = ef800_port_assignment_load_all(&port_assignments);
				if (result != 0) {
					LOG_WRN("Port assignments unavailable: %d", result);
				}
				clear_unsupported_msd_assignment();
			}
			if (plugin_discovery_ready) {
				result = reconcile_plugin_assignments();
				if (result != 0) {
					LOG_WRN("Plugin assignment reconciliation incomplete: %d",
						result);
				}
			} else {
				LOG_WRN("Plugin assignment cleanup skipped; filesystem scan unavailable");
			}
			{
				enum ef800_usb_personality initial_usb =
					EF800_USB_PERSONALITY_NONE;
				uint8_t usb_builder = EF800_PORT_BUILDER_NONE;
				uint32_t usb_connection = EF800_PORT_CONNECTION_NONE;

				if (ef800_storage_configuration_is_mounted()) {
					usb_builder = ef800_port_assignment_get_builder(
						&port_assignments, 4U);
					usb_connection = ef800_port_assignment_get_connection(
						&port_assignments, 4U);
				}
				if (usb_builder == EF800_PORT_BUILDER_MSD &&
					!usb_msd_composition_enabled()) {
					usb_builder = EF800_PORT_BUILDER_NONE;
					usb_connection = EF800_PORT_CONNECTION_NONE;
				}
				initial_usb = usb_personality_from_assignment(usb_builder,
					usb_connection);
				if (initial_usb == EF800_USB_PERSONALITY_MTP &&
					ef800_storage_data_is_mounted()) {
					result = ef800_storage_prepare_mtp_roots();
					(void)ef800_usb_manager_init(EF800_USB_PERSONALITY_NONE);
					if (result == 0) {
						/* The USB mux is selected below, after the MCP23017
						 * outputs are configured. Start MTP only then. */
						startup_mtp_pending = true;
					}
					if (result != 0) {
						LOG_WRN("USB MTP startup unavailable: %d", result);
					}
				} else {
					(void)ef800_usb_manager_init(initial_usb);
				}
			}
			result = reconcile_port_uart_state();
			if (result != 0) {
				LOG_WRN("Port routing initialization incomplete: %d", result);
			}
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
			if (plugin_discovery_ready) {
				int recovery_handler_result =
					ef800_plugin_host_set_route_recovery_handler(
						recover_plugin_uart_route, NULL);

				if (recovery_handler_result != 0) {
					LOG_ERR("Plugin route recovery handler registration failed: %d",
						recovery_handler_result);
				}
			}
#endif
			if (result == 0 && ef800_storage_is_mounted()) {
				LOG_INF("Interactive shell active on DebugConsole; run 'mcumgr start' to enter raw FS mode");
			}
		}
	}
	result = ef800_lcd_contrast_init(&lcd_contrast,
		lcd_contrast_composition_mode(), &main_contrast_ops, NULL);
	if (result != 0) {
		LOG_WRN("Main LCD contrast initialization failed: %d", result);
	}

#if DT_HAS_COMPAT_STATUS_OKAY(microchip_mcp23017)
result = ef800_mcp23017_open(&mcp23017,
			DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(microchip_mcp23017)));
#else
	result = -ENODEV;
#endif
	if (result == 0) {
		if (usb_select.port == NULL || keyboard_reset.port == NULL) {
			result = -ENODEV;
		} else {
			result = ef800_mcp23017_configure_outputs(&mcp23017,
				BIT(usb_select.pin) | BIT(keyboard_reset.pin) |
				BIT(lcd_config.fs.pin) | BIT(lcd_config.reset.pin),
				BIT(lcd_config.fs.pin));
		}
	}
	if (result == 0) {
		result = ef800_mcp23017_set_pin(&mcp23017, usb_select.pin, true);
	}
	if (keyboard_updater_request.port != NULL &&
	    device_is_ready(keyboard_updater_request.port)) {
		int strap_result = gpio_pin_configure_dt(&keyboard_updater_request,
			GPIO_OUTPUT_INACTIVE);

		keyboard_updater_request_ready = strap_result == 0;
		if (strap_result != 0) {
			LOG_WRN("Keyboard updater-request PE0 unavailable: %d",
				strap_result);
		} else {
			LOG_INF("Keyboard updater-request PE0 initialized inactive");
		}
	} else {
		LOG_WRN("Keyboard updater-request PE0 GPIO unavailable");
	}
	if (result == 0 && startup_mtp_pending) {
		result = ef800_usb_manager_set(EF800_USB_PERSONALITY_MTP);
		LOG_INF("Deferred USB MTP startup result=%d", result);
		if (result != 0) {
			LOG_WRN("USB MTP startup unavailable: %d", result);
		}
	}
	if (result == 0) {
		result = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	}
	if (result == 0) {
		if (!device_is_ready(keyboard_uart)) {
			result = -ENODEV;
			LOG_ERR("Keyboard UART is not ready");
		} else {
			result = configure_keyboard_uart();
		}
	}
	if (result == 0) {
		ef800_keyboard_host_init(&keyboard_host);
		k_msgq_purge(&keyboard_rx_msgq);
		keyboard_uart_initialized = true;
		uart_irq_callback_user_data_set(keyboard_uart, keyboard_uart_isr, NULL);
		uart_irq_rx_enable(keyboard_uart);
		/* RX is live before reset release so the controller's one-shot 0xaa is kept. */
		atomic_set(&keyboard_ready_indication, 0);
		k_msleep(10U);
		result = gpio_pin_set_dt(&keyboard_reset, 1);
		keyboard_rx_armed = result == 0;
	}
	if (result != 0) {
		LOG_WRN("MCP23017 controls unavailable: %d", result);
	} else if (ef800_mcp23017_read(&mcp23017, &mcp_port) == 0) {
		mcp_controls_ready = true;
		LOG_INF("MCP23017 controls initialized (port=0x%04x)", mcp_port);
	} else {
		mcp_controls_ready = true;
		LOG_WRN("MCP23017 controls initialized but port readback failed");
	}

	if (power_down.port == NULL) {
		result = -ENODEV;
	} else {
		result = gpio_pin_configure_dt(&power_down, GPIO_OUTPUT_INACTIVE);
	}
	if (result != 0) {
		LOG_WRN("PB5 keyboard power-down output unavailable: %d", result);
	} else {
		LOG_INF("PB5 keyboard power-down output initialized inactive");
	}
	if (keyboard_rx_armed) {
		result = keyboard_update_run();
		if (result != 0 && result != -EAGAIN) {
			LOG_ERR("keyboard update startup attempt failed: %d", result);
		}
	}

	if (!mcp_controls_ready) {
		LOG_WRN("T6963 LCD skipped: MCP23017 reset and FS controls are unavailable");
	} else {
		result = ef800_t6963_init(&lcd, &lcd_config);
		if (result == 0) {
			result = ef800_t6963_initialize(&lcd);
		}
		if (result != 0) {
			LOG_WRN("T6963 LCD unavailable; continuing without display: %d", result);
		} else {
			lcd_ready = true;
			result = draw_header_separator();
			if (result == 0) {
				display_banner();
			} else {
				lcd_ready = false;
				LOG_WRN("T6963 header unavailable; continuing without display: %d",
					result);
			}
		}
	}

	enum ef800_battery_soc_source battery_source =
		battery_soc_composition_source();
	ef800_runtime_battery_reader_t battery_reader = NULL;
	void *battery_reader_context = NULL;

	if (battery_source == EF800_BATTERY_SOC_SOURCE_MAX17048) {
#if DT_HAS_COMPAT_STATUS_OKAY(maxim_max17048)
		#if CONFIG_EF800_MAIN_PROBE_FUEL_GAUGE_AT_STARTUP
		result = ef800_battery_soc_gauge_init(&battery_gauge_source,
			probe_max17048, read_max17048, &fuel_gauge);
		if (result == 0) {
			battery_reader = ef800_battery_soc_gauge_read;
			battery_reader_context = &battery_gauge_source;
			LOG_INF("MAX17048 will be probed once by the runtime battery worker");
		} else {
			LOG_ERR("MAX17048 source setup failed: %d", result);
		}
		#else
		LOG_INF("MAX17048 probe disabled; battery SOC unavailable");
		#endif
#else
		LOG_INF("MAX17048-compatible fuel gauge disabled: no enabled I2C node");
#endif
	} else if (battery_source == EF800_BATTERY_SOC_SOURCE_ADC) {
		result = adc_channel_setup_dt(&battery_adc);
		if (result != 0) {
			LOG_WRN("Battery ADC unavailable on PC1: %d", result);
		} else {
			battery_adc_ready = true;
			battery_reader = read_adc_battery;
		}
	} else {
		LOG_INF("BATSOC0 active; battery SOC is disabled by composition");
	}
	ef800_runtime_state_init(&runtime_state, battery_reader,
		battery_reader_context);
	runtime_state_ready = true;
	ef800_runtime_state_start(&runtime_state);
	LOG_INF("Main startup checkpoint: runtime state started");
	k_msleep(20U);
	update_lcd_status();
	LOG_INF("Main startup checkpoint: LCD status updated");

	LOG_INF("Main startup checkpoint: waiting for keyboard");
	if (keyboard_rx_armed && wait_for_keyboard_startup()) {
		display_line(6U, "Keyboard ready");
	} else if (!keyboard_rx_armed) {
		LOG_WRN("Keyboard disabled: reset remains asserted because UART setup failed");
	}
	display_menu();

#ifdef CONFIG_BOARD_NATIVE_SIM
	result = ef800_virtual_usb_peer_init();
	if (result != 0) {
		LOG_WRN("Virtual USB host peer unavailable: %d", result);
	}
#endif

	while (true) {
		static bool msc_requested;
		static bool msc_removal_pending;
		static int64_t next_status_update_ms;
		static enum ef800_storage_owner storage_owner =
			EF800_STORAGE_OWNER_FAT;
#ifdef CONFIG_BOARD_NATIVE_SIM
		if (virtual_power_off) {
			if (k_msgq_get(&keyboard_rx_msgq, &byte, K_FOREVER) == 0) {
				process_keyboard_byte(byte, k_uptime_get_32());
			}
			continue;
		}
#endif
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		process_plugin_async_completion();
#endif
		bool requested = ef800_main_usb_msd_requested();
		enum ef800_storage_action storage_action =
			ef800_storage_owner_action(storage_owner, requested);

		if (!requested && storage_owner == EF800_STORAGE_OWNER_FAT) {
			/* Permit one fresh attempt after VBUS is removed or the persisted
			 * assignment is changed away from MSD. */
			msc_requested = false;
		}
		if (storage_action == EF800_STORAGE_ACTION_FAT_TO_MSC && !msc_requested) {
			int unmount_result;
			int msc_result = 0;
			bool mcumgr_was_active = mcumgr_transport_active;

			msc_requested = true;
			ef800_mcumgr_transport_stop();
			mcumgr_transport_active = false;
			unmount_result = ef800_storage_unmount();
			if (unmount_result == 0) {
				msc_result = ef800_main_usb_msc_start();
			}
			if (unmount_result == 0 && msc_result == 0) {
				storage_owner = EF800_STORAGE_OWNER_MSC;
				LOG_INF("QSPI ownership transferred to USB MSC");
			} else {
				int mount_result;

				LOG_ERR("QSPI transfer to USB MSC failed: unmount=%d usb=%d",
					unmount_result, msc_result);
				/* A failed USB start must not strand the volume with no owner.
				 * Restore FAT and the filesystem transport; do not retry the
				 * partially initialized USB context until the request is cleared. */
				mount_result = ef800_storage_mount();
				if (mount_result != 0) {
					LOG_ERR("QSPI FAT rollback after USB failure failed: %d",
						mount_result);
				} else if (mcumgr_was_active) {
					int transport_result = ef800_mcumgr_transport_init();

					if (transport_result != 0) {
						LOG_WRN("MCUmgr transport rollback failed: %d",
							transport_result);
					} else {
						mcumgr_transport_active = true;
					}
				}
			}
		} else if (storage_action == EF800_STORAGE_ACTION_MSC_TO_FAT) {
			/* USB device-next does not guarantee that usbd_msc worker state is
			 * quiescent after shutdown. Never remount FAT while that worker may
			 * still access endpoint or disk state. Apply this change after reboot. */
			if (!msc_removal_pending) {
				msc_removal_pending = true;
				LOG_WRN("USB MSD removal requires reboot before FAT remount");
			}
		}
		if (k_msgq_get(&keyboard_rx_msgq, &byte,
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
			K_MSEC(plugin_async_ui_pending ? 20 : 1000)) == 0) {
#else
			K_MSEC(1000)) == 0) {
#endif
			process_keyboard_byte(byte, k_uptime_get_32());
		}
		while (k_msgq_get(&keyboard_rx_msgq, &byte, K_NO_WAIT) == 0) {
			process_keyboard_byte(byte, k_uptime_get_32());
		}
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		process_plugin_async_completion();
#endif
		if (k_uptime_get() >= next_status_update_ms) {
			update_lcd_status();
			next_status_update_ms = k_uptime_get() +
				CONFIG_EF800_MAIN_RUNTIME_RTC_PERIOD_MS;
		}
	}
}
