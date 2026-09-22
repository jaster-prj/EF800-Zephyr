/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_watchdog.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <stm32_ll_iwdg.h>

#if DT_HAS_ALIAS(watchdog0)
static const struct device *const watchdog = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int watchdog_channel = -1;
static bool watchdog_inherited;
#endif

static int ef800_watchdog_early_feed(void)
{
#if DT_HAS_ALIAS(watchdog0) && DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
	/* The bootloader hands off without a CPU reset, so IWDG remains active. */
	LL_IWDG_ReloadCounter(IWDG);
#endif
	return 0;
}

SYS_INIT(ef800_watchdog_early_feed, PRE_KERNEL_1, 0);

int ef800_watchdog_start(unsigned int timeout_ms)
{
#if DT_HAS_ALIAS(watchdog0)
	const struct wdt_timeout_cfg timeout = {
		.window.min = 0U,
		.window.max = timeout_ms,
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int ret;

	if (!device_is_ready(watchdog)) {
		return -ENODEV;
	}
	watchdog_channel = wdt_install_timeout(watchdog, &timeout);
	if (watchdog_channel == -EALREADY) {
		/* The bootloader already started IWDG; keep feeding that instance. */
		watchdog_inherited = true;
		LL_IWDG_ReloadCounter(IWDG);
		return 0;
	}
	if (watchdog_channel < 0) {
		return watchdog_channel;
	}
	ret = wdt_setup(watchdog, 0U);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	return wdt_feed(watchdog, watchdog_channel);
#else
	ARG_UNUSED(timeout_ms);
	return -ENODEV;
#endif
}

int ef800_watchdog_feed(void)
{
#if DT_HAS_ALIAS(watchdog0)
	if (watchdog_inherited) {
		LL_IWDG_ReloadCounter(IWDG);
		return 0;
	}
	return watchdog_channel >= 0 ? wdt_feed(watchdog, watchdog_channel) : -EAGAIN;
#else
	return -ENODEV;
#endif
}

bool ef800_watchdog_available(void)
{
#if DT_HAS_ALIAS(watchdog0)
	return device_is_ready(watchdog);
#else
	return false;
#endif
}
