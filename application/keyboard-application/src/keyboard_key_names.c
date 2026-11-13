/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_key_names.h"

#include <stdbool.h>

#include "keyboard_protocol.h"

struct keyboard_key_name {
	const char *name;
	uint16_t raw;
};

/*
 * Names resolve to physical positions. The controller remains the only
 * component that turns those positions into wire scancodes.
 */
static const struct keyboard_key_name key_names[] = {
	{ "a", KB_MATRIX_RAW(2, 1) },
	{ "b", KB_MATRIX_RAW(1, 3) },
	{ "c", KB_MATRIX_RAW(0, 3) },
	{ "d", KB_MATRIX_RAW(0, 1) },
	{ "e", KB_MATRIX_RAW(0, 4) },
	{ "f", KB_MATRIX_RAW(0, 2) },
	{ "g", KB_MATRIX_RAW(1, 1) },
	{ "h", KB_MATRIX_RAW(1, 2) },
	{ "i", KB_MATRIX_RAW(3, 6) },
	{ "j", KB_MATRIX_RAW(3, 1) },
	{ "k", KB_MATRIX_RAW(3, 2) },
	{ "l", KB_MATRIX_RAW(5, 1) },
	{ "m", KB_MATRIX_RAW(3, 3) },
	{ "n", KB_MATRIX_RAW(1, 0) },
	{ "o", KB_MATRIX_RAW(5, 4) },
	{ "p", KB_MATRIX_RAW(5, 6) },
	{ "q", KB_MATRIX_RAW(2, 4) },
	{ "r", KB_MATRIX_RAW(0, 6) },
	{ "s", KB_MATRIX_RAW(2, 2) },
	{ "t", KB_MATRIX_RAW(1, 4) },
	{ "u", KB_MATRIX_RAW(3, 4) },
	{ "v", KB_MATRIX_RAW(0, 0) },
	{ "w", KB_MATRIX_RAW(2, 6) },
	{ "x", KB_MATRIX_RAW(2, 0) },
	{ "y", KB_MATRIX_RAW(1, 6) },
	{ "z", KB_MATRIX_RAW(2, 3) },
	{ "0", KB_MATRIX_RAW(6, 0) },
	{ "1", KB_MATRIX_RAW(4, 3) },
	{ "2", KB_MATRIX_RAW(6, 3) },
	{ "3", KB_MATRIX_RAW(7, 3) },
	{ "4", KB_MATRIX_RAW(4, 2) },
	{ "5", KB_MATRIX_RAW(6, 2) },
	{ "6", KB_MATRIX_RAW(7, 2) },
	{ "7", KB_MATRIX_RAW(4, 1) },
	{ "8", KB_MATRIX_RAW(6, 1) },
	{ "9", KB_MATRIX_RAW(7, 1) },
	{ "comma", KB_MATRIX_RAW(3, 0) },
	{ "period", KB_MATRIX_RAW(7, 0) },
	{ "minus", KB_MATRIX_RAW(4, 0) },
	{ "plus", KB_MATRIX_RAW(5, 0) },
	{ "less", KB_MATRIX_RAW(1, 5) },
	{ "caret", KB_MATRIX_RAW(3, 5) },
	{ "space", KB_MATRIX_RAW(5, 3) },
	{ "tab", KB_MATRIX_RAW(2, 5) },
	{ "enter", KB_MATRIX_RAW(5, 2) },
	{ "return", KB_MATRIX_RAW(5, 2) },
	{ "escape", KB_MATRIX_RAW(0, 5) },
	{ "esc", KB_MATRIX_RAW(0, 5) },
	{ "backspace", KB_MATRIX_RAW(7, 6) },
	{ "back", KB_MATRIX_RAW(7, 6) },
	{ "delete", KB_MATRIX_RAW(4, 6) },
	{ "insert", KB_MATRIX_RAW(5, 5) },
	{ "shift", KB_SPECIAL_RAW(1) },
	{ "ctrl", KB_SPECIAL_RAW(0) },
	{ "control", KB_SPECIAL_RAW(0) },
	{ "alt", KB_SPECIAL_RAW(3) },
	{ "caps", KB_SPECIAL_RAW(2) },
	{ "on", KB_SPECIAL_RAW(4) },
	{ "off", KB_MATRIX_RAW(4, 5) },
	{ "up", KB_MATRIX_RAW(6, 6) },
	{ "down", KB_MATRIX_RAW(6, 4) },
	{ "left", KB_MATRIX_RAW(4, 4) },
	{ "right", KB_MATRIX_RAW(7, 4) },
};

static char ascii_lower(char character)
{
	if (character >= 'A' && character <= 'Z') {
		return (char)(character + ('a' - 'A'));
	}
	return character;
}

static bool key_name_equal(const char *left, size_t length, const char *right)
{
	size_t index;

	for (index = 0U; index < length; ++index) {
		if (right[index] == '\0' || ascii_lower(left[index]) != right[index]) {
			return false;
		}
	}
	return right[length] == '\0';
}

int kb_key_name_to_raw(const char *name, size_t length, uint16_t *raw)
{
	size_t index;

	if (name == NULL || raw == NULL || length == 0U ||
		length > KB_DEBUG_KEY_NAME_MAX) {
		return -1;
	}

	for (index = 0U; index < sizeof(key_names) / sizeof(key_names[0]); ++index) {
		if (key_name_equal(name, length, key_names[index].name)) {
			*raw = key_names[index].raw;
			return 0;
		}
	}
	return -1;
}
