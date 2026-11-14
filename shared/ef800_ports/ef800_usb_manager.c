/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_usb_manager.h"

#include <errno.h>

#include <zephyr/kernel.h>

#include "ef800_console.h"
#include "ef800_app_usb_mtp.h"
#include "ef800_app_usb_msc.h"

K_MUTEX_DEFINE(usb_manager_lock);
static enum ef800_usb_personality active_personality;

static int stop_personality(enum ef800_usb_personality personality)
{
	switch (personality) {
	case EF800_USB_PERSONALITY_DEBUG_CDC:
		return ef800_console_usb_stop();
	case EF800_USB_PERSONALITY_MTP:
		return ef800_app_usb_mtp_stop();
	case EF800_USB_PERSONALITY_NONE:
		return 0;
	case EF800_USB_PERSONALITY_MSC:
		return ef800_app_usb_msc_stop();
	default:
		return -EINVAL;
	}
}

static int start_personality(enum ef800_usb_personality personality)
{
	switch (personality) {
	case EF800_USB_PERSONALITY_DEBUG_CDC:
		return ef800_console_usb_start();
	case EF800_USB_PERSONALITY_MTP:
		return ef800_app_usb_mtp_start();
	case EF800_USB_PERSONALITY_NONE:
		return 0;
	case EF800_USB_PERSONALITY_MSC:
		return ef800_app_usb_msc_start();
	default:
		return -EINVAL;
	}
}

int ef800_usb_manager_init(enum ef800_usb_personality initial)
{
	int ret;

	ret = k_mutex_lock(&usb_manager_lock, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	active_personality = initial;
	k_mutex_unlock(&usb_manager_lock);
	return 0;
}

int ef800_usb_manager_set(enum ef800_usb_personality desired)
{
	enum ef800_usb_personality previous;
	int ret;

	ret = k_mutex_lock(&usb_manager_lock, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	previous = active_personality;
	if (previous == desired) {
		k_mutex_unlock(&usb_manager_lock);
		return 0;
	}
	if (ef800_usb_personality_transition(previous, desired) ==
		EF800_USB_TRANSITION_RESTART) {
		k_mutex_unlock(&usb_manager_lock);
		return -EAGAIN;
	}
	ret = stop_personality(previous);
	if (ret == 0) {
		ret = start_personality(desired);
	}
	if (ret != 0) {
		(void)stop_personality(desired);
		(void)start_personality(previous);
		active_personality = previous;
	} else {
		active_personality = desired;
	}
	k_mutex_unlock(&usb_manager_lock);
	return ret;
}

enum ef800_usb_personality ef800_usb_manager_get(void)
{
	return active_personality;
}
