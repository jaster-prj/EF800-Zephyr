/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_TRANSLATION_H
#define EF800_KEYBOARD_TRANSLATION_H

#include <stdbool.h>
#include <stdint.h>

#include "keyboard_protocol.h"

enum kb_translation_type {
	KB_TRANSLATION_NONE = 0,
	KB_TRANSLATION_CHAR = 1,
	KB_TRANSLATION_EVENT = 2,
};

struct kb_translation {
	enum kb_translation_type type;
	uint32_t value;
};

struct kb_wire_scancode {
	uint8_t prefix;
	uint8_t code;
	bool modifier;
};

struct kb_translator {
	bool shift;
	bool caps;
	bool ctrl;
	bool alt;
};

void kb_translator_init(struct kb_translator *translator);

/*
 * Convert an EF800 raw matrix/special-key code to its wire scancode.
 * Returns 0 for a valid, mapped key and -1 for an unused/invalid position.
 */
int kb_scancode_for_raw(uint16_t raw, struct kb_wire_scancode *scancode);

/* Returns true when the wire scancode is one of the four mbed modifiers. */
bool kb_scancode_is_modifier(uint8_t prefix, uint8_t code);

/*
 * Consume one make/break scancode.  Modifier state is updated for both make
 * and break.  The return value is 1 when output contains a character/event,
 * 0 when the scancode is valid but produces no output, and -1 when unknown.
 */
int kb_translate_scancode(struct kb_translator *translator, uint8_t prefix,
				  uint8_t code, bool release,
				  struct kb_translation *output);

#endif /* EF800_KEYBOARD_TRANSLATION_H */
