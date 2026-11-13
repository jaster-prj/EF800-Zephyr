/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Coordinate USB personality selection and device lifecycle. */
#ifndef EF800_USB_MANAGER_H_
#define EF800_USB_MANAGER_H_

#include "ef800_usb_personality.h"

/** @brief Initialize USB device support with the initial personality.
 * @param initial Personality to activate.
 * @return 0 on init; negative errno if device setup fails.
 */
int ef800_usb_manager_init(enum ef800_usb_personality initial);

/** @brief Transition the active USB device to a requested personality.
 * @param desired Requested personality.
 * @return 0 on transition; negative errno if stop/start fails.
 */
int ef800_usb_manager_set(enum ef800_usb_personality desired);

/** @brief Get the currently active USB personality.
 * @return Active personality, or NONE when inactive.
 */
enum ef800_usb_personality ef800_usb_manager_get(void);

#endif /* EF800_USB_MANAGER_H_ */
