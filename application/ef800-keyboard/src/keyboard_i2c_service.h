/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_I2C_SERVICE_H
#define EF800_KEYBOARD_I2C_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_update_protocol.h"

int ef800_keyboard_i2c_service_init(void);
int ef800_keyboard_i2c_service_unregister(void);
bool ef800_keyboard_i2c_update_take(uint8_t request[EF800_KB_UPDATE_REQUEST_MAX],
				    size_t *request_length,
				    bool *timed_out, bool *overflow);
int ef800_keyboard_i2c_update_respond(
	const uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX], size_t response_length);
bool ef800_keyboard_i2c_updater_entry_take(void);
void ef800_keyboard_i2c_updater_entry_request(void);

#endif /* EF800_KEYBOARD_I2C_SERVICE_H */
