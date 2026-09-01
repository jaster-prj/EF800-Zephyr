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
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "ef800_max17043.h"
#include "ef800_main.h"
#include "ef800_mcp23017.h"
#include "ef800_persistence.h"
#include "ef800_position.h"
#include "ef800_ports.h"
#include "ef800_port_uart.h"
#include "ef800_console.h"
#include "ef800_metadata.h"
#include "ef800_main_usb_msc.h"
#include "ef800_t6963.h"
#include "ef800_workspace.h"
#include "keyboard_host.h"

LOG_MODULE_REGISTER(ef800_main_app, LOG_LEVEL_INF);

/* lpuart1 is the EF800 board's PG7/PC0 keyboard link. */
#define KEYBOARD_UART_NODE DT_CHOSEN(zephyr_keyboard_uart)
#define KEYBOARD_RX_QUEUE_SIZE 128U
#define EF800_USER_NODE DT_PATH(zephyr_user)
#define LCD_NODE EF800_USER_NODE
#define LCD_COLUMNS 40U
#define STATUS_LINE_SIZE 80U

static const struct device *const keyboard_uart =
	DEVICE_DT_GET(KEYBOARD_UART_NODE);
static struct ef800_keyboard_host keyboard_host;
K_MSGQ_DEFINE(keyboard_rx_msgq, sizeof(uint8_t), KEYBOARD_RX_QUEUE_SIZE, 1);

static struct ef800_mcp23017 mcp23017;
static struct ef800_t6963 lcd;
static struct ef800_max17043 fuel_gauge;
static struct ef800_persistence persistence;
static struct ef800_workspace workspace;
static struct ef800_port_assignment_service port_assignments;
static struct ef800_port_uart_registry port_uart_registry;
static struct ef800_boot_metadata boot_metadata;
static bool boot_metadata_valid;
static bool lcd_ready;
static bool fuel_gauge_ready;
static uint8_t menu_selection;
static uint8_t selected_port_index;
static uint8_t selected_builder_id;

enum application_menu {
	APPLICATION_MENU_MAIN = 0,
	APPLICATION_MENU_SETTINGS,
	APPLICATION_MENU_WORKSPACE,
	APPLICATION_MENU_WORKSPACE_CREATE,
	APPLICATION_MENU_WORKSPACE_SELECT,
	APPLICATION_MENU_WORKSPACE_LIST,
	APPLICATION_MENU_POSITION,
	APPLICATION_MENU_PORTS,
	APPLICATION_MENU_PORT_DEVICES,
	APPLICATION_MENU_PORT_CONNECTIONS,
};

#define WORKSPACE_LIST_MAX 16U

static enum application_menu active_menu = APPLICATION_MENU_MAIN;
static char workspace_input[EF800_WORKSPACE_NAME_MAX + 1U];
static uint8_t workspace_input_length;
static char workspace_names[WORKSPACE_LIST_MAX][EF800_WORKSPACE_NAME_MAX + 1U];
static size_t workspace_name_count;

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
	"Settings",
};

static const char *const menu_shortcuts[] = {"w", "s", "m", "e"};
static const char *const settings_items[] = {
	"Set time",
	"Port assignment",
};
static const char *const settings_shortcuts[] = {"z", "p"};
static const char *const workspace_items[] = {
	"Restore current",
	"Load existing",
	"New workspace",
	"List workspaces",
};
static const char *const workspace_shortcuts[] = {"l", "e", "n", "i"};

static void start_time_editor(void);
static void display_workspace_list(void);
static void display_position_editor(void);
static void display_port_assignment_ports(void);
static void display_port_assignment_devices(void);
static void display_port_assignment_connections(void);
static int port_uart_check_ready(uint8_t port_id);
static int claim_loaded_port_assignments(void);

static int load_boot_metadata(void)
{
	int result = ef800_metadata_read_current(&boot_metadata,
							&boot_metadata_valid);

	if (result != 0 || !boot_metadata_valid) {
		ef800_boot_metadata_init(&boot_metadata);
		boot_metadata_valid = false;
		return result;
	}
	return 0;
}

