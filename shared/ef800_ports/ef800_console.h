/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_CONSOLE_H
#define EF800_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Select and configure the application console from the internal metadata
 * record. Port zero means the board's compile-time Zephyr console. */
int ef800_console_init(bool usb_debug_available);
int ef800_console_set_target(uint8_t port_id);
uint8_t ef800_console_get_target(void);
int ef800_console_write(const uint8_t *data, size_t length);

#endif /* EF800_CONSOLE_H */
