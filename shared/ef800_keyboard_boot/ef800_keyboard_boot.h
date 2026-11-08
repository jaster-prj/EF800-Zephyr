/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_BOOT_H_
#define EF800_KEYBOARD_BOOT_H_

#include <stdbool.h>
#include <stdint.h>

#define EF800_KEYBOARD_BOOT_STATE_ADDRESS 0x2000F000UL
#define EF800_KEYBOARD_BOOT_STATE_MAGIC 0x4B425354U
#define EF800_KEYBOARD_BOOT_STATE_VERSION 1U
#define EF800_KEYBOARD_BOOT_MAX_WATCHDOG_STARTS 3U
#define EF800_KEYBOARD_BOOT_UPDATER_WINDOW_MS 50U

enum ef800_keyboard_boot_phase {
	EF800_KEYBOARD_BOOT_NONE = 0U,
	EF800_KEYBOARD_BOOT_PENDING = 1U,
	EF800_KEYBOARD_BOOT_HEALTHY = 2U,
};

enum ef800_keyboard_boot_request {
	EF800_KEYBOARD_BOOT_REQUEST_NONE = 0U,
	EF800_KEYBOARD_BOOT_REQUEST_SYSTEM = 1U,
};

struct ef800_keyboard_boot_state {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint32_t watchdog_starts;
	uint32_t phase;
	uint32_t request;
	uint32_t reset_cause;
	uint32_t crc32;
};

struct ef800_keyboard_boot_state *ef800_keyboard_boot_state_get(void);
bool ef800_keyboard_boot_state_valid(
	const struct ef800_keyboard_boot_state *state);
void ef800_keyboard_boot_state_initialize(
	struct ef800_keyboard_boot_state *state);
bool ef800_keyboard_boot_prepare(struct ef800_keyboard_boot_state *state,
	bool watchdog_reset, uint32_t reset_cause);
void ef800_keyboard_boot_request_system(
	struct ef800_keyboard_boot_state *state);
bool ef800_keyboard_boot_consume_system_request(
	struct ef800_keyboard_boot_state *state);
bool ef800_keyboard_boot_system_request_pending(
	const struct ef800_keyboard_boot_state *state);
bool ef800_keyboard_boot_i2c_probe(bool write_started, bool write_had_data);
bool ef800_keyboard_boot_updater_window_expired(uint32_t started_ms,
						       uint32_t now_ms);
bool ef800_keyboard_boot_should_enter_updater(bool probe_received,
					     bool recovery_required,
					     bool application_valid);
void ef800_keyboard_boot_confirm(struct ef800_keyboard_boot_state *state);

#endif /* EF800_KEYBOARD_BOOT_H */
