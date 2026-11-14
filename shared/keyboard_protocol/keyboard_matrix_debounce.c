/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_matrix_debounce.h"

#include <errno.h>

int kb_matrix_debounce_update(struct kb_matrix_debounce_state *state,
			      uint64_t sampled_keys, uint8_t threshold,
			      struct kb_matrix_key_event *events,
			      size_t event_capacity, size_t *event_count)
{
	size_t count = 0U;

	if (state == NULL || threshold == 0U || event_count == NULL ||
		(events == NULL && event_capacity != 0U)) {
		return -EINVAL;
	}
	*event_count = 0U;
	for (size_t index = 0U; index < KB_MATRIX_KEY_COUNT; ++index) {
		uint64_t mask = UINT64_C(1) << index;
		bool current = (sampled_keys & mask) != 0U;
		bool stable = (state->stable_keys & mask) != 0U;

		if (current != stable && state->scan_counts[index] >= threshold - 1U) {
			++count;
		}
	}
	if (count > event_capacity || (count != 0U && events == NULL)) {
		return -ENOSPC;
	}
	count = 0U;
	for (size_t index = 0U; index < KB_MATRIX_KEY_COUNT; ++index) {
		uint64_t mask = UINT64_C(1) << index;
		bool current = (sampled_keys & mask) != 0U;
		bool stable = (state->stable_keys & mask) != 0U;

		if (current == stable) {
			state->scan_counts[index] = 0U;
			continue;
		}
		if (state->scan_counts[index] < threshold) {
			++state->scan_counts[index];
		}
		if (state->scan_counts[index] < threshold) {
			continue;
		}
		if (current) {
			state->stable_keys |= mask;
		} else {
			state->stable_keys &= ~mask;
		}
		state->scan_counts[index] = 0U;
		events[count++] = (struct kb_matrix_key_event) {
			.key_index = (uint8_t)index,
			.pressed = current,
		};
	}
	*event_count = count;
	return 0;
}
