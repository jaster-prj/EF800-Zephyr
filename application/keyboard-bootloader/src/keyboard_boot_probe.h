/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Detect and release the keyboard bootloader's updater-entry request.
 */

#ifndef EF800_KEYBOARD_BOOT_PROBE_H
#define EF800_KEYBOARD_BOOT_PROBE_H

#include <stdbool.h>

/** @brief Sample the hardware boot-request GPIO.
 * @param requested Receives whether updater entry is requested.
 * @return 0 on sample; negative errno if the GPIO cannot be read.
 */
int ef800_keyboard_boot_probe_gpio(bool *requested);

/** @brief Listen for the updater-entry request during its startup window.
 * @param received Receives whether a valid request was observed.
 * @return 0 when the window completes; negative errno on setup/receive failure.
 */
int ef800_keyboard_boot_probe_window(bool *received);

/** @brief Unregister the probe's temporary UART/GPIO resources.
 * @return 0 on release; negative errno if cleanup fails.
 */
int ef800_keyboard_boot_probe_unregister(void);

#endif /* EF800_KEYBOARD_BOOT_PROBE_H */
