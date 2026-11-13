/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Initialize and scan the keyboard matrix into debounced key events.
 */

#ifndef EF800_KEYBOARD_MATRIX_H
#define EF800_KEYBOARD_MATRIX_H

#include <stdint.h>
#include <stddef.h>

#include "keyboard_matrix_debounce.h"

#define EF800_KEYBOARD_SCAN_KEY_COUNT KB_MATRIX_KEY_COUNT

/** @brief Configure GPIO resources required by the keyboard matrix.
 * @return 0 on success; negative errno if a required device/setup is unavailable.
 */
int ef800_keyboard_matrix_init(void);

/** @brief Scan the matrix and append debounced changes to the caller's buffer.
 * @param state Debounce history to update.
 * @param enabled False suppresses transitions and releases tracked keys.
 * @param events Buffer receiving key transition events.
 * @param event_capacity Maximum events that fit in events.
 * @param event_count Receives the number of events written.
 * @return 0 on scan; negative errno on hardware or argument failure.
 */
int ef800_keyboard_matrix_scan(struct kb_matrix_debounce_state *state,
			       bool enabled, struct kb_matrix_key_event *events,
			       size_t event_capacity, size_t *event_count);

#endif /* EF800_KEYBOARD_MATRIX_H */
