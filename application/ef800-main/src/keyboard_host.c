/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_host.h"

#include <string.h>

enum parser_state {
	PARSER_IDLE = 0,
	PARSER_PREFIX,
	PARSER_BREAK_CODE,
	PARSER_COMMAND,
	PARSER_COMMAND_PAYLOAD,
};

static void parser_reset(struct ef800_keyboard_host *host)
{
	host->parser_state = PARSER_IDLE;
	host->parser_prefix = KB_SCANCODE_PREFIX_NONE;
	host->parser_command = 0U;
	host->parser_payload_length = 0U;
	host->parser_payload_used = 0U;
	host->parser_timestamp_valid = false;
}

static void queue_key(struct ef800_keyboard_host *host, uint8_t key)
{
	if (host->key_count >= EF800_HOST_KEY_QUEUE_SIZE) {
		host->overflow = true;
		return;
	}
	host->keys[host->key_write] = key;
	host->key_write = (uint8_t)((host->key_write + 1U) %
					EF800_HOST_KEY_QUEUE_SIZE);
	++host->key_count;
}

static void queue_event(struct ef800_keyboard_host *host, uint32_t event)
{
	if (host->event_count >= EF800_HOST_EVENT_QUEUE_SIZE) {
		host->overflow = true;
		return;
	}
	host->events[host->event_write] = event;
	host->event_write = (uint8_t)((host->event_write + 1U) %
					EF800_HOST_EVENT_QUEUE_SIZE);
	++host->event_count;
	host->last_event = event;
}

static enum ef800_host_feed_result consume_scancode(
	struct ef800_keyboard_host *host, uint8_t prefix, uint8_t code,
	bool release)
{
	struct kb_translation output;
	int result;

	result = kb_translate_scancode(&host->translator, prefix, code, release,
				       &output);
	if (result < 0) {
		return EF800_HOST_FEED_ERROR;
	}
	if (result == 0) {
		return EF800_HOST_FEED_NONE;
	}
	if (output.type == KB_TRANSLATION_CHAR) {
		queue_key(host, (uint8_t)output.value);
		return EF800_HOST_FEED_KEY;
	}
	if (output.type == KB_TRANSLATION_EVENT) {
		queue_event(host, output.value);
		return EF800_HOST_FEED_EVENT;
	}
	return EF800_HOST_FEED_NONE;
}

static uint8_t command_payload_length(uint8_t command)
{
	switch (command) {
	case CMD_KEYBOARD_TYPEMATIC:
	case CMD_KEYBOARD_SET_DEBUG:
		return 1U; /* ACK/RESEND, followed by data only on ACK. */
	case CMD_KEYBOARD_ID:
		return 2U; /* The legacy controller returns two ID bytes. */
	case CMD_KEYBOARD_VERSION:
		return 3U;
	default:
		return 1U;
	}
}

static enum ef800_host_feed_result consume_command(
	struct ef800_keyboard_host *host)
{
	uint8_t command = host->parser_command;
	uint8_t status = host->parser_payload[0];

	host->last_command = command;
	host->last_command_status = status;
	if (command == CMD_KEYBOARD_TYPEMATIC && status == CMD_ACK &&
	    host->parser_payload_length >= 2U) {
		host->typematic = host->parser_payload[1];
	}
	if (command == CMD_KEYBOARD_SET_DEBUG && status == CMD_ACK &&
	    host->parser_payload_length >= 2U) {
		host->debug = host->parser_payload[1] != 0U;
	}
	if (command == CMD_KEYBOARD_ID && host->parser_payload_length >= 2U) {
		host->id[0] = host->parser_payload[0];
		host->id[1] = host->parser_payload[1];
		host->id_valid = true;
	}
	if (command == CMD_KEYBOARD_VERSION && host->parser_payload_length >= 3U) {
		host->version[0] = host->parser_payload[0];
		host->version[1] = host->parser_payload[1];
		host->version[2] = host->parser_payload[2];
		host->version_valid = true;
	}
	if (command == CMD_KEYBOARD_ACTIVATE && status == CMD_ACK) {
		host->active = true;
	}
	if (command == CMD_KEYBOARD_DEACTIVATE && status == CMD_ACK) {
		host->active = false;
	}
	if (command == CMD_KEYBOARD_RESET && status == CMD_ACK) {
		host->ready = false;
	}
	return EF800_HOST_FEED_COMMAND;
}

static enum ef800_host_feed_result feed_byte(
	struct ef800_keyboard_host *host, uint8_t byte)
{
	enum ef800_host_feed_result result;

