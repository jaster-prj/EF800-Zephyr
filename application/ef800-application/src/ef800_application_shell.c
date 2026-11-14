/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>

#include "ef800_application_shell.h"
#include "ef800_app.h"
#include "ef800_memory_pool.h"
#include "ef800_mcumgr_transport.h"
#include "ef800_keyboard_i2c.h"
#include "ef800_keyboard_update.h"
#include "ef800_keyboard_update_service.h"
#include "ef800_keyboard_update_protocol.h"
#include "ef800_persistence.h"
#include "ef800_storage_owner.h"
#include "ef800_time.h"

static struct ef800_application_shell_context shell_context;

int ef800_application_shell_init(const struct ef800_application_shell_context *context)
{
	if (context == NULL || context->battery_soc_store == NULL ||
	    context->battery_soc_store_ready == NULL ||
	    context->composition_service == NULL ||
	    context->composition_service_ready == NULL ||
	    context->mcumgr_transport_active == NULL ||
	    context->keyboard_command == NULL ||
	    context->keyboard_debug_active == NULL ||
	    context->keyboard_boot_probe == NULL ||
	    context->keyboard_upload == NULL ||
	    context->backup_update == NULL ||
	    context->bootloader_update == NULL) {
		return -EINVAL;
	}
	shell_context = *context;
	return 0;
}

/** @brief Print local RTC time and the active offset/DST settings.
 * @param shell Shell receiving output and errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 on read; negative errno if the RTC is unavailable.
 */
static int cmd_rtc_get(const struct shell *shell, size_t argc, char **argv)
{
	struct rtc_time local;
	const struct ef800_time_settings *settings;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (ef800_app_rtc_get_local_time(&local) != 0) {
		shell_error(shell, "RTC unavailable");
		return -ENODEV;
	}
	settings = ef800_app_get_time_settings();
	shell_print(shell, "%04d-%02d-%02dT%02d:%02d:%02d offset=%d dst=%d",
		local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
		local.tm_hour, local.tm_min, local.tm_sec,
		settings->utc_offset_minutes, settings->daylight_saving ? 1 : 0);
	return 0;
}

