/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_MATRIX_H
#define EF800_KEYBOARD_MATRIX_H

#include <stdint.h>
#include <stddef.h>

#include "keyboard_matrix_debounce.h"

#define EF800_KEYBOARD_SCAN_KEY_COUNT KB_MATRIX_KEY_COUNT

int ef800_keyboard_matrix_init(void);
int ef800_keyboard_matrix_scan(struct kb_matrix_debounce_state *state,
			       bool enabled, struct kb_matrix_key_event *events,
			       size_t event_capacity, size_t *event_count);

#endif /* EF800_KEYBOARD_MATRIX_H */
