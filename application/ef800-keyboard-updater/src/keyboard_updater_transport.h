/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Queue keyboard updater requests and responses across the I2C transport.
 */

#ifndef EF800_KEYBOARD_UPDATER_TRANSPORT_H
#define EF800_KEYBOARD_UPDATER_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_update_protocol.h"

/** @brief Initialize the updater request/response transport.
 * @return 0 on success; negative errno if transport setup fails.
 */
int ef800_keyboard_updater_transport_init(void);

/** @brief Consume a pending request and its capture metadata.
 * @param request Receives request bytes up to the protocol maximum.
 * @param request_length Receives request byte count.
 * @param is_version Receives whether this is a version query.
 * @param timed_out Receives capture timeout state.
 * @param overflow Receives capture overflow state.
 * @return True if a request was consumed; otherwise false.
 */
bool ef800_keyboard_updater_request_take(
	uint8_t request[EF800_KB_UPDATE_REQUEST_MAX], size_t *request_length,
	bool *is_version, bool *timed_out, bool *overflow);

/** @brief Submit a response and indicate whether reboot follows transmission.
 * @param response Complete response frame.
 * @param response_length Number of response bytes.
 * @param reboot_after_response True to reboot after the transport sends it.
 * @return 0 on submission; negative errno if response cannot be queued.
 */
int ef800_keyboard_updater_response_submit(
	const uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX], size_t response_length,
	bool reboot_after_response);

/** @brief Consume a pending reboot request.
 * @return True if reboot was requested, otherwise false.
 */
bool ef800_keyboard_updater_reboot_take(void);

#endif /* EF800_KEYBOARD_UPDATER_TRANSPORT_H */
