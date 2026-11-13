/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Serialize keyboard UART commands and match asynchronous responses.
 */
#ifndef EF800_KEYBOARD_COMMAND_H
#define EF800_KEYBOARD_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "keyboard_host.h"

#define EF800_KEYBOARD_COMMAND_TIMEOUT_MS 1000U

struct ef800_keyboard_command {
	const struct device *uart;
	struct ef800_keyboard_host *host;
	struct k_mutex mutex;
	struct k_sem complete;
	volatile bool pending;
	volatile uint8_t pending_command;
	bool uart_initialized;
};

/** @brief Initialize command state, synchronization objects, and UART binding.
 * @param command State to initialize.
 * @param uart Keyboard UART device.
 * @param host Keyboard protocol state receiving parsed responses.
 * @return None; NULL command is ignored.
 */
void ef800_keyboard_command_init(struct ef800_keyboard_command *command,
				 const struct device *uart,
				 struct ef800_keyboard_host *host);
/** @brief Set whether the UART adapter has been configured for transmission.
 * @param command Command state to update.
 * @param ready True when UART setup and RX are active.
 * @return None.
 */
void ef800_keyboard_command_set_uart_ready(
	struct ef800_keyboard_command *command, bool ready);
/** @brief Lock the serialized command channel until it becomes available.
 * @param command Command state to lock; NULL is ignored.
 * @return None.
 */
void ef800_keyboard_command_lock(struct ef800_keyboard_command *command);
/** @brief Release the serialized command channel.
 * @param command Command state to unlock; NULL is ignored.
 * @return None.
 */
void ef800_keyboard_command_unlock(struct ef800_keyboard_command *command);
/** @brief Check whether a keyboard-version command is awaiting its response.
 * @param command Command state to inspect.
 * @return True when the pending command requests the version.
 */
bool ef800_keyboard_command_waiting_for_version(
	const struct ef800_keyboard_command *command);
/** @brief Signal completion when a received response matches the pending request.
 * @param command Command state receiving the response.
 * @param response_command Command identifier from the parsed response.
 * @return None; unrelated responses do not signal completion.
 */
void ef800_keyboard_command_response(struct ef800_keyboard_command *command,
				     uint8_t response_command);
/** @brief Send one serialized request and wait for its protocol response.
 * @param command Initialized command state.
 * @param request Request command byte.
 * @param payload Optional request payload.
 * @param payload_length Number of payload bytes.
 * @return 0 on acknowledged response; negative errno on unavailable UART,
 *         timeout, malformed response, or keyboard rejection.
 */
int ef800_keyboard_command_send(struct ef800_keyboard_command *command,
				uint8_t request,
				const uint8_t *payload, size_t payload_length);
/** @brief Send a named virtual-key event while keyboard debug mode is active.
 * @param command Initialized command state.
 * @param debug_active Whether debug mode currently owns key input.
 * @param action Virtual-key press/release action.
 * @param name Key name to send.
 * @return 0 on send; negative errno on inactive debug mode, invalid name, or
 *         unavailable UART.
 */
int ef800_keyboard_command_send_virtual_key(
	struct ef800_keyboard_command *command, bool debug_active,
	uint8_t action, const char *name);

#endif /* EF800_KEYBOARD_COMMAND_H */
