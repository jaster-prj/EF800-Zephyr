/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_keyboard_boot.h"

#include <stddef.h>

static uint32_t boot_crc(const struct ef800_keyboard_boot_state *state)
{
	const uint8_t *bytes = (const uint8_t *)state;
	uint32_t crc = 0xFFFFFFFFU;

	for (size_t i = 0U; i < offsetof(struct ef800_keyboard_boot_state, crc32); ++i) {
		crc ^= bytes[i];
		for (uint8_t bit = 0U; bit < 8U; ++bit) {
			crc = (crc >> 1U) ^ (0xEDB88320U & -(crc & 1U));
		}
	}
	return ~crc;
}

struct ef800_keyboard_boot_state *ef800_keyboard_boot_state_get(void)
{
	return (struct ef800_keyboard_boot_state *)EF800_KEYBOARD_BOOT_STATE_ADDRESS;
}

bool ef800_keyboard_boot_state_valid(
	const struct ef800_keyboard_boot_state *state)
{
	return state != NULL && state->magic == EF800_KEYBOARD_BOOT_STATE_MAGIC &&
		state->version == EF800_KEYBOARD_BOOT_STATE_VERSION &&
		state->size == sizeof(*state) && state->phase <= EF800_KEYBOARD_BOOT_HEALTHY &&
		state->request <= EF800_KEYBOARD_BOOT_REQUEST_SYSTEM &&
		state->watchdog_starts <= EF800_KEYBOARD_BOOT_MAX_WATCHDOG_STARTS &&
		state->crc32 == boot_crc(state);
}

void ef800_keyboard_boot_state_initialize(
	struct ef800_keyboard_boot_state *state)
{
	if (state == NULL) {
		return;
	}
	*state = (struct ef800_keyboard_boot_state){
		.magic = EF800_KEYBOARD_BOOT_STATE_MAGIC,
		.version = EF800_KEYBOARD_BOOT_STATE_VERSION,
		.size = sizeof(*state),
		.phase = EF800_KEYBOARD_BOOT_NONE,
	};
	state->crc32 = boot_crc(state);
}

static void boot_store(struct ef800_keyboard_boot_state *state)
{
	state->crc32 = 0U;
	state->crc32 = boot_crc(state);
}

bool ef800_keyboard_boot_prepare(struct ef800_keyboard_boot_state *state,
	bool watchdog_reset, uint32_t reset_cause)
{
	if (!ef800_keyboard_boot_state_valid(state)) {
		ef800_keyboard_boot_state_initialize(state);
	}
	state->reset_cause = reset_cause;
	if (watchdog_reset && state->phase == EF800_KEYBOARD_BOOT_PENDING &&
		state->watchdog_starts < EF800_KEYBOARD_BOOT_MAX_WATCHDOG_STARTS) {
		state->watchdog_starts++;
	}
	state->request = EF800_KEYBOARD_BOOT_REQUEST_NONE;
	if (state->watchdog_starts >= EF800_KEYBOARD_BOOT_MAX_WATCHDOG_STARTS) {
		boot_store(state);
		return true;
	}
	state->phase = EF800_KEYBOARD_BOOT_PENDING;
	boot_store(state);
	return false;
}

void ef800_keyboard_boot_request_system(
	struct ef800_keyboard_boot_state *state)
{
	if (!ef800_keyboard_boot_state_valid(state)) {
		ef800_keyboard_boot_state_initialize(state);
	}
	state->request = EF800_KEYBOARD_BOOT_REQUEST_SYSTEM;
	boot_store(state);
}

bool ef800_keyboard_boot_consume_system_request(
	struct ef800_keyboard_boot_state *state)
{
	bool requested;

	if (!ef800_keyboard_boot_state_valid(state)) {
		ef800_keyboard_boot_state_initialize(state);
		return false;
	}
	requested = state->request == EF800_KEYBOARD_BOOT_REQUEST_SYSTEM;
	state->request = EF800_KEYBOARD_BOOT_REQUEST_NONE;
	if (requested) {
		boot_store(state);
	}
	return requested;
}

bool ef800_keyboard_boot_system_request_pending(
	const struct ef800_keyboard_boot_state *state)
{
	return ef800_keyboard_boot_state_valid(state) &&
		state->request == EF800_KEYBOARD_BOOT_REQUEST_SYSTEM;
}

void ef800_keyboard_boot_confirm(struct ef800_keyboard_boot_state *state)
{
	if (ef800_keyboard_boot_state_valid(state)) {
		state->phase = EF800_KEYBOARD_BOOT_HEALTHY;
		state->watchdog_starts = 0U;
		boot_store(state);
	}
}