/** @brief Parse and apply a local date/time, UTC offset, and DST setting.
 * @param shell Shell receiving status and errors.
 * @param argc Expected to be four, including the command name.
 * @param argv Timestamp, offset, and DST arguments.
 * @return 0 on update; negative errno for syntax, validation, or RTC failure.
 */
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
	if ((dst != 0 && dst != 1) || !ef800_rtc_time_valid(&local) ||
	    offset < -24 * 60 || offset > 24 * 60) {
		shell_error(shell, "invalid date/time, offset, or DST value");
		return -EINVAL;
	}
	if (ef800_app_rtc_set_local_time(&local, &settings) != 0) {
		shell_error(shell, "RTC update failed");
		return -EIO;
	}
	shell_print(shell, "RTC updated");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(rtc_commands,
	SHELL_CMD_ARG(get, NULL, "Read local RTC time", cmd_rtc_get, 1, 0),
	SHELL_CMD_ARG(set, NULL,
		"Set local time and UTC offset: YYYY-MM-DDTHH:MM:SS offset dst",
		cmd_rtc_set, 4, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(rtc, &rtc_commands, "EF800 RTC commands", NULL);

/** @brief Find the active BATSOC catalog entry in the current composition.
 * @return Active entry, or NULL when the service/group is unavailable.
 */
static const struct ef800_composition_catalog_entry *battery_source_active(void)
{
	const struct ef800_composition_snapshot *snapshot =
		ef800_composition_service_active(shell_context.composition_service);

	if (snapshot == NULL) {
		return NULL;
	}
	for (size_t index = 0U; index < snapshot->effective_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			snapshot->effective[index].catalog_entry;

		if (entry != NULL && strcmp(entry->group, "BATSOC") == 0) {
			return entry;
		}
	}
	return NULL;
}

/** @brief Print the installed battery SOC calibration table and encoding.
 * @param shell Shell receiving output and errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 on display; negative errno when storage or table access fails.
 */
static int cmd_battery_soc_table_show(const struct shell *shell, size_t argc,
	char **argv)
{
	struct ef800_battery_soc_table table;
	char hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	size_t hex_length;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (*shell_context.battery_soc_store_ready == false) {
		shell_error(shell, "battery SOC calibration storage unavailable");
		return -ENOTSUP;
	}
	ret = ef800_battery_soc_store_get_table(shell_context.battery_soc_store,
		&table);
	if (ret == -ENOENT) {
		shell_print(shell, "no battery SOC calibration table is installed");
		return 0;
	}
	if (ret != 0) {
		shell_error(shell, "battery SOC table read failed: %d", ret);
		return ret;
	}
	ret = ef800_battery_soc_store_get_hex(shell_context.battery_soc_store,
		hex, sizeof(hex), &hex_length);
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

/** @brief Validate and persist a battery SOC table supplied as hexadecimal.
 * @param shell Shell receiving output and errors.
 * @param argc Expected to be two, including the command name.
 * @param argv Hexadecimal table argument.
 * @return 0 on stored table; negative errno on syntax, storage, or validation failure.
 */
static int cmd_battery_soc_table_set(const struct shell *shell, size_t argc,
	char **argv)
{
	int ret;

	if (argc != 2U) {
		shell_error(shell, "usage: battery table set <hex-string>");
		return -EINVAL;
	}
	if (!*shell_context.battery_soc_store_ready ||
	    !ef800_storage_configuration_is_mounted()) {
		shell_error(shell, "battery SOC calibration storage unavailable");
		return -ENOTSUP;
	}
	ret = ef800_battery_soc_store_set_hex(shell_context.battery_soc_store,
		argv[1], strlen(argv[1]));
	if (ret != 0) {
		shell_error(shell, "battery SOC table rejected or not stored: %d", ret);
		return ret;
	}
	shell_print(shell, "battery SOC table verified and stored");
	return 0;
}

/** @brief Print the active BATSOC composition source.
 * @param shell Shell receiving output and errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 when the composition service is available; otherwise negative errno.
 */
static int cmd_battery_soc_source_get(const struct shell *shell, size_t argc,
	char **argv)
{
	const struct ef800_composition_catalog_entry *entry;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!*shell_context.composition_service_ready) {
		shell_error(shell, "COMPOSITION service unavailable");
		return -ENOTSUP;
	}
	entry = battery_source_active();
	shell_print(shell, "active BATSOC source: %s%s",
		entry != NULL ? entry->token : "BATSOC2",
		ef800_composition_service_restart_required(
			shell_context.composition_service) ?
			" (restart required for committed selection)" : "");
	return 0;
}

/** @brief Stage and commit a BATSOC source selection.
 * @param shell Shell receiving output and errors.
 * @param argc Expected to be two, including the command name.
 * @param argv Requested BATSOC catalog token.
 * @return 0 on commit; negative errno when unavailable, invalid, or uncommittable.
 */
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
	if (!*shell_context.composition_service_ready ||
	    !ef800_storage_configuration_is_mounted()) {
		shell_error(shell, "COMPOSITION Configuration storage unavailable");
		return -ENOTSUP;
	}
	if (ef800_composition_service_restart_required(
			shell_context.composition_service)) {
		shell_error(shell, "a COMPOSITION change is already committed; restart first");
		return -EALREADY;
	}
	active = battery_source_active();
	if (active != NULL && strcmp(active->token, argv[1]) == 0) {
		shell_print(shell, "%s is already active", argv[1]);
		return 0;
	}
	ret = ef800_composition_service_stage_group_selection(
		shell_context.composition_service, "BATSOC", argv[1]);
	if (ret == 0) {
		ret = ef800_composition_service_commit(shell_context.composition_service);
	}
	if (ret != 0) {
		ef800_composition_service_discard(shell_context.composition_service);
		shell_error(shell, "BATSOC source selection failed: %d", ret);
		return ret;
	}
	if (shell_context.composition_committed != NULL) {
		shell_context.composition_committed(shell_context.context);
	}
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

/** @brief Print the effective USB-MSD composition mode.
 * @param shell Shell receiving output and errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 when found; negative errno if composition is unavailable.
 */
static int cmd_usb_msd_get(const struct shell *shell, size_t argc, char **argv)
{
	const struct ef800_composition_snapshot *snapshot;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!*shell_context.composition_service_ready) {
		shell_error(shell, "COMPOSITION service unavailable");
		return -ENOTSUP;
	}
	snapshot = ef800_composition_service_active(shell_context.composition_service);
	if (snapshot != NULL) {
		for (size_t index = 0U; index < snapshot->effective_count; ++index) {
			const struct ef800_composition_catalog_entry *entry =
				snapshot->effective[index].catalog_entry;

			if (entry != NULL && strcmp(entry->group, "USBMSD") == 0) {
				shell_print(shell, "active USBMSD mode: %s%s", entry->token,
					ef800_composition_service_restart_required(
						shell_context.composition_service) ?
						" (restart required)" : "");
				return 0;
			}
		}
	}
	shell_error(shell, "USBMSD composition selection unavailable");
	return -ENODEV;
}

/** @brief Stage and commit USBMSD0/USBMSD1 mode selection.
 * @param shell Shell receiving output and errors.
 * @param argc Expected to be two, including the command name.
 * @param argv Requested USBMSD catalog token.
 * @return 0 on commit; negative errno when invalid or unavailable.
 */
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
	if (!*shell_context.composition_service_ready ||
	    !ef800_storage_configuration_is_mounted()) {
		shell_error(shell, "COMPOSITION Configuration storage unavailable");
		return -ENOTSUP;
	}
	if (ef800_composition_service_restart_required(
		shell_context.composition_service)) {
		shell_error(shell, "a COMPOSITION change is already committed; restart first");
		return -EALREADY;
	}
	snapshot = ef800_composition_service_active(shell_context.composition_service);
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
	ret = ef800_composition_service_stage_group_selection(
		shell_context.composition_service, "USBMSD", argv[1]);
	if (ret == 0) {
		ret = ef800_composition_service_commit(shell_context.composition_service);
	}
	if (ret != 0) {
		ef800_composition_service_discard(shell_context.composition_service);
		shell_error(shell, "USBMSD composition commit failed: %d", ret);
		return ret;
	}
	if (shell_context.composition_committed != NULL) {
		shell_context.composition_committed(shell_context.context);
	}
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

/** @brief Start the raw MCUmgr filesystem transport.
 * @param shell Shell receiving output and errors.
 * @param argc Expected to be one, including the command name.
 * @param argv Unused argument vector.
 * @return 0 when started/already active; negative errno on invalid state/failure.
 */
static int cmd_mcumgr_start(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argv);
	if (argc != 1U) {
		shell_error(shell, "usage: mcumgr start");
		return -EINVAL;
	}
	if (*shell_context.mcumgr_transport_active) {
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
	*shell_context.mcumgr_transport_active = true;
	shell_print(shell,
		"MCUmgr raw FS transport active; reset EF800 Application to restore shell input");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mcumgr_commands,
	SHELL_CMD_ARG(start, NULL, "Start raw serial FS transport (shell pauses)",
		cmd_mcumgr_start, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mcumgr, &mcumgr_commands,
	"Raw serial filesystem transport", NULL);

/** @brief Query and print the keyboard application firmware version.
 * @param shell Shell receiving output and errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 on a valid version response; negative errno otherwise.
 */
static int cmd_keyboard_version(const struct shell *shell, size_t argc,
	const char *argv[])
{
	uint8_t request = EF800_KEYBOARD_I2C_CMD_STATUS;
	uint8_t response[EF800_KEYBOARD_I2C_STATUS_SIZE];
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ef800_keyboard_command_lock(shell_context.keyboard_command);
	if (shell_context.keyboard_i2c == NULL ||
	    !device_is_ready(shell_context.keyboard_i2c)) {
		result = -ENODEV;
	} else {
		result = i2c_write_read(shell_context.keyboard_i2c,
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
	ef800_keyboard_command_unlock(shell_context.keyboard_command);
	return result;
}

/** @brief Query and print the keyboard updater's live version.
 * @param shell Shell receiving output and errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 on a valid version response; negative errno otherwise.
 */
static int cmd_keyboard_updater_version(const struct shell *shell, size_t argc,
	const char *argv[])
{
	uint8_t response[EF800_KB_UPDATE_VERSION_SIZE];
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (shell_context.keyboard_i2c == NULL ||
	    !device_is_ready(shell_context.keyboard_i2c)) {
		result = -ENODEV;
	} else {
		result = ef800_keyboard_update_query_version(shell_context.keyboard_i2c,
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

/** @brief Exercise updater boot entry through the platform callback.
 * @param shell Shell receiving output and errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 when strap entry succeeds; callback error otherwise.
 */
static int cmd_keyboard_boot_probe(const struct shell *shell, size_t argc,
	const char *argv[])
{
	uint8_t version[EF800_KB_UPDATE_VERSION_SIZE] = {0};
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(shell, "Asserting JP7 updater request and resetting keyboard");
	result = shell_context.keyboard_boot_probe(version);
	if (result == 0) {
		shell_print(shell, "Bootloader strap PASS; updater version %u.%u.%u",
			version[1], version[2], version[3]);
		return 0;
	}
	shell_error(shell, "bootloader strap handoff failed: %d", result);
	return result;
}

/** @brief Delegate keyboard package upload to the EF800 Application adapter.
 * @param shell Shell used by the upload callback.
 * @param argc Upload command argument count.
 * @param argv Upload command arguments.
 * @return Result returned by the bound upload callback.
 */
static int cmd_keyboard_upload(const struct shell *shell, size_t argc,
	char **argv)
{
	return shell_context.keyboard_upload(shell, argc, argv);
}

/** @brief Disable physical scanning and enable keyboard debug input mode.
 * @param shell Shell receiving status/errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 when active/already active; negative errno on command failure.
 */
static int cmd_keyboard_debug_start(const struct shell *shell,
	size_t argc, char **argv)
{
	uint8_t enabled = 1U;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (*shell_context.keyboard_debug_active) {
		shell_print(shell, "keyboard debug already active");
		return 0;
	}
	result = ef800_keyboard_command_send(shell_context.keyboard_command,
		CMD_KEYBOARD_DEACTIVATE, NULL, 0U);
	if (result != 0) {
		shell_error(shell, "keyboard deactivate failed: %d", result);
		return result;
	}
	result = ef800_keyboard_command_send(shell_context.keyboard_command,
		CMD_KEYBOARD_SET_DEBUG, &enabled, 1U);
	if (result != 0) {
		shell_error(shell, "keyboard debug enable failed: %d", result);
		return result;
	}
	*shell_context.keyboard_debug_active = true;
	shell_print(shell, "keyboard debug active");
	return 0;
}

/** @brief Send a named virtual-key action through the serialized command service.
 * @param shell Shell receiving errors.
 * @param argc Command argument count; must include action and key name.
 * @param argv Key-name argument vector.
 * @param action Press or release action byte.
 * @return 0 on send; negative errno for invalid arguments or unavailable command path.
 */
static int cmd_keyboard_debug_key(const struct shell *shell,
	size_t argc, char **argv, uint8_t action)
{
	int result;

	if (argc != 2U) {
		shell_error(shell, "usage: keyboard debug %s <key-name>",
			action == KB_DEBUG_ACTION_PRESS ? "press" : "release");
		return -EINVAL;
	}
	result = ef800_keyboard_command_send_virtual_key(
		shell_context.keyboard_command, *shell_context.keyboard_debug_active,
		action, argv[1]);
	if (result != 0) {
		shell_error(shell, "keyboard debug request failed: %d", result);
	}
	return result;
}

/** @brief Send a debug-key press command.
 * @param shell Shell receiving errors.
 * @param argc Command argument count.
 * @param argv Key-name argument vector.
 * @return Result from the common virtual-key command handler.
 */
static int cmd_keyboard_debug_press(const struct shell *shell,
	size_t argc, char **argv)
{
	return cmd_keyboard_debug_key(shell, argc, argv, KB_DEBUG_ACTION_PRESS);
}

/** @brief Send a debug-key release command.
 * @param shell Shell receiving errors.
 * @param argc Command argument count.
 * @param argv Key-name argument vector.
 * @return Result from the common virtual-key command handler.
 */
static int cmd_keyboard_debug_release(const struct shell *shell,
	size_t argc, char **argv)
{
	return cmd_keyboard_debug_key(shell, argc, argv, KB_DEBUG_ACTION_RELEASE);
}

/** @brief Disable debug input and restore physical keyboard scanning.
 * @param shell Shell receiving status/errors.
 * @param argc Unused command argument count.
 * @param argv Unused argument vector.
 * @return 0 when stopped/already stopped; negative errno on command failure.
 */
static int cmd_keyboard_debug_stop(const struct shell *shell,
	size_t argc, char **argv)
{
	uint8_t disabled = 0U;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!*shell_context.keyboard_debug_active) {
		shell_print(shell, "keyboard debug is not active");
		return 0;
	}
	result = ef800_keyboard_command_send(shell_context.keyboard_command,
		CMD_KEYBOARD_SET_DEBUG, &disabled, 1U);
	if (result != 0) {
		shell_error(shell, "keyboard debug disable failed: %d", result);
		return result;
	}
	result = ef800_keyboard_command_send(shell_context.keyboard_command,
		CMD_KEYBOARD_ACTIVATE, NULL, 0U);
	if (result != 0) {
		shell_error(shell, "keyboard activate failed: %d", result);
		return result;
	}
	*shell_context.keyboard_debug_active = false;
	shell_print(shell, "keyboard debug stopped");
	return 0;
}

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
		"Assert JP7 updater request and reset into updater bootloader",
		cmd_keyboard_boot_probe, 1, 0),
	SHELL_CMD_ARG(upload, NULL,
		"Upload EF800_KEYBOARD_APP.pkg via DebugConsole",
		cmd_keyboard_upload, 1, 0),
	SHELL_CMD(debug, &keyboard_debug_commands, "Keyboard debug input", NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(keyboard, &keyboard_commands, "Keyboard commands", NULL);

#if CONFIG_EF800_APPLICATION_BACKUP_UPDATE
static int cmd_backup_update(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return shell_context.backup_update(shell);
}

SHELL_CMD_REGISTER(backup_update, NULL,
	"Validate and install the staged EF800 Backup package", cmd_backup_update);
#endif

#if CONFIG_EF800_APPLICATION_BOOTLOADER_UPDATE
static int cmd_bootloader_update(const struct shell *shell, size_t argc,
				 char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return shell_context.bootloader_update(shell);
}

SHELL_CMD_REGISTER(bootloader_update, NULL,
	"Manually validate and install staged EF800_BOOT.pkg",
	cmd_bootloader_update);
#endif

#if defined(CONFIG_THREAD_MONITOR) && defined(CONFIG_THREAD_STACK_INFO) && \
	defined(CONFIG_INIT_STACKS)
/** @brief Print stack capacity and unused bytes for one thread.
 * @param thread Thread whose stack watermark is reported.
 * @param user_data Shell pointer receiving the report.
 * @return None.
 */
static void report_thread_stack_usage(const struct k_thread *thread,
					      void *user_data)
{
	const struct shell *shell = user_data;
	size_t unused = 0U;
	const char *name = k_thread_name_get((k_tid_t)thread);
	int ret = k_thread_stack_space_get(thread, &unused);

	if (ret != 0) {
		shell_print(shell, "stack name=%s error=%d size=%u",
			name != NULL ? name : "(unnamed)", ret,
			(unsigned int)thread->stack_info.size);
		return;
	}
	shell_print(shell, "stack name=%s size=%u used=%u unused=%u",
		name != NULL ? name : "(unnamed)",
		(unsigned int)thread->stack_info.size,
		(unsigned int)(thread->stack_info.size - unused),
		(unsigned int)unused);
}
#endif

/** @brief Report thread stack high-water marks when configured.
 * @param shell Shell receiving output and errors.
 * @param argc Expected to be one, including the command name.
 * @param argv Unused argument vector.
 * @return 0 when reported; negative errno if invalid or unsupported.
 */
static int cmd_memory_stacks(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argv);
	if (argc != 1U) {
		shell_error(shell, "usage: memory stacks");
		return -EINVAL;
	}
#if defined(CONFIG_THREAD_MONITOR) && defined(CONFIG_THREAD_STACK_INFO) && \
	defined(CONFIG_INIT_STACKS)
	shell_print(shell, "stack watermark bytes; sample after normal startup/workload");
	k_thread_foreach_unlocked(report_thread_stack_usage, (void *)shell);
	return 0;
#else
	shell_error(shell, "stack watermark support not configured");
	return -ENOTSUP;
#endif
}

/** @brief Report pool capacities and verify one external-memory allocation.
 * @param shell Shell receiving output and errors.
 * @param argc Expected to be one, including the command name.
 * @param argv Unused argument vector.
 * @return 0 on allocation/readback; negative errno if unavailable or failed.
 */
static int cmd_memory_pool(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t *probe;

	ARG_UNUSED(argv);
	if (argc != 1U) {
		shell_error(shell, "usage: memory pool");
		return -EINVAL;
	}
	if (!ef800_memory_pool_external_ready()) {
		shell_print(shell,
			"profile=internal-only external=unavailable internal-malloc=%u",
			(unsigned int)ef800_memory_pool_internal_capacity());
		return -ENODEV;
	}
	probe = ef800_memory_pool_external_alloc(32U, 32U);
	if (probe == NULL) {
		shell_error(shell, "external pool allocation failed");
		return -ENOMEM;
	}
	probe[0] = 0xEF800123U;
	if (probe[0] != 0xEF800123U) {
		ef800_memory_pool_external_free(probe);
		shell_error(shell, "external pool readback failed at %p", probe);
		return -EIO;
	}
	shell_print(shell,
		"profile=external-enabled ext=%u used=%u internal-malloc=%u probe=%p size=32 readback=PASS",
		(unsigned int)ef800_memory_pool_external_capacity(),
		(unsigned int)ef800_memory_pool_external_used(),
		(unsigned int)ef800_memory_pool_internal_capacity(), probe);
	ef800_memory_pool_external_free(probe);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(memory_commands,
	SHELL_CMD(stacks, NULL, "Report per-thread stack high-water marks", cmd_memory_stacks),
	SHELL_CMD(pool, NULL, "Report pool profile and test an external allocation", cmd_memory_pool),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(memory, &memory_commands, "Memory diagnostics", NULL);
