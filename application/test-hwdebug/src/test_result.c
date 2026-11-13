/* SPDX-License-Identifier: Apache-2.0 */

#include "test_result.h"

#include <zephyr/sys/printk.h>

static bool failed;

void hwdebug_test_begin(const char *name)
{
	failed = false;
	printk("\nRUN   %s\r\n", name);
}

void hwdebug_test_fail(const char *description, int error)
{
	failed = true;
	printk("ERROR %s (error=%d)\r\n", description, error);
}

void hwdebug_test_pass(void)
{
	printk("%s\r\n", failed ? "FAIL" : "PASS");
}

bool hwdebug_test_failed(void)
{
	return failed;
}