static int import_metadata_assignments(void)
{
	int result;

	if (!boot_metadata_valid) {
		return 0;
	}
	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		result = ef800_port_assignment_import_with_connection(
			&port_assignments, port_id,
			ef800_port_assignment_builder(boot_metadata.port_assignments, port_id),
			ef800_port_assignment_connection(boot_metadata.port_assignments,
													  port_id));
		if (result != 0) {
			return result;
		}
	}
	return 0;
}

static int sync_boot_metadata(void)
{
	uint64_t assignments = 0U;
	uint8_t debug_port = 0U;
	int result;

	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		uint8_t builder_id = ef800_port_assignment_get_builder(&port_assignments,
									port_id);

		result = ef800_port_assignment_set_record(&assignments, port_id,
									 builder_id,
									 ef800_port_assignment_get_connection(
										 &port_assignments, port_id));
		if (result != 0) {
			return result;
		}
		if (debug_port == 0U && builder_id == EF800_PORT_BUILDER_DEBUG) {
			debug_port = port_id;
		}
	}
	boot_metadata.port_assignments = assignments;
	result = ef800_boot_metadata_set_console(&boot_metadata, debug_port);
	if (result != 0) {
		return result;
	}
	result = ef800_metadata_write_current(&boot_metadata);
	if (result == 0) {
		boot_metadata_valid = true;
		(void)ef800_console_init(false);
	}
	return result;
}

static enum ef800_port_uart_id port_uart_id_for_console(uint8_t port_id)
{
	switch (port_id) {
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
	struct ef800_boot_metadata desired = boot_metadata;
	uint8_t debug_port = 0U;
	uint8_t console_port;
	int result;

	for (uint8_t port_id = 1U; port_id <= EF800_PORT_COUNT; ++port_id) {
		uint8_t builder_id = ef800_port_assignment_get_builder(
			&port_assignments, port_id);

		result = ef800_port_assignment_set_record(&desired.port_assignments,
									    port_id, builder_id,
									    ef800_port_assignment_get_connection(
										 &port_assignments, port_id));
		if (result != 0) {
			return result;
		}
		if (debug_port == 0U && builder_id == EF800_PORT_BUILDER_DEBUG) {
			debug_port = port_id;
		}
	}
	result = ef800_boot_metadata_set_console(&desired, debug_port);
	if (result != 0) {
		return result;
	}
	console_port = ef800_console_port_select(&desired, false);
	result = ef800_console_set_target(console_port);
	if (result != 0 && console_port != EF800_CONSOLE_SAFE_PORT) {
		console_port = EF800_CONSOLE_SAFE_PORT;
		result = ef800_console_set_target(console_port);
	}
	if (result != 0) {
		LOG_WRN("Console selection failed: %d", result);
		return result;
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
	if (!boot_metadata_valid ||
		boot_metadata.port_assignments != desired.port_assignments ||
		boot_metadata.console_target != desired.console_target) {
		boot_metadata = desired;
		result = ef800_metadata_write_current(&boot_metadata);
		if (result == 0) {
			boot_metadata_valid = true;
		}
	}
	return result;
}

static int persist_port_assignment_metadata(void)
{
	int result = sync_boot_metadata();

	if (result != 0) {
		LOG_WRN("Console/port metadata update failed: %d", result);
	}
	return result;
}

static int claim_loaded_port_assignments(void)
{
	int first_error = 0;

	for (size_t index = 0U; index < ef800_port_count(); ++index) {
		const struct ef800_port_descriptor *port = ef800_port_at(index);
		uint8_t builder_id = ef800_port_assignment_get_builder(
			&port_assignments, port->id);
		int result;

		if (builder_id == EF800_PORT_BUILDER_NONE) {
			continue;
		}
		result = port_uart_check_ready(port->id);
		if (result != 0) {
			LOG_WRN("Port %u UART is not ready: %d", port->id, result);
			if (first_error == 0) {
				first_error = result;
			}
			continue;
		}
		result = ef800_port_uart_claim(&port_uart_registry, port->id,
						       builder_id,
						       ef800_port_assignment_get_connection(
							&port_assignments, port->id));
		if (result != 0) {
			LOG_WRN("Port %u assignment cannot claim UART: %d", port->id,
				result);
			if (first_error == 0) {
				first_error = result;
			}
		}
	}
	return first_error;
}

static const struct gpio_dt_spec keyboard_reset =
	GPIO_DT_SPEC_GET_OR(EF800_USER_NODE, keyboard_reset_gpios, {0});
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

static int port_uart_check_ready(uint8_t port_id)
{
	const struct ef800_port_uart_route *route =
		ef800_port_uart_for_port(port_id);
	const struct device *device;

	if (route == NULL) {
		return 0;
	}
	device = port_uart_device(route->uart_id);
	return device != NULL && device_is_ready(device) ? 0 : -ENODEV;
}

#if DT_HAS_COMPAT_STATUS_OKAY(maxim_max17043)
static const struct i2c_dt_spec fuel_gauge_i2c =
	I2C_DT_SPEC_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(maxim_max17043));
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
		.parity = UART_CFG_PARITY_ODD,
		.stop_bits = UART_CFG_STOP_BITS_2,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	return uart_configure(keyboard_uart, &config);
}

