/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_UPDATER_TRANSPORT_H
#define EF800_KEYBOARD_UPDATER_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_update_protocol.h"

int ef800_keyboard_updater_transport_init(void);
bool ef800_keyboard_updater_request_take(
	uint8_t request[EF800_KB_UPDATE_REQUEST_MAX], size_t *request_length,
	bool *is_version, bool *timed_out, bool *overflow);
int ef800_keyboard_updater_response_submit(
	const uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX], size_t response_length,
	bool reboot_after_response);
bool ef800_keyboard_updater_reboot_take(void);

#endif /* EF800_KEYBOARD_UPDATER_TRANSPORT_H */
