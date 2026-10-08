/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_KEYBOARD_H
#define EF800_VIRTUAL_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard_protocol.h"

#define EF800_VIRTUAL_KEYBOARD_MAX_HELD 64U
#define EF800_VIRTUAL_KEYBOARD_OUTPUT_SIZE 256U
#define EF800_VIRTUAL_KEYBOARD_NAME_SIZE 8U

struct ef800_virtual_keyboard {
	uint16_t held[EF800_VIRTUAL_KEYBOARD_MAX_HELD];
	size_t held_count;
	uint8_t output[EF800_VIRTUAL_KEYBOARD_OUTPUT_SIZE];
	size_t output_length;
	char pending_name[EF800_VIRTUAL_KEYBOARD_NAME_SIZE];
	bool pending_press;
	bool pending_valid;
};

void ef800_virtual_keyboard_init(struct ef800_virtual_keyboard *keyboard);
int ef800_virtual_keyboard_press(struct ef800_virtual_keyboard *keyboard,
					 const char *name, size_t length);
int ef800_virtual_keyboard_release(struct ef800_virtual_keyboard *keyboard,
					   const char *name, size_t length);
void ef800_virtual_keyboard_release_all(
	struct ef800_virtual_keyboard *keyboard);
size_t ef800_virtual_keyboard_read(struct ef800_virtual_keyboard *keyboard,
							uint8_t *output, size_t capacity);
bool ef800_virtual_keyboard_take_event(struct ef800_virtual_keyboard *keyboard,
							char *name, size_t capacity, bool *pressed);

#endif
