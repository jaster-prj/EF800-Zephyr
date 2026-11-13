/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief MainApplication Shell commands for RTC, composition, battery, memory,
 *        keyboard, and management diagnostics.
 */
#ifndef EF800_MAIN_SHELL_H
#define EF800_MAIN_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/shell/shell.h>

#include "ef800_battery_soc_store.h"
#include "ef800_composition.h"
#include "ef800_keyboard_command.h"

struct ef800_main_shell_context {
	struct ef800_battery_soc_store *battery_soc_store;
	bool *battery_soc_store_ready;
	struct ef800_composition_service *composition_service;
	bool *composition_service_ready;
	bool *mcumgr_transport_active;
	const struct device *keyboard_i2c;
	struct ef800_keyboard_command *keyboard_command;
	bool *keyboard_debug_active;
	/* Return 0 and write the detected version, or a negative errno. */
	int (*keyboard_boot_probe)(uint8_t *version);
	/* Upload adapter; returns a shell-command status code. */
	int (*keyboard_upload)(const struct shell *shell, size_t argc, char **argv);
	/* Called after a composition change has been committed successfully. */
	void (*composition_committed)(void *context);
	/* Opaque argument supplied to composition_committed. */
	void *context;
};

/** @brief Bind application-owned services and callbacks used by shell commands.
 * @param context Complete service/context bundle; required members are checked.
 * @return 0 when bound, or -EINVAL when required pointers/callbacks are absent.
 */
int ef800_main_shell_init(const struct ef800_main_shell_context *context);

#endif /* EF800_MAIN_SHELL_H */
