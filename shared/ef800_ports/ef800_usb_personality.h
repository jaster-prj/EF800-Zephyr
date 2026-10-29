/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_USB_PERSONALITY_H_
#define EF800_USB_PERSONALITY_H_

#include <stdbool.h>

enum ef800_usb_personality {
	EF800_USB_PERSONALITY_NONE,
	EF800_USB_PERSONALITY_DEBUG_CDC,
	EF800_USB_PERSONALITY_MTP,
	EF800_USB_PERSONALITY_MSC,
};

enum ef800_usb_transition {
	EF800_USB_TRANSITION_NONE,
	EF800_USB_TRANSITION_HOT_SWITCH,
	EF800_USB_TRANSITION_RESTART,
};

enum ef800_usb_transition ef800_usb_personality_transition(
	enum ef800_usb_personality current,
	enum ef800_usb_personality desired);

bool ef800_usb_personality_is_msc(enum ef800_usb_personality personality);

#endif /* EF800_USB_PERSONALITY_H_ */
