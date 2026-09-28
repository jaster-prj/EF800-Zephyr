/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_WATCHDOG_H_
#define EF800_WATCHDOG_H_

#include <stdbool.h>

int ef800_watchdog_start(unsigned int timeout_ms);
/* Attach to an IWDG started by the resident bootloader without reconfiguring it. */
int ef800_watchdog_inherit(void);
int ef800_watchdog_feed(void);
bool ef800_watchdog_available(void);

#endif /* EF800_WATCHDOG_H */
