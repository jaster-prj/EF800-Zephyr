/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_boot_handoff.h"

#include <cmsis_core.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include "ef800_update.h"

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

bool ef800_keyboard_boot_handoff(uintptr_t address, size_t size)
{
	const uint32_t *vector = (const uint32_t *)address;
	const uintptr_t sram_address = DT_REG_ADDR(DT_NODELABEL(sram0));
	const size_t sram_size = DT_REG_SIZE(DT_NODELABEL(sram0));

	if (!ef800_vector_table_valid(vector, address, size,
				      sram_address, sram_size)) {
		return false;
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
	SCB->VTOR = (uint32_t)address;
	__DSB();
	__ISB();
	branch_to(vector[0], vector[1]);
}
