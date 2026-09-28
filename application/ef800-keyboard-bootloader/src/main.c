/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>

#include <cmsis_core.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "ef800_keyboard_boot.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"

LOG_MODULE_REGISTER(ef800_keyboard_bootloader, LOG_LEVEL_INF);

#define APP_PARTITION_ID PARTITION_ID(keyboard_app_partition)
#define SYSTEM_MEMORY_ADDRESS 0x1FFF0000UL

BUILD_ASSERT(PARTITION_OFFSET(keyboard_boot_partition) == 0U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_boot_partition) == 0x8000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_app_partition) == 0x8000U);

static const uintptr_t sram_address = DT_REG_ADDR(DT_NODELABEL(sram0));
static const size_t sram_size = DT_REG_SIZE(DT_NODELABEL(sram0));

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

static FUNC_NORETURN void jump_to_application(uintptr_t address, size_t size)
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

static FUNC_NORETURN void jump_system_memory(void)
{
	const uint32_t *vector = (const uint32_t *)SYSTEM_MEMORY_ADDRESS;

	/* STM32L431 system memory contains the factory serial bootloader. */
	__disable_irq();
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	for (size_t i = 0U; i < ARRAY_SIZE(NVIC->ICER); ++i) {
		NVIC->ICER[i] = UINT32_MAX;
		NVIC->ICPR[i] = UINT32_MAX;
	}
	SCB->VTOR = SYSTEM_MEMORY_ADDRESS;
	__DSB();
	__ISB();
	branch_to(vector[0], vector[1]);
}

int main(void)
{
	const struct flash_area *app;
	struct ef800_keyboard_boot_state *state = ef800_keyboard_boot_state_get();
	uint32_t cause = 0U;
	bool watchdog;
	bool system_request;

	(void)hwinfo_get_reset_cause(&cause);
	watchdog = (cause & RESET_WATCHDOG) != 0U;
	(void)hwinfo_clear_reset_cause();
	if ((cause & (RESET_POR | RESET_BROWNOUT)) != 0U) {
		ef800_keyboard_boot_state_initialize(state);
	}
	system_request = ef800_keyboard_boot_system_request_pending(state);
	if (ef800_keyboard_boot_prepare(state, watchdog, cause) || system_request) {
		(void)ef800_keyboard_boot_consume_system_request(state);
		LOG_INF("keyboard boot: entering system bootloader without IWDG");
		jump_system_memory();
	}

	/* Start the software IWDG only for normal application execution. */
	if (ef800_watchdog_start(10000U) != 0) {
		LOG_ERR("keyboard IWDG could not be started");
		jump_system_memory();
	}

	if (flash_area_open(APP_PARTITION_ID, &app) != 0 ||
		!flash_area_device_is_ready(app)) {
		(void)ef800_watchdog_feed();
		jump_system_memory();
	}
	if (!app_valid(app)) {
		(void)ef800_watchdog_feed();
		jump_system_memory();
	}
	(void)flash_area_close(app);
	jump_to_application(PARTITION_ADDRESS(keyboard_app_partition),
		PARTITION_SIZE(keyboard_app_partition));
}
