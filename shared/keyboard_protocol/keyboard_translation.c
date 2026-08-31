/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_translation.h"

#include <stddef.h>

struct kb_map_entry {
	uint8_t prefix;
	uint8_t code;
	struct kb_translation combinations[8];
};

#define KB_CHAR(value) { KB_TRANSLATION_CHAR, (uint32_t)(value) }
#define KB_EVENT(value) { KB_TRANSLATION_EVENT, (uint32_t)(value) }
#define KB_NONE { KB_TRANSLATION_NONE, 0U }

/*
 * This table is the mbed-uart-keyboard ASCII map expressed without dynamic
 * TypeCode objects.  The '+' scancode is 0x1B, matching the EF800 controller
 * table; the old mbed ASCII table contained a duplicate 0x18 typo there.
 */
static const struct kb_map_entry kb_ascii_map[] = {
	{ 0x00, 0x2F, { KB_CHAR('v'), KB_CHAR('V'), KB_NONE, KB_EVENT(KB_EVENT_PAST),
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x20, { KB_CHAR('d'), KB_CHAR('D'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x21, { KB_CHAR('f'), KB_CHAR('F'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x2E, { KB_CHAR('c'), KB_CHAR('C'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x12, { KB_CHAR('e'), KB_CHAR('E'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x01, { KB_CHAR(0x1B), KB_CHAR(0x1B), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x13, { KB_CHAR('r'), KB_CHAR('R'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x31, { KB_CHAR('n'), KB_CHAR('N'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x22, { KB_CHAR('g'), KB_CHAR('G'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x23, { KB_CHAR('h'), KB_CHAR('H'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x30, { KB_CHAR('b'), KB_CHAR('B'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x14, { KB_CHAR('t'), KB_CHAR('T'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x56, { KB_CHAR('<'), KB_CHAR('>'), KB_CHAR('|'), KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x2C, { KB_CHAR('y'), KB_CHAR('Y'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x2D, { KB_CHAR('x'), KB_CHAR('X'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x1E, { KB_CHAR('a'), KB_CHAR('A'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x1F, { KB_CHAR('s'), KB_CHAR('S'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x15, { KB_CHAR('z'), KB_CHAR('Z'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x10, { KB_CHAR('q'), KB_CHAR('Q'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x0F, { KB_CHAR(0x09), KB_NONE, KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x11, { KB_CHAR('w'), KB_CHAR('W'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x33, { KB_CHAR(','), KB_CHAR(';'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x24, { KB_CHAR('j'), KB_CHAR('J'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x25, { KB_CHAR('k'), KB_CHAR('K'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x32, { KB_CHAR('m'), KB_CHAR('M'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x16, { KB_CHAR('u'), KB_CHAR('U'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x29, { KB_CHAR('^'), KB_CHAR('?'), KB_CHAR('\\'), KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x17, { KB_CHAR('i'), KB_CHAR('I'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x35, { KB_CHAR('-'), KB_CHAR('_'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x08, { KB_CHAR('7'), KB_CHAR('/'), KB_CHAR('&'), KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x05, { KB_CHAR('4'), KB_CHAR('$'), KB_CHAR('~'), KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x02, { KB_CHAR('1'), KB_CHAR('!'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ KB_SCANCODE_PREFIX_E0, 0x48, { KB_EVENT(KB_EVENT_ARROWLEFT), KB_EVENT(KB_EVENT_FUNC4),
				KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ KB_SCANCODE_PREFIX_E0, 0x5F, { KB_EVENT(KB_EVENT_OFF), KB_EVENT(KB_EVENT_OFF),
				KB_EVENT(KB_EVENT_OFF), KB_EVENT(KB_EVENT_OFF), KB_EVENT(KB_EVENT_OFF),
				KB_EVENT(KB_EVENT_OFF), KB_EVENT(KB_EVENT_OFF), KB_EVENT(KB_EVENT_OFF) } },
	{ KB_SCANCODE_PREFIX_E0, 0x22, { KB_CHAR(0x7F), KB_EVENT(KB_EVENT_FUNC1),
				KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x1B, { KB_CHAR('+'), KB_CHAR('*'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x26, { KB_CHAR('l'), KB_CHAR('L'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x1C, { KB_CHAR(0x0D), KB_CHAR(0x0D), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x39, { KB_CHAR(' '), KB_CHAR(' '), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x18, { KB_CHAR('o'), KB_CHAR('O'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ KB_SCANCODE_PREFIX_E0, 0x52, { KB_EVENT(KB_EVENT_INSERT), KB_EVENT(KB_EVENT_INSERT),
				KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x19, { KB_CHAR('p'), KB_CHAR('P'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x0B, { KB_CHAR('0'), KB_CHAR('='), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x09, { KB_CHAR('8'), KB_CHAR('('), KB_CHAR('['), KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x06, { KB_CHAR('5'), KB_CHAR('%'), KB_CHAR('{'), KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x03, { KB_CHAR('2'), KB_CHAR('"'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ KB_SCANCODE_PREFIX_E0, 0x50, { KB_EVENT(KB_EVENT_ARROWDOWN), KB_EVENT(KB_EVENT_FUNC5),
				KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ KB_SCANCODE_PREFIX_E0, 0x48, { KB_EVENT(KB_EVENT_ARROWUP), KB_EVENT(KB_EVENT_FUNC2),
				KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x34, { KB_CHAR('.'), KB_CHAR(':'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x0A, { KB_CHAR('9'), KB_CHAR(')'), KB_CHAR(']'), KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x07, { KB_CHAR('6'), KB_CHAR('\''), KB_CHAR('}'), KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x04, { KB_CHAR('3'), KB_CHAR('#'), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ KB_SCANCODE_PREFIX_E0, 0x40, { KB_EVENT(KB_EVENT_ARROWRIGHT), KB_EVENT(KB_EVENT_FUNC6),
				KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ 0x00, 0x0E, { KB_CHAR(0x08), KB_EVENT(KB_EVENT_FUNC3), KB_NONE, KB_NONE,
				KB_NONE, KB_NONE, KB_NONE, KB_NONE } },
	{ KB_SCANCODE_PREFIX_E0, 0x5E, { KB_EVENT(KB_EVENT_ON), KB_EVENT(KB_EVENT_ON),
				KB_EVENT(KB_EVENT_ON), KB_EVENT(KB_EVENT_ON), KB_EVENT(KB_EVENT_ON),
				KB_EVENT(KB_EVENT_ON), KB_EVENT(KB_EVENT_ON), KB_EVENT(KB_EVENT_ON) } },
};

/* Indexing follows the controller's (row * 7 + col) lookup. */
static const struct kb_wire_scancode kb_raw_scancodes[61] = {
	{ 0x00, 0x2F, false }, { 0x00, 0x20, false }, { 0x00, 0x21, false },
	{ 0x00, 0x2E, false }, { 0x00, 0x12, false }, { 0x00, 0x01, false },
	{ 0x00, 0x13, false }, { 0x00, 0x31, false }, { 0x00, 0x22, false },
	{ 0x00, 0x23, false }, { 0x00, 0x30, false }, { 0x00, 0x14, false },
	{ 0x00, 0x56, false }, { 0x00, 0x2C, false }, { 0x00, 0x2D, false },
	{ 0x00, 0x1E, false }, { 0x00, 0x1F, false }, { 0x00, 0x15, false },
	{ 0x00, 0x10, false }, { 0x00, 0x0F, false }, { 0x00, 0x11, false },
	{ 0x00, 0x33, false }, { 0x00, 0x24, false }, { 0x00, 0x25, false },
	{ 0x00, 0x32, false }, { 0x00, 0x16, false }, { 0x00, 0x29, false },
	{ 0x00, 0x17, false }, { 0x00, 0x35, false }, { 0x00, 0x08, false },
	{ 0x00, 0x05, false }, { 0x00, 0x02, false }, { 0xE0, 0x48, false },
	{ 0xE0, 0x5F, true },  { 0xE0, 0x22, false }, { 0x00, 0x1B, false },
	{ 0x00, 0x26, false }, { 0x00, 0x1C, false }, { 0x00, 0x39, false },
	{ 0x00, 0x18, false }, { 0xE0, 0x52, false }, { 0x00, 0x19, false },
	{ 0x00, 0x0B, false }, { 0x00, 0x09, false }, { 0x00, 0x06, false },
	{ 0x00, 0x03, false }, { 0xE0, 0x50, false }, { 0x00, 0x00, false },
	{ 0xE0, 0x48, false }, { 0x00, 0x34, false }, { 0x00, 0x0A, false },
	{ 0x00, 0x07, false }, { 0x00, 0x04, false }, { 0xE0, 0x40, false },
	{ 0x00, 0x00, false }, { 0x00, 0x0E, false }, { 0x00, 0x1D, true },
	{ 0x00, 0x2A, true }, { 0x00, 0x3A, true }, { 0x00, 0x38, true },
	{ 0xE0, 0x5E, true },
};

#undef KB_CHAR
#undef KB_EVENT
#undef KB_NONE

void kb_translator_init(struct kb_translator *translator)
{
	if (translator == NULL) {
		return;
	}
	translator->shift = false;
	translator->caps = false;
	translator->ctrl = false;
	translator->alt = false;
}

int kb_scancode_for_raw(uint16_t raw, struct kb_wire_scancode *scancode)
{
	uint16_t index;

	if (scancode == NULL) {
		return -1;
	}

	if ((raw & 0xF000U) != 0U) {
		return -1;
	}
	if ((raw & 0x0F00U) == 0U && (raw >> 4) < KB_MATRIX_ROWS &&
	    (raw & 0x0FU) < KB_MATRIX_COLS) {
		index = (uint16_t)((raw >> 4) * KB_MATRIX_COLS + (raw & 0x0FU));
	} else if (raw >= KB_SPECIAL_RAW(0) && raw < KB_SPECIAL_RAW(KB_SPECIAL_KEYS)) {
		index = (uint16_t)(KB_MATRIX_ROWS * KB_MATRIX_COLS +
				   (raw - KB_SPECIAL_RAW(0)));
	} else {
		return -1;
	}

	if (kb_raw_scancodes[index].code == 0U) {
		return -1;
	}
	*scancode = kb_raw_scancodes[index];
	return 0;
}

bool kb_scancode_is_modifier(uint8_t prefix, uint8_t code)
{
	return prefix == KB_SCANCODE_PREFIX_NONE &&
		(code == 0x1DU || code == 0x2AU || code == 0x3AU || code == 0x38U);
}

static const struct kb_map_entry *find_map_entry(uint8_t prefix, uint8_t code)
{
	size_t i;

	for (i = 0U; i < sizeof(kb_ascii_map) / sizeof(kb_ascii_map[0]); ++i) {
		if (kb_ascii_map[i].prefix == prefix && kb_ascii_map[i].code == code) {
			return &kb_ascii_map[i];
		}
	}
	return NULL;
}

static void update_modifier(struct kb_translator *translator, uint8_t code,
				    bool release)
{
	switch (code) {
	case 0x1D:
		translator->ctrl = !release;
		break;
	case 0x2A:
		translator->shift = !release;
		break;
	case 0x3A:
		if (!release) {
			translator->caps = !translator->caps;
		}
		break;
	case 0x38:
		translator->alt = !release;
		break;
	default:
		break;
	}
}

static uint8_t map_index(uint8_t combination)
{
	/* The table follows mbed's named fields, not bit-number order. */
	switch (combination) {
	case KB_MODIFIER_NONE:
		return 0U;
	case KB_MODIFIER_SHIFT:
		return 1U;
	case KB_MODIFIER_ALT:
		return 2U;
	case KB_MODIFIER_CTRL:
		return 3U;
	case KB_MODIFIER_SHIFTALT:
		return 4U;
	case KB_MODIFIER_ALTCTRL:
		return 5U;
	case KB_MODIFIER_CTRLSHIFT:
		return 6U;
	case KB_MODIFIER_CTRLALTSHIFT:
		return 7U;
	default:
		return 0U;
	}
}

int kb_translate_scancode(struct kb_translator *translator, uint8_t prefix,
				  uint8_t code, bool release,
				  struct kb_translation *output)
{
	const struct kb_map_entry *entry;
	uint8_t combination;

	if (translator == NULL || output == NULL) {
		return -1;
	}
	output->type = KB_TRANSLATION_NONE;
	output->value = 0U;

	if (kb_scancode_is_modifier(prefix, code)) {
		update_modifier(translator, code, release);
		return 0;
	}
	if (release) {
		return find_map_entry(prefix, code) != NULL ? 0 : -1;
	}

	entry = find_map_entry(prefix, code);
	if (entry == NULL) {
		return -1;
	}

	/* Shift and caps are XORed for the character map, as in mbed code. */
	combination = (uint8_t)((translator->ctrl << MODIFIER_CTRL) |
					(translator->alt << MODIFIER_ALT) |
					((translator->shift ^ translator->caps) << MODIFIER_SHIFT));
	*output = entry->combinations[map_index(combination)];
	return output->type == KB_TRANSLATION_NONE ? 0 : 1;
}
