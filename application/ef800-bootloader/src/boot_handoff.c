/* SPDX-License-Identifier: Apache-2.0 */

#include "boot_handoff.h"

#include <stdbool.h>
#include <errno.h>
#include <stdint.h>

#include <cmsis_core.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "ef800_update.h"

LOG_MODULE_REGISTER(ef800_boot_handoff, LOG_LEVEL_INF);

#define EF800_VECTOR_BYTES (2U * sizeof(uint32_t))

static int read_vector(const struct flash_area *area, uint32_t vector[2])
{
	if (!flash_area_device_is_ready(area) || area->fa_size < EF800_VECTOR_BYTES) {
		return -ENODEV;
	}

	return flash_area_read(area, 0U, vector, EF800_VECTOR_BYTES);
}

__attribute__((naked, noreturn)) static void branch_to_application(
	uint32_t stack_pointer, uint32_t reset_handler)
{
	__asm__ volatile (
		"msr msp, r0\n"
		"movs r0, #0\n"
		"msr control, r0\n"
		"msr basepri, r0\n"
		"msr faultmask, r0\n"
		"isb\n"
		"cpsie i\n"
		"bx r1\n"
	);
}

FUNC_NORETURN void ef800_boot_handoff(const struct flash_area *area,
				      uintptr_t address,
				      uintptr_t sram_address,
				      size_t sram_size)
{
	uint32_t vector[2];

	if (read_vector(area, vector) != 0 ||
		!ef800_vector_table_valid(vector, address, area->fa_size,
					  sram_address, sram_size)) {
		LOG_ERR("refusing handoff to invalid image at 0x%lx",
			(unsigned long)address);
		while (true) {
			k_sleep(K_FOREVER);
		}
	}
	__disable_irq();
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	for (size_t index = 0U; index < ARRAY_SIZE(NVIC->ICER); ++index) {
		NVIC->ICER[index] = UINT32_MAX;
		NVIC->ICPR[index] = UINT32_MAX;
	}
	MPU->CTRL = 0U;
	SCB->CFSR = SCB->CFSR;
	SCB->HFSR = SCB->HFSR;
	SCB->VTOR = (uint32_t)address;
	__DSB();
	__ISB();
	branch_to_application(vector[0], vector[1]);
}
