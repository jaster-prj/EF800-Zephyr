/* SPDX-License-Identifier: Apache-2.0 */

/* This hook runs before Zephyr initializes MSP/RAM or device drivers. */
__attribute__((naked, used)) void soc_early_reset_hook(void)
{
	__asm__ volatile (
		"ldr r0, =0x40003000\n"
		"movw r1, #0xaaaa\n"
		"str r1, [r0, #0]\n"
		"bx lr\n"
	);
}
