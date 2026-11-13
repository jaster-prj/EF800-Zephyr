/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Expose EF800Application workspace files through USB MTP. */
#ifndef EF800_APPLICATION_USB_MTP_H_
#define EF800_APPLICATION_USB_MTP_H_

#include <stdbool.h>

/** @brief Start USB MTP after preparing the exported storage roots.
 * @return 0 on start; negative errno if storage or USB setup fails.
 */
int ef800_app_usb_mtp_start(void);

/** @brief Stop USB MTP and release its storage exposure.
 * @return 0 on stop; negative errno if shutdown fails.
 */
int ef800_app_usb_mtp_stop(void);

/** @brief Query whether EF800Application MTP is active.
 * @return True when MTP is enabled.
 */
bool ef800_app_usb_mtp_is_active(void);

#endif /* EF800_APPLICATION_USB_MTP_H_ */
