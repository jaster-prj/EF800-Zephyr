/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_CONSOLE_H
#define EF800_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_measurement.h"

/* Select and configure the application console from the internal metadata
 * record. Port zero means the board's compile-time Zephyr console. */
int ef800_console_init(bool usb_debug_available);
int ef800_console_set_target(uint8_t port_id);
int ef800_console_set_target_with_config(
	uint8_t port_id, const struct ef800_measurement_serial_config *config);
uint8_t ef800_console_get_target(void);
int ef800_console_write(const uint8_t *data, size_t length);
int ef800_console_read(uint8_t *data);
int ef800_console_usb_start(void);
void ef800_console_set_transport_ready(bool ready);

/* Queue output until a usable DebugConsole transport is selected. */
size_t ef800_console_pending(void);
uint32_t ef800_console_dropped(void);

#endif /* EF800_CONSOLE_H */
