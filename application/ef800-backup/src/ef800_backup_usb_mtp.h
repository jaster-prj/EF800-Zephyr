/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Start, stop, and query EF800 Backup USB MTP service state.
 */
#ifndef EF800_BACKUP_USB_MTP_H_
#define EF800_BACKUP_USB_MTP_H_

#include <stdbool.h>

/** @brief Start the USB MTP service.
 * @return 0 on success; negative errno if initialization or enablement fails.
 */
int ef800_backup_usb_mtp_start(void);

/** @brief Stop the USB MTP service.
 * @return 0 on success; negative errno if the service cannot be disabled.
 */
int ef800_backup_usb_mtp_stop(void);

/** @brief Check whether the USB MTP service is active.
 * @return True when MTP is enabled, otherwise false.
 */
bool ef800_backup_usb_mtp_is_active(void);

#endif /* EF800_BACKUP_USB_MTP_H_ */
