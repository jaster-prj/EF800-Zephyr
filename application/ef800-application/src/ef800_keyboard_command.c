/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_keyboard_command.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "keyboard_protocol.h"

LOG_MODULE_REGISTER(ef800_keyboard_command, LOG_LEVEL_INF);

void ef800_keyboard_command_init(struct ef800_keyboard_command *command,
				 const struct device *uart,
				 struct ef800_keyboard_host *host)
{
	if (command == NULL) {
		return;
	}
	memset(command, 0, sizeof(*command));
	command->uart = uart;
	command->host = host;
	k_mutex_init(&command->mutex);
	k_sem_init(&command->complete, 0, 1);
}

void ef800_keyboard_command_set_uart_ready(
	struct ef800_keyboard_command *command, bool ready)
{
	if (command != NULL) {
		command->uart_initialized = ready;
	}
}

void ef800_keyboard_command_lock(struct ef800_keyboard_command *command)
{
	if (command != NULL) {
		k_mutex_lock(&command->mutex, K_FOREVER);
	}
}

void ef800_keyboard_command_unlock(struct ef800_keyboard_command *command)
{
	if (command != NULL) {
		k_mutex_unlock(&command->mutex);
	}
}

bool ef800_keyboard_command_waiting_for_version(
	const struct ef800_keyboard_command *command)
{
	return command != NULL && command->pending &&
		command->pending_command == CMD_KEYBOARD_VERSION;
}

void ef800_keyboard_command_response(struct ef800_keyboard_command *command,
				     uint8_t response_command)
{
	if (command != NULL && command->pending &&
	    command->pending_command == response_command) {
		k_sem_give(&command->complete);
	}
}

/** @brief Poll each byte of a command packet to the bound UART.
 * @param command Initialized command state with a UART device.
 * @param data Packet bytes to transmit.
 * @param length Number of bytes to transmit.
 * @return None.
 */
static void keyboard_uart_send(struct ef800_keyboard_command *command,
			       const uint8_t *data, size_t length)
{
	for (size_t index = 0U; index < length; ++index) {
		uart_poll_out(command->uart, data[index]);
	}
}

/** @brief Transmit a request and await its response while the mutex is held.
 * @param command Initialized command state.
 * @param request Request command byte.
 * @param payload Optional payload bytes.
 * @param payload_length Number of payload bytes.
 * @return 0 on acknowledgment; negative errno on unavailable device, timeout,
 *         or rejected/malformed response.
 */
static int send_locked(struct ef800_keyboard_command *command,
		       uint8_t request, const uint8_t *payload,
		       size_t payload_length)
{
	uint8_t status;
	int result;

	if (command == NULL || command->host == NULL || command->uart == NULL ||
	    !command->uart_initialized ||
	    !ef800_keyboard_host_is_ready(command->host)) {
		return -ENODEV;
	}
	k_sem_reset(&command->complete);
	command->pending_command = request;
	command->pending = true;
	if (request == CMD_KEYBOARD_VERSION) {
		LOG_INF("Keyboard version query: TX command 0x%02x", request);
	}
	keyboard_uart_send(command, &request, 1U);
	if (payload != NULL && payload_length != 0U) {
		keyboard_uart_send(command, payload, payload_length);
	}
	result = k_sem_take(&command->complete,
		K_MSEC(EF800_KEYBOARD_COMMAND_TIMEOUT_MS));
	command->pending = false;
	if (result != 0) {
		if (request == CMD_KEYBOARD_VERSION) {
			LOG_WRN("Keyboard version query timed out; RX response incomplete or absent");
		}
		return -ETIMEDOUT;
	}
	if (request == CMD_KEYBOARD_VERSION) {
		uint8_t version[3];

		return ef800_keyboard_host_get_version(command->host, version) ?
			0 : -EBADMSG;
	}
	status = command->host->last_command_status;
	return status == CMD_ACK ? 0 : -EIO;
}

int ef800_keyboard_command_send(struct ef800_keyboard_command *command,
				uint8_t request,
				const uint8_t *payload, size_t payload_length)
{
	int result;

	if (command == NULL) {
		return -EINVAL;
	}
	ef800_keyboard_command_lock(command);
	result = send_locked(command, request, payload, payload_length);
	ef800_keyboard_command_unlock(command);
	return result;
}

int ef800_keyboard_command_send_virtual_key(
	struct ef800_keyboard_command *command, bool debug_active,
	uint8_t action, const char *name)
{
	uint8_t packet[3U + KB_DEBUG_KEY_NAME_MAX];
	size_t length;
	int result;

	if (!debug_active || name == NULL) {
		return -EACCES;
	}
	length = strlen(name);
	if (length == 0U || length > KB_DEBUG_KEY_NAME_MAX) {
		return -EINVAL;
	}
	if (command == NULL) {
		return -EINVAL;
	}
	packet[0] = CMD_KEYBOARD_DEBUG;
	packet[1] = action;
	packet[2] = (uint8_t)length;
	memcpy(&packet[3], name, length);
	ef800_keyboard_command_lock(command);
	if (!command->uart_initialized || command->uart == NULL ||
	    command->host == NULL ||
	    !ef800_keyboard_host_is_ready(command->host)) {
		result = -ENODEV;
	} else {
		keyboard_uart_send(command, packet, 3U + length);
		result = 0;
	}
	ef800_keyboard_command_unlock(command);
	return result;
}
