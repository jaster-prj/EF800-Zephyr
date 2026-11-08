/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>

#include <cmsis_core.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "ef800_keyboard_boot.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"

LOG_MODULE_REGISTER(ef800_keyboard_bootloader, LOG_LEVEL_INF);

#define APP_PARTITION_ID PARTITION_ID(keyboard_app_partition)
#define UPDATER_PARTITION_ID PARTITION_ID(keyboard_updater_partition)
#define UPDATER_I2C_ADDRESS 0x48U

BUILD_ASSERT(PARTITION_OFFSET(keyboard_boot_partition) == 0U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_boot_partition) == 0x8000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_app_partition) == 0x8000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_app_partition) == 0x30000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_updater_partition) == 0x38000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_updater_partition) == 0x8000U);

static const uintptr_t sram_address = DT_REG_ADDR(DT_NODELABEL(sram0));
static const size_t sram_size = DT_REG_SIZE(DT_NODELABEL(sram0));
static const struct device *const i2c_target =
	DEVICE_DT_GET(DT_NODELABEL(i2c1));
static const struct gpio_dt_spec updater_request =
	GPIO_DT_SPEC_GET_OR(DT_PATH(zephyr_user), updater_request_gpios, {0});
static atomic_t updater_probe_received;
static bool i2c_target_registered;

static int i2c_write_requested(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	/* The address match itself is the bootloader probe. Do not depend on a
	 * data byte or STOP callback: some controller/target combinations only
	 * reliably report the address phase for this short startup handshake. */
	if (ef800_keyboard_boot_i2c_probe(true, false)) {
		atomic_set(&updater_probe_received, 1);
	}
	return 0;
}

static int i2c_write_received(struct i2c_target_config *config, uint8_t byte)
{
	ARG_UNUSED(config);
	ARG_UNUSED(byte);
	return 0;
}

static int i2c_stop(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	return 0;
}

static const struct i2c_target_callbacks i2c_callbacks = {
	.write_requested = i2c_write_requested,
	.write_received = i2c_write_received,
	.stop = i2c_stop,
};

static struct i2c_target_config i2c_config = {
	.address = UPDATER_I2C_ADDRESS,
	.callbacks = &i2c_callbacks,
};

static bool app_valid(const struct flash_area *area)
{
	uint32_t vector[2];

	return flash_area_read(area, 0U, vector, sizeof(vector)) == 0 &&
		ef800_vector_table_valid(vector, PARTITION_ADDRESS(keyboard_app_partition),
			area->fa_size, sram_address, sram_size);
}

__attribute__((naked, noreturn)) static void branch_to(uint32_t msp,
	uint32_t reset_handler)
{
	__asm__ volatile (
		"msr msp, r0\n"
		"movs r0, #0\n"
		"msr control, r0\n"
		"cpsie i\n"
		"isb\n"
		"bx r1\n");
}

static FUNC_NORETURN void jump_to_image(uintptr_t address, size_t size)
{
	const uint32_t *vector = (const uint32_t *)address;

	if (!ef800_vector_table_valid(vector, address, size, sram_address, sram_size)) {
		LOG_ERR("invalid boot target 0x%lx", (unsigned long)address);
		for (;;) {
			k_sleep(K_FOREVER);
		}
	}
	__disable_irq();
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	for (size_t i = 0U; i < ARRAY_SIZE(NVIC->ICER); ++i) {
		NVIC->ICER[i] = UINT32_MAX;
		NVIC->ICPR[i] = UINT32_MAX;
	}
	SCB->VTOR = (uint32_t)address;
	__DSB();
	__ISB();
	branch_to(vector[0], vector[1]);
}

static bool updater_valid(void)
{
	const uint32_t *vector =
		(const uint32_t *)PARTITION_ADDRESS(keyboard_updater_partition);

	return ef800_vector_table_valid(vector,
		PARTITION_ADDRESS(keyboard_updater_partition),
		PARTITION_SIZE(keyboard_updater_partition), sram_address, sram_size);
}

static bool updater_requested_by_gpio(void)
{
	int result;

	if (updater_request.port == NULL ||
	    !device_is_ready(updater_request.port)) {
		LOG_WRN("keyboard boot: updater request GPIO unavailable");
		return false;
	}
	result = gpio_pin_configure_dt(&updater_request, GPIO_INPUT);
	if (result != 0) {
		LOG_WRN("keyboard boot: updater request GPIO configure failed: %d",
			result);
		return false;
	}
	result = gpio_pin_get_dt(&updater_request);
	if (result < 0) {
		LOG_WRN("keyboard boot: updater request GPIO read failed: %d",
			result);
		return false;
	}
	return result == 0;
}

static FUNC_NORETURN void enter_updater(void)
{
	if (i2c_target_registered &&
	    i2c_target_unregister(i2c_target, &i2c_config) != 0) {
		LOG_WRN("keyboard boot: I2C target unregister failed");
	}
	i2c_target_registered = false;
	if (!updater_valid()) {
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

static bool updater_probe_window(void)
{
	uint32_t started_ms;
	int result;

	if (!device_is_ready(i2c_target)) {
		LOG_ERR("keyboard boot: I2C target unavailable");
		return false;
	}
	result = i2c_target_register(i2c_target, &i2c_config);
	if (result != 0) {
		LOG_ERR("keyboard boot: I2C target registration failed: %d", result);
		return false;
	}
	i2c_target_registered = true;
	started_ms = k_uptime_get_32();
	while (!ef800_keyboard_boot_updater_window_expired(started_ms,
							 k_uptime_get_32())) {
		if (atomic_cas(&updater_probe_received, 1, 0)) {
			return true;
		}
		(void)ef800_watchdog_feed();
		k_sleep(K_MSEC(1));
	}
	(void)i2c_target_unregister(i2c_target, &i2c_config);
	i2c_target_registered = false;
	return false;
}

int main(void)
{
	const struct flash_area *app;
	struct ef800_keyboard_boot_state *state = ef800_keyboard_boot_state_get();
	uint32_t cause = 0U;
	bool watchdog;
	bool recovery_required;
	bool application_is_valid;

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
	if (updater_requested_by_gpio()) {
		LOG_INF("keyboard boot: PB12 updater request asserted");
		enter_updater();
	}
	if (ef800_keyboard_boot_should_enter_updater(false,
			recovery_required, true)) {
		LOG_WRN("keyboard boot: watchdog recovery limit reached; entering updater");
		enter_updater();
	}
	if (ef800_keyboard_boot_should_enter_updater(updater_probe_window(),
			false, true)) {
		LOG_INF("keyboard boot: updater probe received");
		enter_updater();
	}

	if (flash_area_open(APP_PARTITION_ID, &app) != 0 ||
		!flash_area_device_is_ready(app)) {
		LOG_ERR("keyboard boot: application Flash unavailable; entering updater");
		enter_updater();
	}
	application_is_valid = app_valid(app);
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
