/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Start, inherit, feed, and query the platform watchdog. */
#ifndef EF800_WATCHDOG_H_
#define EF800_WATCHDOG_H_

#include <stdbool.h>

/** @brief Configure and start the watchdog with the requested timeout.
 * @param timeout_ms Timeout in milliseconds.
 * @return 0 on start; negative errno if unsupported or setup fails.
 */
int ef800_watchdog_start(unsigned int timeout_ms);
/* Attach to an IWDG started by the resident bootloader without reconfiguring it. */

/** @brief Attach to a watchdog already started by the resident bootloader.
 * @return 0 when inherited; negative errno if no inherited watchdog is usable.
 */
int ef800_watchdog_inherit(void);

/** @brief Feed the running watchdog.
 * @return 0 on feed; negative errno if unavailable or feeding fails.
 */
int ef800_watchdog_feed(void);

/** @brief Query whether a watchdog is configured and available.
 * @return True when start/inherit completed successfully.
 */
bool ef800_watchdog_available(void);

#endif /* EF800_WATCHDOG_H */
