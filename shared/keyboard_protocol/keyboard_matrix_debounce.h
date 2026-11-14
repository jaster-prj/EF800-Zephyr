/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Debounce sampled matrix state into key transition events. */

#ifndef EF800_KEYBOARD_MATRIX_DEBOUNCE_H
#define EF800_KEYBOARD_MATRIX_DEBOUNCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard_protocol.h"

#define KB_MATRIX_KEY_COUNT (KB_MATRIX_ROWS * KB_MATRIX_COLS + KB_SPECIAL_KEYS)

struct kb_matrix_debounce_state {
	uint64_t stable_keys;
	uint8_t scan_counts[KB_MATRIX_KEY_COUNT];
};

struct kb_matrix_key_event {
	uint8_t key_index;
	bool pressed;
};

/** @brief Update stable key state and emit transitions crossing a threshold.
 * @param state Debounce history to update.
 * @param sampled_keys Current sampled key bitmask.
 * @param threshold Consecutive scans required to accept a transition.
 * @param events Buffer receiving press/release events.
 * @param event_capacity Maximum number of events in the buffer.
 * @param event_count Receives number of events written.
 * @return 0 on update; negative errno when arguments/capacity are invalid.
 */
int kb_matrix_debounce_update(struct kb_matrix_debounce_state *state,
			      uint64_t sampled_keys, uint8_t threshold,
			      struct kb_matrix_key_event *events,
			      size_t event_capacity, size_t *event_count);

#endif /* EF800_KEYBOARD_MATRIX_DEBOUNCE_H */
