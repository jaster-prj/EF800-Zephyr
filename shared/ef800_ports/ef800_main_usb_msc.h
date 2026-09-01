/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MAIN_USB_MSC_H
#define EF800_MAIN_USB_MSC_H

#include <stdbool.h>

int ef800_main_usb_msc_start(void);
int ef800_main_usb_msc_stop(void);
bool ef800_main_usb_msc_is_active(void);

#endif /* EF800_MAIN_USB_MSC_H */
