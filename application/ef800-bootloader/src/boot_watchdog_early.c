/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>

#include <stm32_ll_iwdg.h>

static int ef800_bootloader_watchdog_early_feed(void)
{
#if DT_HAS_ALIAS(watchdog0) && DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
	/* Option bytes may start IWDG before Zephyr reaches the application. */
	LL_IWDG_ReloadCounter(IWDG);
#endif
	return 0;
}

SYS_INIT(ef800_bootloader_watchdog_early_feed, PRE_KERNEL_1, 0);
