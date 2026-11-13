/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_UPDATER_SESSION_H
#define EF800_KEYBOARD_UPDATER_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_update_protocol.h"

int ef800_keyboard_updater_session_process(
	const uint8_t *request, size_t request_length, bool is_version,
	bool timed_out, bool overflow,
	uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX], size_t *response_length,
	bool *reboot_after_response);

#endif /* EF800_KEYBOARD_UPDATER_SESSION_H */
