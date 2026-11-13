/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "ef800_keyboard_boot.h"
#include "ef800_watchdog.h"
#include "keyboard_boot_handoff.h"
#include "keyboard_boot_flash.h"
#include "keyboard_boot_probe.h"

LOG_MODULE_REGISTER(ef800_keyboard_bootloader, LOG_LEVEL_INF);

#define APP_PARTITION_ID PARTITION_ID(keyboard_app_partition)
#define UPDATER_PARTITION_ID PARTITION_ID(keyboard_updater_partition)

BUILD_ASSERT(PARTITION_OFFSET(keyboard_boot_partition) == 0U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_boot_partition) == 0x8000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_app_partition) == 0x8000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_app_partition) == 0x30000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_updater_partition) == 0x38000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_updater_partition) == 0x8000U);


static FUNC_NORETURN void jump_to_image(uintptr_t address, size_t size)
{
	if (!ef800_keyboard_boot_handoff(address, size)) {
		LOG_ERR("invalid boot target 0x%lx", (unsigned long)address);
		for (;;) {
			k_sleep(K_FOREVER);
		}
	}
	for (;;) {
		k_sleep(K_FOREVER);
	}
}

static FUNC_NORETURN void enter_updater(void)
{
	if (ef800_keyboard_boot_probe_unregister() != 0) {
		LOG_WRN("keyboard boot: I2C target unregister failed");
	}
	if (!ef800_keyboard_boot_updater_valid()) {
		LOG_ERR("keyboard boot: updater image invalid");
		for (;;) {
			(void)ef800_watchdog_feed();
			k_sleep(K_SECONDS(1));
		}
	}
	(void)ef800_watchdog_feed();
	jump_to_image(PARTITION_ADDRESS(keyboard_updater_partition),
		PARTITION_SIZE(keyboard_updater_partition));
}

int main(void)
{
	const struct flash_area *app;
	struct ef800_keyboard_boot_state *state = ef800_keyboard_boot_state_get();
	uint32_t cause = 0U;
	bool watchdog;
	bool recovery_required;
	bool application_is_valid;
	bool gpio_updater_request;
	bool i2c_updater_probe = false;
	int probe_result;

	(void)hwinfo_get_reset_cause(&cause);
	watchdog = (cause & RESET_WATCHDOG) != 0U;
	(void)hwinfo_clear_reset_cause();
	if ((cause & (RESET_POR | RESET_BROWNOUT)) != 0U) {
		ef800_keyboard_boot_state_initialize(state);
	}
	recovery_required = ef800_keyboard_boot_prepare(state, watchdog, cause);

	/* Start or adopt IWDG before the bounded updater-entry window. */
	if (ef800_watchdog_start(10000U) != 0) {
		LOG_ERR("keyboard IWDG could not be started");
	}
	probe_result = ef800_keyboard_boot_probe_gpio(&gpio_updater_request);
	if (probe_result != 0) {
		LOG_WRN("keyboard boot: updater request GPIO unavailable: %d",
			probe_result);
	}
	if (gpio_updater_request) {
		LOG_INF("keyboard boot: PB12 updater request asserted");
		enter_updater();
	}
	if (ef800_keyboard_boot_should_enter_updater(false,
			recovery_required, true)) {
		LOG_WRN("keyboard boot: watchdog recovery limit reached; entering updater");
		enter_updater();
	}
	probe_result = ef800_keyboard_boot_probe_window(&i2c_updater_probe);
	if (probe_result != 0) {
		LOG_ERR("keyboard boot: I2C probe unavailable: %d", probe_result);
	}
	if (ef800_keyboard_boot_should_enter_updater(i2c_updater_probe,
			false, true)) {
		LOG_INF("keyboard boot: updater probe received");
		enter_updater();
	}

	if (flash_area_open(APP_PARTITION_ID, &app) != 0 ||
		!flash_area_device_is_ready(app)) {
		LOG_ERR("keyboard boot: application Flash unavailable; entering updater");
		enter_updater();
	}
	application_is_valid = ef800_keyboard_boot_application_valid(app);
	if (ef800_keyboard_boot_should_enter_updater(false, false,
			application_is_valid)) {
		(void)flash_area_close(app);
		LOG_ERR("keyboard boot: application invalid; entering updater");
		enter_updater();
	}
	(void)flash_area_close(app);
	jump_to_image(PARTITION_ADDRESS(keyboard_app_partition),
		PARTITION_SIZE(keyboard_app_partition));
}