	switch ((enum parser_state)host->parser_state) {
	case PARSER_IDLE:
		switch (byte) {
		case CMD_INITIALIZED:
			host->ready = true;
			return EF800_HOST_FEED_READY;
		case CMD_KEYBOARD_ECHO:
			return EF800_HOST_FEED_ECHO;
		case CMD_RETURN:
			host->parser_state = PARSER_COMMAND;
			return EF800_HOST_FEED_NONE;
		case KB_SCANCODE_PREFIX_E0:
		case KB_SCANCODE_PREFIX_E1:
			host->parser_prefix = byte;
			host->parser_state = PARSER_PREFIX;
			return EF800_HOST_FEED_NONE;
		case KB_SCANCODE_BREAK:
			host->parser_prefix = KB_SCANCODE_PREFIX_NONE;
			host->parser_state = PARSER_BREAK_CODE;
			return EF800_HOST_FEED_NONE;
		default:
			return consume_scancode(host, KB_SCANCODE_PREFIX_NONE, byte,
						false);
		}

	case PARSER_PREFIX:
		if (byte == KB_SCANCODE_BREAK) {
			host->parser_state = PARSER_BREAK_CODE;
			return EF800_HOST_FEED_NONE;
		}
		result = consume_scancode(host, host->parser_prefix, byte, false);
		parser_reset(host);
		return result;

	case PARSER_BREAK_CODE:
		result = consume_scancode(host, host->parser_prefix, byte, true);
		parser_reset(host);
		return result;

	case PARSER_COMMAND:
		host->parser_command = byte;
		host->parser_payload_length = command_payload_length(byte);
		host->parser_payload_used = 0U;
		host->parser_state = PARSER_COMMAND_PAYLOAD;
		return EF800_HOST_FEED_NONE;

	case PARSER_COMMAND_PAYLOAD:
		if (host->parser_payload_used >= sizeof(host->parser_payload)) {
			parser_reset(host);
			return EF800_HOST_FEED_ERROR;
		}
		host->parser_payload[host->parser_payload_used++] = byte;
		if ((host->parser_command == CMD_KEYBOARD_TYPEMATIC ||
		     host->parser_command == CMD_KEYBOARD_SET_DEBUG) &&
		    host->parser_payload_used == 1U && byte == CMD_ACK) {
			host->parser_payload_length = 2U;
			return EF800_HOST_FEED_NONE;
		}
		if (host->parser_payload_used < host->parser_payload_length) {
			return EF800_HOST_FEED_NONE;
		}
		result = consume_command(host);
		parser_reset(host);
		return result;
	}

	parser_reset(host);
	return EF800_HOST_FEED_ERROR;
}

void ef800_keyboard_host_init(struct ef800_keyboard_host *host)
{
	if (host == NULL) {
		return;
	}
	memset(host, 0, sizeof(*host));
	kb_translator_init(&host->translator);
	host->active = true;
	host->typematic = 0x2BU;
	parser_reset(host);
}

enum ef800_host_feed_result ef800_keyboard_host_feed_at(
	struct ef800_keyboard_host *host, uint8_t byte, uint32_t now_ms)
{
	enum ef800_host_feed_result result;

	if (host == NULL) {
		return EF800_HOST_FEED_ERROR;
	}

	if (host->parser_timestamp_valid && host->parser_state != PARSER_IDLE &&
	    now_ms - host->parser_timestamp_ms > EF800_HOST_FRAME_TIMEOUT_MS) {
		parser_reset(host);
	}

	result = feed_byte(host, byte);
	if (host->parser_state != PARSER_IDLE) {
		host->parser_timestamp_ms = now_ms;
		host->parser_timestamp_valid = true;
	}
	return result;
}

enum ef800_host_feed_result ef800_keyboard_host_feed(
	struct ef800_keyboard_host *host, uint8_t byte)
{
	enum ef800_host_feed_result result;

	if (host == NULL) {
		return EF800_HOST_FEED_ERROR;
	}
	result = feed_byte(host, byte);
	if (host->parser_state == PARSER_IDLE) {
		host->parser_timestamp_valid = false;
	}
	return result;
}

int ef800_keyboard_host_get_key(struct ef800_keyboard_host *host,
				uint8_t *key)
{
	if (host == NULL || key == NULL || host->key_count == 0U) {
		return -1;
	}
	*key = host->keys[host->key_read];
	host->key_read = (uint8_t)((host->key_read + 1U) %
					EF800_HOST_KEY_QUEUE_SIZE);
	--host->key_count;
	return 0;
}

int ef800_keyboard_host_get_event(struct ef800_keyboard_host *host,
				  uint32_t *event)
{
	if (host == NULL || event == NULL || host->event_count == 0U) {
		return -1;
	}
	*event = host->events[host->event_read];
	host->event_read = (uint8_t)((host->event_read + 1U) %
					EF800_HOST_EVENT_QUEUE_SIZE);
	--host->event_count;
	return 0;
}

uint8_t ef800_keyboard_host_key_count(
	const struct ef800_keyboard_host *host)
{
	return host == NULL ? 0U : host->key_count;
}

uint8_t ef800_keyboard_host_event_count(
	const struct ef800_keyboard_host *host)
{
	return host == NULL ? 0U : host->event_count;
}

bool ef800_keyboard_host_take_overflow(struct ef800_keyboard_host *host)
{
	bool overflow;

	if (host == NULL) {
		return false;
	}
	overflow = host->overflow;
	host->overflow = false;
	return overflow;
}

bool ef800_keyboard_host_is_ready(const struct ef800_keyboard_host *host)
{
	return host != NULL && host->ready;
}

bool ef800_keyboard_host_is_active(const struct ef800_keyboard_host *host)
{
	return host != NULL && host->active;
}

bool ef800_keyboard_host_get_id(const struct ef800_keyboard_host *host,
				uint8_t id[2])
{
	if (host == NULL || id == NULL || !host->id_valid) {
		return false;
	}
	id[0] = host->id[0];
	id[1] = host->id[1];
	return true;
}

bool ef800_keyboard_host_get_version(const struct ef800_keyboard_host *host,
				uint8_t version[3])
{
	if (host == NULL || version == NULL || !host->version_valid) {
		return false;
	}
	memcpy(version, host->version, 3U);
	return true;
}
