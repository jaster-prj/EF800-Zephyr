/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_watchdog.h"

#include <errno.h>

#include <zephyr/kernel.h>

/*
 * native_sim has no STM32 IWDG register block. Keep the production watchdog
 * service contract alive while making watchdog behavior observable through
 * the normal application task and scheduler.
 */
static bool watchdog_started;
static unsigned int watchdog_timeout_ms;

int ef800_watchdog_start(unsigned int timeout_ms)
{
	watchdog_timeout_ms = timeout_ms;
	watchdog_started = timeout_ms != 0U;
	return watchdog_started ? 0 : -EINVAL;
}

int ef800_watchdog_inherit(void)
{
	watchdog_started = true;
	return 0;
}

int ef800_watchdog_feed(void)
{
	return watchdog_started ? 0 : -EAGAIN;
}

bool ef800_watchdog_available(void)
{
	return watchdog_started || watchdog_timeout_ms != 0U;
}
