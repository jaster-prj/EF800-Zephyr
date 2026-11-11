/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/init.h>

#include <stm32l4xx_hal.h>

/*
 * AN4761's FMC examples enable VDDIO2 before configuring FMC GPIOs. The
 * STM32L496 FMC uses GPIOG pins, which are powered by this I/O supply.
 * Enable it before the POST_KERNEL FMC/pinctrl device initialization.
 */
static int ef800_sram_io_supply_init(void)
{
	__HAL_RCC_PWR_CLK_ENABLE();
	SET_BIT(PWR->CR2, PWR_CR2_IOSV);
	return 0;
}

SYS_INIT(ef800_sram_io_supply_init, PRE_KERNEL_1, 0);
