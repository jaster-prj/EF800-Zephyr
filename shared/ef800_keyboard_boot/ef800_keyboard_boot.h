/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Track keyboard boot health, watchdog recovery, and updater-entry requests.
 */
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

/** @brief Get the retained boot-state record at its fixed memory location.
 * @return Pointer to retained state.
 */
struct ef800_keyboard_boot_state *ef800_keyboard_boot_state_get(void);

/** @brief Validate retained boot-state fields and checksum.
 * @param state State record to check.
 * @return True when the record is valid.
 */
bool ef800_keyboard_boot_state_valid(
	const struct ef800_keyboard_boot_state *state);

/** @brief Initialize a boot-state record to safe defaults.
 * @param state Record to initialize.
 * @return None.
 */
void ef800_keyboard_boot_state_initialize(
	struct ef800_keyboard_boot_state *state);

/** @brief Update watchdog recovery state and report whether recovery is required.
 * @param state Retained state to validate/update.
 * @param watchdog_reset True when the previous reset was watchdog-triggered.
 * @param reset_cause Hardware reset-cause flags to retain.
 * @return True when the caller should enter recovery/updater mode.
 */
bool ef800_keyboard_boot_prepare(struct ef800_keyboard_boot_state *state,
	bool watchdog_reset, uint32_t reset_cause);

/** @brief Record a request for the keyboard application to enter its updater.
 * @param state Retained state to update.
 * @return None.
 */
void ef800_keyboard_boot_request_system(
	struct ef800_keyboard_boot_state *state);

/** @brief Consume and clear a pending updater-entry request.
 * @param state Retained state to update.
 * @return True if a request was pending.
 */
bool ef800_keyboard_boot_consume_system_request(
	struct ef800_keyboard_boot_state *state);

/** @brief Check whether an updater-entry request is pending.
 * @param state Retained state to inspect.
 * @return True when a system request is pending.
 */
bool ef800_keyboard_boot_system_request_pending(
	const struct ef800_keyboard_boot_state *state);

/** @brief Recognize an I2C probe transaction used to request updater entry.
 * @param write_started True when a write transaction began.
 * @param write_had_data True when request data followed the address.
 * @return True for the recognized probe form.
 */
bool ef800_keyboard_boot_i2c_probe(bool write_started, bool write_had_data);

/** @brief Check whether the bounded updater-entry listen window has elapsed.
 * @param started_ms Window start time.
 * @param now_ms Current time, both in wrapping millisecond ticks.
 * @return True when the configured window expired.
 */
bool ef800_keyboard_boot_updater_window_expired(uint32_t started_ms,
						       uint32_t now_ms);

/** @brief Decide whether recovery or a probe requires entering the updater.
 * @param probe_received True when a host entry probe was detected.
 * @param recovery_required True when watchdog policy requested recovery.
 * @param application_valid True when the application image is bootable.
 * @return True when updater entry is required.
 */
bool ef800_keyboard_boot_should_enter_updater(bool probe_received,
					     bool recovery_required,
					     bool application_valid);

/** @brief Mark the running application healthy and reset failure tracking.
 * @param state Retained boot-state record to update.
 * @return None.
 */
void ef800_keyboard_boot_confirm(struct ef800_keyboard_boot_state *state);

#endif /* EF800_KEYBOARD_BOOT_H */
