/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_virtual_display.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "cgrom_0101.h"

static void changed(struct ef800_virtual_display *display)
{
	++display->sequence;
	if (display->sequence == 0U) {
		++display->sequence;
	}
	display->dirty = true;
}

static void compose_text(const struct ef800_virtual_display *display,
				 uint8_t *frame)
{
	for (uint8_t row = 0U; row < EF800_VIRTUAL_DISPLAY_TEXT_ROWS; ++row) {
		for (uint8_t column = 0U; column < EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS; ++column) {
			char character = display->text[row][column];
			if ((uint8_t)character >= 0x80U) continue;
			for (uint8_t glyph_column_index = 0U; glyph_column_index < 6U; ++glyph_column_index) {
				for (uint8_t glyph_row = 0U; glyph_row < 8U; ++glyph_row) {
					uint8_t glyph = cgrom_0101[(uint8_t)character][glyph_row];
					uint8_t glyph_bit = (uint8_t)(5U - glyph_column_index);
					if ((glyph & BIT(glyph_bit)) == 0U) continue;
					uint16_t x = (uint16_t)column * 6U + glyph_column_index;
					uint16_t y = (uint16_t)row * 8U + glyph_row;
					size_t offset = (size_t)y * EF800_VIRTUAL_DISPLAY_ROW_BYTES + x / 8U;
					uint8_t mask = BIT(x % 8U);
					switch (display->merge) {
					case EF800_VIRTUAL_DISPLAY_MERGE_XOR: frame[offset] ^= mask; break;
					case EF800_VIRTUAL_DISPLAY_MERGE_AND: frame[offset] &= (uint8_t)~mask; break;
					case EF800_VIRTUAL_DISPLAY_MERGE_INVERT: frame[offset] ^= mask; break;
					default: frame[offset] |= mask; break;
					}
				}
			}
		}
	}
}

void ef800_virtual_display_init(struct ef800_virtual_display *display)
{
	if (display == NULL) {
		return;
	}
	memset(display, 0, sizeof(*display));
	display->merge = EF800_VIRTUAL_DISPLAY_MERGE_OR;
	changed(display);
}

void ef800_virtual_display_clear(struct ef800_virtual_display *display)
{
	if (display == NULL) {
		return;
	}
	memset(display->frame, 0, sizeof(display->frame));
	memset(display->text, 0, sizeof(display->text));
	for (size_t row = 0U; row < EF800_VIRTUAL_DISPLAY_TEXT_ROWS; ++row) {
		memset(display->text[row], ' ', EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS);
		display->text[row][EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS] = '\0';
	}
	changed(display);
}

int ef800_virtual_display_set_pixel(struct ef800_virtual_display *display,
					    uint16_t x, uint16_t y, bool value)
{
	size_t offset;
	uint8_t mask;
	uint8_t old;

	if (display == NULL || x >= EF800_VIRTUAL_DISPLAY_WIDTH ||
		y >= EF800_VIRTUAL_DISPLAY_HEIGHT) {
		return -EINVAL;
	}
	offset = (size_t)y * EF800_VIRTUAL_DISPLAY_ROW_BYTES + x / 8U;
	mask = (uint8_t)(1U << (x % 8U));
	old = display->frame[offset];
	if (value) {
		display->frame[offset] |= mask;
	} else {
		display->frame[offset] &= (uint8_t)~mask;
	}
	if (old != display->frame[offset]) {
		changed(display);
	}
	return 0;
}

int ef800_virtual_display_write_line(struct ef800_virtual_display *display,
					     uint8_t row, const char *text)
{
	if (display == NULL || row >= EF800_VIRTUAL_DISPLAY_TEXT_ROWS ||
		text == NULL) {
		return -EINVAL;
	}
	memset(display->text[row], ' ', EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS);
	strncpy(display->text[row], text, EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS);
	display->text[row][EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS] = '\0';
	changed(display);
	return 0;
}

bool ef800_virtual_display_is_dirty(
	const struct ef800_virtual_display *display)
{
	return display != NULL && display->dirty;
}

uint32_t ef800_virtual_display_sequence(
	const struct ef800_virtual_display *display)
{
	return display == NULL ? 0U : display->sequence;
}

int ef800_virtual_display_snapshot(const struct ef800_virtual_display *display,
					   uint8_t *frame, size_t frame_size,
					   uint32_t *sequence)
{
	if (display == NULL || frame == NULL || frame_size < sizeof(display->frame) ||
		sequence == NULL) {
		return -EINVAL;
	}
	memcpy(frame, display->frame, sizeof(display->frame));
	compose_text(display, frame);
	*sequence = display->sequence;
	return 0;
}

void ef800_virtual_display_set_merge(
	struct ef800_virtual_display *display,
	enum ef800_virtual_display_merge merge)
{
	if (display != NULL && merge <= EF800_VIRTUAL_DISPLAY_MERGE_INVERT &&
		display->merge != merge) {
		display->merge = merge;
		changed(display);
	}
}

void ef800_virtual_display_acknowledge(
	struct ef800_virtual_display *display, uint32_t sequence)
{
	if (display != NULL && sequence == display->sequence) {
		display->dirty = false;
	}
}
