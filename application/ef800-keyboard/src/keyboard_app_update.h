/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_APP_UPDATE_H
#define EF800_KEYBOARD_APP_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_update_protocol.h"

void ef800_keyboard_app_update_process(
	const uint8_t *request, size_t request_length,
	bool timed_out, bool overflow);

#endif /* EF800_KEYBOARD_APP_UPDATE_H */
