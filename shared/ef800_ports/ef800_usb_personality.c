/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_usb_personality.h"

bool ef800_usb_personality_is_msc(enum ef800_usb_personality personality)
{
	return personality == EF800_USB_PERSONALITY_MSC;
}

enum ef800_usb_transition ef800_usb_personality_transition(
	enum ef800_usb_personality current,
	enum ef800_usb_personality desired)
{
	if (current == desired) {
		return EF800_USB_TRANSITION_NONE;
	}
	if (ef800_usb_personality_is_msc(current) ||
		ef800_usb_personality_is_msc(desired)) {
		return EF800_USB_TRANSITION_RESTART;
	}
	return EF800_USB_TRANSITION_HOT_SWITCH;
}
