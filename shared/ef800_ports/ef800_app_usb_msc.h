/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Start/stop EF800Application USB mass-storage exposure safely. */
#ifndef EF800_APPLICATION_USB_MSC_H
#define EF800_APPLICATION_USB_MSC_H

#include <stdbool.h>

/** @brief Start USB mass-storage exposure after obtaining exclusive storage ownership.
 * @return 0 on start; negative errno if storage or USB setup is unsafe/fails.
 */
int ef800_app_usb_msc_start(void);

/** @brief Stop MSC exposure and restore filesystem ownership when safe.
 * @return 0 on stop; negative errno if shutdown or ownership transfer fails.
 */
int ef800_app_usb_msc_stop(void);

/** @brief Query whether EF800Application MSC is active.
 * @return True when MSC is enabled.
 */
bool ef800_app_usb_msc_is_active(void);

#endif /* EF800_APPLICATION_USB_MSC_H */
