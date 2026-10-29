/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_USB_MANAGER_H_
#define EF800_USB_MANAGER_H_

#include "ef800_usb_personality.h"

int ef800_usb_manager_init(enum ef800_usb_personality initial);
int ef800_usb_manager_set(enum ef800_usb_personality desired);
enum ef800_usb_personality ef800_usb_manager_get(void);

#endif /* EF800_USB_MANAGER_H_ */
