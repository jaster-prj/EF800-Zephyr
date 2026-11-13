/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_app_usb_msc.h"

#include <errno.h>

/* USB MSC ownership is intentionally outside the native-sim profile. */
int ef800_app_usb_msc_start(void)
{
	return -ENOTSUP;
}

int ef800_app_usb_msc_stop(void)
{
	return 0;
}

bool ef800_app_usb_msc_is_active(void)
{
	return false;
}
