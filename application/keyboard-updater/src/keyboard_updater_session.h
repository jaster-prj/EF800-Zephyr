/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Validate updater requests and execute keyboard flash-update commands.
 */

#ifndef EF800_KEYBOARD_UPDATER_SESSION_H
#define EF800_KEYBOARD_UPDATER_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_update_protocol.h"

/** @brief Process one captured protocol request and construct its response.
 * @param request Captured request frame.
 * @param request_length Number of captured bytes.
 * @param is_version True when the transport identified a version query.
 * @param timed_out True if request capture timed out.
 * @param overflow True if the capture buffer overflowed.
 * @param response Buffer receiving the complete response frame.
 * @param response_length Receives response length in bytes.
 * @param reboot_after_response Receives whether a reboot should follow transmission.
 * @return 0 on processing; negative errno for malformed or failed operations.
 */
int ef800_keyboard_updater_session_process(
	const uint8_t *request, size_t request_length, bool is_version,
	bool timed_out, bool overflow,
	uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX], size_t *response_length,
	bool *reboot_after_response);

#endif /* EF800_KEYBOARD_UPDATER_SESSION_H */
