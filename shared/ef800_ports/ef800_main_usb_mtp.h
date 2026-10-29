/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MAIN_USB_MTP_H_
#define EF800_MAIN_USB_MTP_H_

#include <stdbool.h>

int ef800_main_usb_mtp_start(void);
int ef800_main_usb_mtp_stop(void);
bool ef800_main_usb_mtp_is_active(void);

#endif /* EF800_MAIN_USB_MTP_H_ */
