/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Parse keyboard UART frames and queue translated keys/events. */
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
	uint8_t parser_payload[3];
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
	uint8_t version[3];
	bool version_valid;
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

/** @brief Reset parser, modifier, status, and output queue state.
 * @param host Host protocol state to initialize.
 * @return None.
 */
void ef800_keyboard_host_init(struct ef800_keyboard_host *host);

/* Feed one UART byte. The return value reports newly queued output/state. */

/** @brief Feed a byte without timestamp-based partial-frame expiration.
 * @param host Keyboard host state.
 * @param byte Received protocol byte.
 * @return Feed result describing newly parsed output/state or an error.
 */
enum ef800_host_feed_result ef800_keyboard_host_feed(
	struct ef800_keyboard_host *host, uint8_t byte);

/* Timestamped variant which drops stale partial frames before resyncing. */

/** @brief Feed a byte and discard stale partial frames before parsing it.
 * @param host Keyboard host state.
 * @param byte Received protocol byte.
 * @param now_ms Current monotonic time in milliseconds.
 * @return Feed result describing newly parsed output/state or an error.
 */
enum ef800_host_feed_result ef800_keyboard_host_feed_at(
	struct ef800_keyboard_host *host, uint8_t byte, uint32_t now_ms);

/** @brief Pop the next translated character from the key queue.
 * @param host Keyboard host state.
 * @param key Receives the next character.
 * @return 0 when a key is returned; negative errno when queue is empty.
 */
int ef800_keyboard_host_get_key(struct ef800_keyboard_host *host,
				uint8_t *key);

/** @brief Pop the next translated special event from the event queue.
 * @param host Keyboard host state.
 * @param event Receives the next event bit/value.
 * @return 0 when an event is returned; negative errno when queue is empty.
 */
int ef800_keyboard_host_get_event(struct ef800_keyboard_host *host,
				  uint32_t *event);

/** @brief Get the number of queued character keys.
 * @param host Keyboard host state.
 * @return Queue count, or zero for NULL.
 */
uint8_t ef800_keyboard_host_key_count(
	const struct ef800_keyboard_host *host);

/** @brief Get the number of queued special events.
 * @param host Keyboard host state.
 * @return Queue count, or zero for NULL.
 */
uint8_t ef800_keyboard_host_event_count(
	const struct ef800_keyboard_host *host);

/** @brief Read and clear the queue-overflow indicator.
 * @param host Keyboard host state.
 * @return True if overflow occurred since the previous call.
 */
bool ef800_keyboard_host_take_overflow(
	struct ef800_keyboard_host *host);

/* These values are updated by command responses. */

/** @brief Check whether the keyboard reported its initialized frame.
 * @param host Keyboard host state.
 * @return True when ready.
 */
bool ef800_keyboard_host_is_ready(const struct ef800_keyboard_host *host);

/** @brief Check whether physical keyboard input is active.
 * @param host Keyboard host state.
 * @return True when active.
 */
bool ef800_keyboard_host_is_active(const struct ef800_keyboard_host *host);

/** @brief Copy the last valid keyboard identifier.
 * @param host Keyboard host state.
 * @param id Destination for two ID bytes.
 * @return True when a valid ID is available.
 */
bool ef800_keyboard_host_get_id(const struct ef800_keyboard_host *host,
				uint8_t id[2]);

/** @brief Copy the last valid keyboard firmware version.
 * @param host Keyboard host state.
 * @param version Destination for major/minor/patch bytes.
 * @return True when a valid version is available.
 */
bool ef800_keyboard_host_get_version(const struct ef800_keyboard_host *host,
				uint8_t version[3]);

#endif /* EF800_KEYBOARD_HOST_H */
