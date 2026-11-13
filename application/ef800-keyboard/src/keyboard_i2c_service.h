/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Capture updater I2C requests and return responses from keyboard firmware.
 */

#ifndef EF800_KEYBOARD_I2C_SERVICE_H
#define EF800_KEYBOARD_I2C_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_update_protocol.h"

/** @brief Register the updater's I2C target callbacks and request signaling.
 * @return 0 on initialization; negative errno if registration fails.
 */
int ef800_keyboard_i2c_service_init(void);

/** @brief Unregister the updater's I2C target service.
 * @return 0 on cleanup; negative errno if unregistering fails.
 */
int ef800_keyboard_i2c_service_unregister(void);

/** @brief Consume a pending captured request, including timeout/overflow state.
 * @param request Receives request bytes up to the protocol maximum.
 * @param request_length Receives the captured byte count.
 * @param timed_out Receives whether capture timed out.
 * @param overflow Receives whether the request exceeded its buffer.
 * @return True when a request event was consumed; otherwise false.
 */
bool ef800_keyboard_i2c_update_take(uint8_t request[EF800_KB_UPDATE_REQUEST_MAX],
				    size_t *request_length,
				    bool *timed_out, bool *overflow);

/** @brief Queue a framed updater response for the I2C target callback.
 * @param response Response bytes, up to the protocol maximum.
 * @param response_length Number of response bytes.
 * @return 0 when queued; negative errno for invalid length or inactive service.
 */
int ef800_keyboard_i2c_update_respond(
	const uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX], size_t response_length);

/** @brief Consume a pending request to enter the updater.
 * @return True if an entry request was pending, otherwise false.
 */
bool ef800_keyboard_i2c_updater_entry_take(void);

/** @brief Notify the updater task that an entry request has arrived.
 * @return None.
 */
void ef800_keyboard_i2c_updater_entry_request(void);

#endif /* EF800_KEYBOARD_I2C_SERVICE_H */
