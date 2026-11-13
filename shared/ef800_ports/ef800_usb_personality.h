/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Select the active EF800 USB device personality from composition. */
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

/** @brief Classify the transition required between USB personalities.
 * @param current Active personality.
 * @param desired Requested personality.
 * @return None, hot-switch, or restart transition.
 */
enum ef800_usb_transition ef800_usb_personality_transition(
	enum ef800_usb_personality current,
	enum ef800_usb_personality desired);

/** @brief Check whether a personality exposes mass-storage ownership.
 * @param personality USB personality to inspect.
 * @return True for MSC personality.
 */
bool ef800_usb_personality_is_msc(enum ef800_usb_personality personality);

#endif /* EF800_USB_PERSONALITY_H_ */
