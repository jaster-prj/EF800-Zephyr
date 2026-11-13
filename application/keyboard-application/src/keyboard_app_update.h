/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Process keyboard updater requests and produce protocol responses.
 */

#ifndef EF800_KEYBOARD_APP_UPDATE_H
#define EF800_KEYBOARD_APP_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_update_protocol.h"

/** @brief Dispatch one captured updater request to the flash/update protocol.
 * @param request Captured request frame bytes.
 * @param request_length Number of captured bytes.
 * @param timed_out True when capture ended by request timeout.
 * @param overflow True when the capture buffer overflowed.
 * @return None; response is sent through the configured updater transport.
 */
void ef800_keyboard_app_update_process(
	const uint8_t *request, size_t request_length,
	bool timed_out, bool overflow);

#endif /* EF800_KEYBOARD_APP_UPDATE_H */
