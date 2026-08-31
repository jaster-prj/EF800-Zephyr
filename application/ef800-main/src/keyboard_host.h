/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_HOST_H
#define EF800_KEYBOARD_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard_protocol.h"
#include "keyboard_translation.h"

#define EF800_HOST_KEY_QUEUE_SIZE 32U
#define EF800_HOST_EVENT_QUEUE_SIZE 16U
#define EF800_HOST_FRAME_TIMEOUT_MS 250U

enum ef800_host_feed_result {
	EF800_HOST_FEED_NONE = 0,
	EF800_HOST_FEED_KEY = 1,
	EF800_HOST_FEED_EVENT = 2,
	EF800_HOST_FEED_COMMAND = 3,
	EF800_HOST_FEED_READY = 4,
	EF800_HOST_FEED_ECHO = 5,
	EF800_HOST_FEED_ERROR = -1,
};

struct ef800_keyboard_host {
	struct kb_translator translator;
	uint8_t parser_state;
	uint8_t parser_prefix;
	uint8_t parser_command;
	uint8_t parser_payload[2];
	uint8_t parser_payload_length;
	uint8_t parser_payload_used;
	uint32_t parser_timestamp_ms;
	bool parser_timestamp_valid;

	bool ready;
	bool active;
	bool debug;
	uint8_t typematic;
	uint8_t id[2];
	bool id_valid;
	uint8_t last_command;
	uint8_t last_command_status;
	uint32_t last_event;
	bool overflow;

	uint8_t keys[EF800_HOST_KEY_QUEUE_SIZE];
	uint8_t key_read;
	uint8_t key_write;
	uint8_t key_count;
	uint32_t events[EF800_HOST_EVENT_QUEUE_SIZE];
	uint8_t event_read;
	uint8_t event_write;
	uint8_t event_count;
};

void ef800_keyboard_host_init(struct ef800_keyboard_host *host);

/* Feed one UART byte. The return value reports newly queued output/state. */
enum ef800_host_feed_result ef800_keyboard_host_feed(
	struct ef800_keyboard_host *host, uint8_t byte);

/* Timestamped variant which drops stale partial frames before resyncing. */
enum ef800_host_feed_result ef800_keyboard_host_feed_at(
	struct ef800_keyboard_host *host, uint8_t byte, uint32_t now_ms);

int ef800_keyboard_host_get_key(struct ef800_keyboard_host *host,
				uint8_t *key);
int ef800_keyboard_host_get_event(struct ef800_keyboard_host *host,
				  uint32_t *event);

uint8_t ef800_keyboard_host_key_count(
	const struct ef800_keyboard_host *host);
uint8_t ef800_keyboard_host_event_count(
	const struct ef800_keyboard_host *host);
bool ef800_keyboard_host_take_overflow(
	struct ef800_keyboard_host *host);

/* These values are updated by command responses. */
bool ef800_keyboard_host_is_ready(const struct ef800_keyboard_host *host);
bool ef800_keyboard_host_is_active(const struct ef800_keyboard_host *host);
bool ef800_keyboard_host_get_id(const struct ef800_keyboard_host *host,
				uint8_t id[2]);

#endif /* EF800_KEYBOARD_HOST_H */
