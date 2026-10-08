/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_virtual_keyboard.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "keyboard_key_names.h"
#include "keyboard_translation.h"

static int find_key(const char *name, size_t length, uint16_t *raw)
{
	return kb_key_name_to_raw(name, length, raw) == 0 ? 0 : -EINVAL;
}

static int append(struct ef800_virtual_keyboard *keyboard, uint8_t value)
{
	if (keyboard->output_length >= sizeof(keyboard->output)) {
		return -ENOSPC;
	}
	keyboard->output[keyboard->output_length++] = value;
	return 0;
}

static int append_scancode(struct ef800_virtual_keyboard *keyboard,
				   uint16_t raw, bool release)
{
	struct kb_wire_scancode scancode;

	if (kb_scancode_for_raw(raw, &scancode) != 0) {
		return -EINVAL;
	}
	if (scancode.prefix != KB_SCANCODE_PREFIX_NONE &&
		append(keyboard, scancode.prefix) != 0) {
		return -ENOSPC;
	}
	if (release && append(keyboard, KB_SCANCODE_BREAK) != 0) {
		return -ENOSPC;
	}
	return append(keyboard, scancode.code);
}

static void record_event(struct ef800_virtual_keyboard *keyboard,
				 const char *name, size_t length, bool pressed)
{
	size_t copy_length = length < EF800_VIRTUAL_KEYBOARD_NAME_SIZE - 1U ?
		length : EF800_VIRTUAL_KEYBOARD_NAME_SIZE - 1U;
	memcpy(keyboard->pending_name, name, copy_length);
	keyboard->pending_name[copy_length] = '\0';
	keyboard->pending_press = pressed;
	keyboard->pending_valid = true;
}

void ef800_virtual_keyboard_init(struct ef800_virtual_keyboard *keyboard)
{
	if (keyboard == NULL) {
		return;
	}
	memset(keyboard, 0, sizeof(*keyboard));
	(void)append(keyboard, CMD_INITIALIZED);
}

int ef800_virtual_keyboard_press(struct ef800_virtual_keyboard *keyboard,
					 const char *name, size_t length)
{
	uint16_t raw;

	if (keyboard == NULL || find_key(name, length, &raw) != 0) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < keyboard->held_count; ++index) {
		if (keyboard->held[index] == raw) {
			return 0;
		}
	}
	if (keyboard->held_count >= ARRAY_SIZE(keyboard->held)) {
		return -ENOSPC;
	}
	if (append_scancode(keyboard, raw, false) != 0) {
		return -ENOSPC;
	}
	keyboard->held[keyboard->held_count++] = raw;
	record_event(keyboard, name, length, true);
	return 0;
}

int ef800_virtual_keyboard_release(struct ef800_virtual_keyboard *keyboard,
					   const char *name, size_t length)
{
	uint16_t raw;

	if (keyboard == NULL || find_key(name, length, &raw) != 0) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < keyboard->held_count; ++index) {
		if (keyboard->held[index] == raw) {
			if (append_scancode(keyboard, raw, true) != 0) {
				return -ENOSPC;
			}
			memmove(&keyboard->held[index], &keyboard->held[index + 1U],
				(keyboard->held_count - index - 1U) * sizeof(raw));
			--keyboard->held_count;
			record_event(keyboard, name, length, false);
			return 0;
		}
	}
	return 0;
}

void ef800_virtual_keyboard_release_all(
	struct ef800_virtual_keyboard *keyboard)
{
	if (keyboard == NULL) {
		return;
	}
	while (keyboard->held_count != 0U) {
		uint16_t raw = keyboard->held[keyboard->held_count - 1U];
		(void)append_scancode(keyboard, raw, true);
		--keyboard->held_count;
	}
}

size_t ef800_virtual_keyboard_read(struct ef800_virtual_keyboard *keyboard,
					   uint8_t *output, size_t capacity)
{
	size_t length;

	if (keyboard == NULL || output == NULL || capacity == 0U) {
		return 0U;
	}
	length = keyboard->output_length < capacity ? keyboard->output_length : capacity;
	memcpy(output, keyboard->output, length);
	memmove(keyboard->output, &keyboard->output[length],
		keyboard->output_length - length);
	keyboard->output_length -= length;
	return length;
}

bool ef800_virtual_keyboard_take_event(struct ef800_virtual_keyboard *keyboard,
							char *name, size_t capacity, bool *pressed)
{
	if (keyboard == NULL || name == NULL || capacity == 0U ||
		pressed == NULL || !keyboard->pending_valid) {
		return false;
	}
	strncpy(name, keyboard->pending_name, capacity - 1U);
	name[capacity - 1U] = '\0';
	*pressed = keyboard->pending_press;
	keyboard->pending_valid = false;
	return true;
}