static void display_line(uint8_t row, const char *text)
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

static void display_banner(void)
{
	display_line(0U, "EF800 MainApplication");
	display_line(1U, "Zephyr runtime/menu foundation");
	display_line(6U, "Keyboard startup...");
}

static void display_menu_entries(const char *title, const char *const *items,
				 const char *const *shortcuts, size_t count)
{
	char line[STATUS_LINE_SIZE];

	display_line(0U, title);
	if (ef800_workspace_is_selected(&workspace)) {
		snprintk(line, sizeof(line), "Workspace: %s",
			ef800_workspace_get_name(&workspace));
	} else {
		snprintk(line, sizeof(line), "Workspace: none");
	}
	display_line(1U, line);
	for (size_t index = 0U; index < 4U; ++index) {
		if (index < count) {
			snprintk(line, sizeof(line), "%c %s [%s]",
				index == menu_selection ? '>' : ' ', items[index],
				shortcuts[index]);
		} else {
			line[0] = '\0';
		}
		display_line((uint8_t)(4U + index), line);
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
				     ARRAY_SIZE(settings_items));
}

static void display_workspace_menu(void)
{
	display_menu_entries("Workspace", workspace_items, workspace_shortcuts,
				     ARRAY_SIZE(workspace_items));
}

static void display_port_assignment_ports(void)
{
	char line[STATUS_LINE_SIZE];
	size_t first = menu_selection >= 3U ? menu_selection - 3U : 0U;

	display_line(0U, "Port assignment");
	snprintk(line, sizeof(line), "Ports %u-%u of %u", (unsigned)(first + 1U),
		(unsigned)MIN(first + 4U, EF800_PORT_COUNT), (unsigned)EF800_PORT_COUNT);
	display_line(1U, line);
	for (size_t row = 0U; row < 4U; ++row) {
		size_t index = first + row;
		const struct ef800_port_descriptor *port = ef800_port_at(index);
		const struct ef800_port_device_descriptor *device = NULL;
		const struct ef800_port_connection_descriptor *connection = NULL;

		if (port == NULL) {
			display_line((uint8_t)(3U + row), "");
			continue;
		}
		if (ef800_port_assignment_is_assigned(&port_assignments, port->id)) {
			device = ef800_port_device_get(
				ef800_port_assignment_get_builder(&port_assignments, port->id));
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
			device == NULL ? "none" : device->title,
			connection == NULL ? "" : " / ",
			connection == NULL ? "" : connection->name,
			device != NULL && ef800_port_uart_can_claim(
				&port_uart_registry, port->id, device->builder_id,
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
		if (ef800_port_device_is_compatible(port,
							 ef800_port_device_at(index))) {
			++count;
		}
	}
	return count;
}

static size_t port_assignment_device_count(void)
{
	const struct ef800_port_descriptor *port =
		ef800_port_at(selected_port_index);

	return compatible_device_count(port) + (port != NULL && !port->fixed ? 1U : 0U);
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
		if (ef800_port_device_is_compatible(port, device)) {
			if (compatible_index++ == selection) {
				return device;
			}
		}
	}
	return NULL;
}

static void display_port_assignment_devices(void)
{
	const struct ef800_port_descriptor *port = ef800_port_at(selected_port_index);
	const struct ef800_port_device_descriptor *current;
	char line[STATUS_LINE_SIZE];
	size_t row = 3U;
	size_t device_index = 0U;

	if (port == NULL) {
		return;
	}
	display_line(0U, "Port assignment");
	snprintk(line, sizeof(line), "Port %u: %s", port->id, port->title);
	display_line(1U, line);
	current = ef800_port_device_get(ef800_port_assignment_get_builder(
		&port_assignments, port->id));
	if (!ef800_port_assignment_is_assigned(&port_assignments, port->id)) {
		current = NULL;
	}
	snprintk(line, sizeof(line), "Current: %s%s", current == NULL ? "none" : current->title,
		port->fixed ? " (fixed)" : "");
	display_line(2U, line);
	for (size_t index = 0U; index < ef800_port_device_count(); ++index) {
		const struct ef800_port_device_descriptor *device =
			ef800_port_device_at(index);
		if (!ef800_port_device_is_compatible(port, device)) {
			continue;
		}
		snprintk(line, sizeof(line), "%c %s [%c]",
			device_index == menu_selection ? '>' : ' ', device->title,
			device->shortcut);
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
		if (connection != NULL && ef800_port_device_supports_connection(
			selected_builder_id, connection->capability)) {
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
		if (connection != NULL && ef800_port_device_supports_connection(
			selected_builder_id, connection->capability) &&
			compatible_index++ == selection) {
			return connection;
		}
	}
	return NULL;
}

static void display_port_assignment_connections(void)
{
	const struct ef800_port_descriptor *port = ef800_port_at(selected_port_index);
	char line[STATUS_LINE_SIZE];
	size_t row = 3U;
	size_t connection_index = 0U;
	size_t first;
	size_t count;

	if (port == NULL) {
		return;
	}
	display_line(0U, "Port connection");
	snprintk(line, sizeof(line), "Port %u: %s", port->id, port->title);
	display_line(1U, line);
	snprintk(line, sizeof(line), "Device: %s",
		ef800_port_device_get(selected_builder_id)->title);
	display_line(2U, line);
	count = port_assignment_connection_count();
	first = menu_selection >= 3U ? menu_selection - 3U : 0U;
	for (size_t index = 0U; index < ef800_port_connection_count(port->id); ++index) {
		const struct ef800_port_connection_descriptor *connection =
			ef800_port_connection_at(port->id, index);
		if (connection == NULL || !ef800_port_device_supports_connection(
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
	display_line(1U, line);
	display_line(7U, "ENTER select  ESC back");
}

static void display_active_menu(void)
{
	switch (active_menu) {
	case APPLICATION_MENU_SETTINGS:
		display_settings_menu();
		break;
	case APPLICATION_MENU_WORKSPACE:
		display_workspace_menu();
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

static size_t active_menu_count(void)
{
	switch (active_menu) {
	case APPLICATION_MENU_SETTINGS:
		return ARRAY_SIZE(settings_items);
	case APPLICATION_MENU_WORKSPACE:
		return ARRAY_SIZE(workspace_items);
	case APPLICATION_MENU_WORKSPACE_SELECT:
	case APPLICATION_MENU_WORKSPACE_LIST:
		return workspace_name_count;
	case APPLICATION_MENU_PORTS:
		return EF800_PORT_COUNT;
	case APPLICATION_MENU_PORT_DEVICES:
		return port_assignment_device_count();
	case APPLICATION_MENU_PORT_CONNECTIONS:
		return port_assignment_connection_count();
	case APPLICATION_MENU_MAIN:
	default:
		return ARRAY_SIZE(menu_items);
	}
}

static void move_menu_selection(int direction)
{
	size_t count = active_menu_count();

	if (count == 0U) {
		menu_selection = 0U;
		return;
	}
	if (direction < 0 && menu_selection > 0U) {
		--menu_selection;
	} else if (direction > 0 && menu_selection + 1U < count) {
		++menu_selection;
	}
	display_active_menu();
}

static void return_to_workspace_menu(void)
{
	active_menu = APPLICATION_MENU_WORKSPACE;
	menu_selection = 0U;
	display_workspace_menu();
}

static void return_to_main_menu(void)
{
	active_menu = APPLICATION_MENU_MAIN;
	menu_selection = 0U;
	display_menu();
}

static void return_from_child_menu(void)
{
	if (active_menu == APPLICATION_MENU_WORKSPACE ||
	    active_menu == APPLICATION_MENU_SETTINGS) {
		return_to_main_menu();
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
	} else {
		return_to_workspace_menu();
	}
}

static void return_from_position_editor(void)
{
	position_editor.active = false;
	(void)ef800_t6963_clear_text(&lcd);
	active_menu = APPLICATION_MENU_MAIN;
	menu_selection = 1U;
	display_menu();
}

static void display_workspace_input(void)
{
	char line[STATUS_LINE_SIZE];

	display_line(0U, "New workspace");
	display_line(1U, "Use letters, numbers, _ - .");
	snprintk(line, sizeof(line), "> %s", workspace_input);
	display_line(4U, line);
	display_line(6U, "ENTER create  ESC cancel");
}

static void display_workspace_list(void)
{
	size_t first;

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

	if (result == 0) {
		snprintk(line, sizeof(line), "%s: %s", operation,
			ef800_workspace_get_name(&workspace) == NULL ? "none" :
			ef800_workspace_get_name(&workspace));
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
	default:
		break;
	}
}

static void display_port_assignment_result(const char *operation, int result)
{
	char line[STATUS_LINE_SIZE];

	if (result == 0) {
		snprintk(line, sizeof(line), "%s", operation);
	} else if (result == -ENOTSUP) {
		snprintk(line, sizeof(line), "%s: storage unavailable", operation);
	} else {
		snprintk(line, sizeof(line), "%s failed (%d)", operation, result);
	}
	LOG_INF("%s", line);
	display_line(6U, line);
}

static void activate_port_assignment_entry(void)
{
	const struct ef800_port_descriptor *port =
		ef800_port_at(selected_port_index);
	const struct ef800_port_device_descriptor *device;

	if (port == NULL) {
		return;
	}
	if (!port->fixed && menu_selection == port_assignment_device_count() - 1U) {
		int result = ef800_port_assignment_remove(&port_assignments, port->id);
		display_port_assignment_result("Assignment removed", result);
		if (result == 0) {
			active_menu = APPLICATION_MENU_PORTS;
			menu_selection = selected_port_index;
			display_port_assignment_ports();
		}
		return;
	}
	device = device_at_selection(port, menu_selection);
	if (device == NULL) {
		display_port_assignment_result("Invalid device selection", -EINVAL);
		return;
	}
	selected_builder_id = device->builder_id;
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
	uint32_t old_connection;
	int result;

	if (port == NULL || connection == NULL) {
		display_port_assignment_result("Invalid connection selection", -EINVAL);
		return;
	}
	old_builder_id = ef800_port_assignment_get_builder(&port_assignments, port->id);
	old_connection = ef800_port_assignment_get_connection(&port_assignments, port->id);
	if (old_builder_id != EF800_PORT_BUILDER_NONE) {
		result = ef800_port_uart_release(&port_uart_registry, port->id,
							 old_builder_id, old_connection);
		if (result != 0) {
			display_port_assignment_result("UART release failed", result);
			return;
		}
	}
	result = ef800_port_uart_claim(&port_uart_registry, port->id,
						       selected_builder_id, connection->capability);
	if (result != 0) {
		if (old_builder_id != EF800_PORT_BUILDER_NONE) {
			(void)ef800_port_uart_claim(&port_uart_registry, port->id,
							     old_builder_id, old_connection);
		}
		display_port_assignment_result("Connection unavailable", result);
		return;
	}
	result = ef800_port_assignment_set_with_connection(&port_assignments,
												  port->id,
												  selected_builder_id,
												  connection->capability);
	if (result == 0) {
		result = persist_port_assignment_metadata();
	}
	if (result == 0) {
		char line[STATUS_LINE_SIZE];
		snprintk(line, sizeof(line), "Assigned %s to port %u",
			ef800_port_device_get(selected_builder_id)->title, port->id);
		display_port_assignment_result(line, 0);
		active_menu = APPLICATION_MENU_PORTS;
		menu_selection = selected_port_index;
		display_port_assignment_ports();
	} else {
		(void)ef800_port_uart_release(&port_uart_registry, port->id,
						       selected_builder_id, connection->capability);
		if (old_builder_id != EF800_PORT_BUILDER_NONE) {
			(void)ef800_port_uart_claim(&port_uart_registry, port->id,
							     old_builder_id, old_connection);
		}
		display_port_assignment_result("Assignment failed", result);
	}
}

static void activate_menu_entry(void)
{
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
		case 3U:
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
		} else {
			active_menu = APPLICATION_MENU_PORTS;
			menu_selection = 0U;
			display_port_assignment_ports();
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

	if (key == 0x1BU || key == 0x08U || key == 0x7FU) {
		if (active_menu == APPLICATION_MENU_WORKSPACE_CREATE &&
		    key != 0x1BU && workspace_input_length > 0U) {
			workspace_input[--workspace_input_length] = '\0';
			display_workspace_input();
		} else {
			return_to_workspace_menu();
		}
		return;
	}
	if (key == '\r' || key == '\n') {
		if (active_menu == APPLICATION_MENU_WORKSPACE_CREATE) {
			if (workspace_input_length == 0U) {
				display_line(6U, "Workspace name is empty");
				return;
			}
			result = ef800_workspace_create(&workspace, workspace_input);
			display_workspace_result("Create workspace", result);
			if (result == 0) {
				return_to_workspace_menu();
			}
			return;
		}
		if (active_menu == APPLICATION_MENU_WORKSPACE_SELECT &&
		    workspace_name_count > 0U) {
			result = ef800_workspace_select(&workspace,
						workspace_names[menu_selection]);
			display_workspace_result("Load workspace", result);
			if (result == 0) {
				return_to_workspace_menu();
			}
		}
		return;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_CREATE &&
	    workspace_input_character_valid(key) &&
	    workspace_input_length + 1U < sizeof(workspace_input)) {
		workspace_input[workspace_input_length++] = (char)key;
		workspace_input[workspace_input_length] = '\0';
		display_workspace_input();
	}
}

static bool handle_application_key(uint8_t key)
{
	const char *const *shortcuts;
	size_t count;

	if (position_editor.active) {
		handle_position_editor_key(key);
		return true;
	}
	if (active_menu == APPLICATION_MENU_WORKSPACE_CREATE ||
	    active_menu == APPLICATION_MENU_WORKSPACE_SELECT ||
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
	if (key == '\r' || key == '\n') {
		activate_menu_entry();
		return true;
	}
	if (active_menu == APPLICATION_MENU_MAIN) {
		shortcuts = menu_shortcuts;
		count = ARRAY_SIZE(menu_items);
	} else if (active_menu == APPLICATION_MENU_SETTINGS) {
		shortcuts = settings_shortcuts;
		count = ARRAY_SIZE(settings_items);
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
				key == (uint8_t)device->shortcut) {
				/* The displayed list contains only compatible devices. */
				size_t selection = 0U;
				for (size_t candidate = 0U; candidate < index; ++candidate) {
					if (ef800_port_device_is_compatible(port,
									 ef800_port_device_at(candidate))) {
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
	} else {
		shortcuts = workspace_shortcuts;
		count = ARRAY_SIZE(workspace_items);
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
				bool filled = soc >= (uint8_t)((x - 228U) * 20U);
				for (uint16_t y = 2U; y <= 4U; ++y) {
					(void)ef800_t6963_write_pixel(&lcd, x, y, filled);
				}
			}
			for (uint16_t y = 2U; y <= 4U; ++y) {
				(void)ef800_t6963_write_pixel(&lcd, 233U, y, soc > 95U);
			}
}

static void update_lcd_status(void)
{
	char line[STATUS_LINE_SIZE];
	struct rtc_time time;
	struct ef800_max17043_status battery;
	int ret = 0;
	int rtc_ret;

	if (!lcd_ready) {
		return;
	}

	rtc_ret = ef800_main_rtc_get_local_time(&time);
	if (rtc_ret == 0) {
		snprintk(line, sizeof(line), "Local %04d-%02d-%02d %02d:%02d:%02d",
			time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
			time.tm_hour, time.tm_min, time.tm_sec);
	} else {
		snprintk(line, sizeof(line), "RTC unavailable (%d)", rtc_ret);
	}
	display_line(2U, line);

	if (fuel_gauge_ready) {
		ret = ef800_max17043_read(&fuel_gauge, &battery);
		if (ret == 0) {
			snprintk(line, sizeof(line), "Battery %u%% %lumV",
				battery.soc_percent,
				(unsigned long)(battery.voltage_uv / 1000U));
		} else {
			snprintk(line, sizeof(line), "Battery read failed (%d)", ret);
		}
	} else {
		snprintk(line, sizeof(line), "Battery unavailable");
	}
	display_line(3U, line);

	if (fuel_gauge_ready && ret == 0) {
		static uint8_t last_soc = UINT8_MAX;

		if (battery.soc_percent != last_soc) {
			draw_battery_icon(battery.soc_percent);
			last_soc = battery.soc_percent;
		}
	}

	/* The old LcdHelper keeps a compact local-time clock in the header. */
	if (rtc_ret == 0 &&
		(time.tm_hour >= 0 && time.tm_min >= 0)) {
		static int last_hour = -1;
		static int last_minute = -1;

		if (time.tm_hour != last_hour || time.tm_min != last_minute) {
			char clock_text[16];
			uint8_t hour = (uint8_t)time.tm_hour;
			uint8_t minute = (uint8_t)time.tm_min;
			snprintk(clock_text, sizeof(clock_text), "%02u:%02u",
				hour, minute);
			(void)ef800_t6963_text_cursor(&lcd, 17U, 0U);
			(void)ef800_t6963_write_string(&lcd, clock_text);
			last_hour = time.tm_hour;
			last_minute = time.tm_min;
		}
	}
}

static const char *time_editor_prompt(enum time_editor_field field)
{
	switch (field) {
	case TIME_EDITOR_WEEKDAY:
		return "Weekday 1-7";
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

static void display_time_editor(void)
{
	char line[STATUS_LINE_SIZE];

	if (!lcd_ready) {
		return;
	}
	(void)ef800_t6963_clear_text(&lcd);
	display_line(0U, "EF800 RTC setup");
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
	snprintk(line, sizeof(line), "> %s", time_editor.input);
	display_line(6U, line);
	display_line(7U, "ENTER next  ESC cancel");
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
		int hours;
		int minutes;
		char sign = time_editor.input[0] == '-' ? '-' : '+';
		if (sscanf(time_editor.input, "%d:%d", &hours, &minutes) != 2 ||
			hours < -24 || hours > 24 || minutes < 0 || minutes >= 60) {
			return -EINVAL;
		}
		if (sign == '-') {
			*value = hours * 60 - minutes;
		} else {
			*value = hours * 60 + minutes;
		}
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
	int ret = parse_time_editor_value(&value);

	if (ret != 0) {
		return ret;
	}
	switch (time_editor.field) {
	case TIME_EDITOR_WEEKDAY:
		if (value < 1 || value > 7) return -ERANGE;
		time_editor.local.tm_wday = value - 1;
		break;
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
		display_line(6U, "RTC updated successfully");
		time_editor.active = false;
		(void)ef800_t6963_clear_text(&lcd);
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
		(void)ef800_t6963_clear_text(&lcd);
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
		int hours;
		int minutes;
		char sign = argv[2][0] == '-' ? '-' : '+';
		if (sscanf(argv[2], "%d:%d", &hours, &minutes) != 2 ||
			hours < -24 || hours > 24 || minutes < 0 || minutes >= 60) {
			shell_error(shell, "invalid UTC offset");
			return -EINVAL;
		}
		offset = hours * 60 + (sign == '-' ? -minutes : minutes);
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
	if (time_editor.active) {
		return;
	}
	if (position_editor.active) {
		if ((event & KB_EVENT_ARROWLEFT) != 0U) {
			return_from_position_editor();
		}
		return;
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
		(void)k_msgq_put(&keyboard_rx_msgq, &byte, K_NO_WAIT);
	}
}

static void process_keyboard_output(void)
{
	uint8_t key;
	uint32_t event;

	while (ef800_keyboard_host_get_key(&keyboard_host, &key) == 0) {
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

	result = ef800_keyboard_host_feed_at(&keyboard_host, byte, now_ms);
	switch (result) {
	case EF800_HOST_FEED_READY:
		LOG_INF("Keyboard host ready indication received");
		break;
	case EF800_HOST_FEED_COMMAND:
		LOG_DBG("Keyboard command 0x%02x status 0x%02x",
			keyboard_host.last_command,
			keyboard_host.last_command_status);
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
	int result;
	uint16_t mcp_port;

	(void)ef800_main_foundation_init();
	result = ef800_persistence_init(&persistence,
				       ef800_persistence_zephyr_backend());
	if (result != 0) {
		LOG_ERR("Persistence service initialization failed: %d", result);
	} else {
		result = ef800_port_assignment_init(&port_assignments, &persistence);
		if (result != 0) {
			LOG_ERR("Port assignment service initialization failed: %d", result);
		} else {
			result = load_boot_metadata();
			if (result != 0) {
				LOG_WRN("Boot metadata read failed: %d", result);
			}
			if (boot_metadata_valid && !ef800_storage_is_mounted()) {
				result = import_metadata_assignments();
				if (result != 0) {
					LOG_WRN("Boot metadata assignments unavailable: %d", result);
				}
			}
			result = ef800_workspace_init(&workspace, &persistence);
			if (result != 0) {
				LOG_ERR("Workspace service initialization failed: %d", result);
			} else if (ef800_storage_is_mounted()) {
				result = ef800_workspace_load_current(&workspace);
				if (result != 0 && result != -ENOENT) {
					LOG_WRN("Current Workspace unavailable: %d", result);
				}
				result = ef800_port_assignment_load_all(&port_assignments);
				if (result != 0) {
					LOG_WRN("Port assignments unavailable: %d", result);
				}
			}
			result = reconcile_port_uart_state();
			if (result != 0) {
				LOG_WRN("Port routing initialization incomplete: %d", result);
			}
		}
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
	if (result == 0) {
		result = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	}
	if (result == 0) {
		k_msleep(10U);
		result = gpio_pin_set_dt(&keyboard_reset, 1);
	}
	if (result != 0) {
		LOG_WRN("MCP23017 controls unavailable: %d", result);
	} else if (ef800_mcp23017_read(&mcp23017, &mcp_port) == 0) {
		LOG_INF("MCP23017 controls initialized (port=0x%04x)", mcp_port);
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

	result = ef800_t6963_init(&lcd, &lcd_config);
	if (result == 0) {
		result = ef800_t6963_initialize(&lcd);
	}
	if (result != 0) {
		LOG_WRN("T6963 LCD unavailable; continuing without display: %d", result);
	} else {
		lcd_ready = true;
		display_banner();
	}

#if DT_HAS_COMPAT_STATUS_OKAY(maxim_max17043)
	result = ef800_max17043_init(&fuel_gauge, &fuel_gauge_i2c);
	if (result != 0) {
		LOG_WRN("MAX17043 unavailable; continuing without battery status: %d",
			result);
	} else {
		fuel_gauge_ready = true;
	}
#else
	LOG_INF("MAX17043 disabled: no enabled fuel_gauge node");
#endif

	update_lcd_status();

	if (!device_is_ready(keyboard_uart)) {
		LOG_ERR("Keyboard UART is not ready");
		return 0;
	}
	result = configure_keyboard_uart();
	if (result != 0) {
		LOG_ERR("Keyboard UART configuration failed: %d", result);
		return 0;
	}

	ef800_keyboard_host_init(&keyboard_host);
	uart_irq_callback_user_data_set(keyboard_uart, keyboard_uart_isr, NULL);
	uart_irq_rx_enable(keyboard_uart);
	if (wait_for_keyboard_startup()) {
		display_line(6U, "Keyboard ready");
	}
	display_menu();

	while (true) {
		if (k_msgq_get(&keyboard_rx_msgq, &byte, K_MSEC(1000)) == 0) {
			process_keyboard_byte(byte, k_uptime_get_32());
		}
		while (k_msgq_get(&keyboard_rx_msgq, &byte, K_NO_WAIT) == 0) {
			process_keyboard_byte(byte, k_uptime_get_32());
		}
		update_lcd_status();
	}
}
