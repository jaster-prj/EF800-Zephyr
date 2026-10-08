/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_DISPLAY_H
#define EF800_VIRTUAL_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EF800_VIRTUAL_DISPLAY_WIDTH 240U
#define EF800_VIRTUAL_DISPLAY_HEIGHT 64U
#define EF800_VIRTUAL_DISPLAY_ROW_BYTES 30U
#define EF800_VIRTUAL_DISPLAY_FRAME_SIZE 1920U
#define EF800_VIRTUAL_DISPLAY_TEXT_ROWS 8U
#define EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS 40U

enum ef800_virtual_display_merge {
	EF800_VIRTUAL_DISPLAY_MERGE_OR = 0,
	EF800_VIRTUAL_DISPLAY_MERGE_XOR,
	EF800_VIRTUAL_DISPLAY_MERGE_AND,
	EF800_VIRTUAL_DISPLAY_MERGE_INVERT,
};

struct ef800_virtual_display {
	uint8_t frame[EF800_VIRTUAL_DISPLAY_FRAME_SIZE];
	char text[EF800_VIRTUAL_DISPLAY_TEXT_ROWS]
		[EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS + 1U];
	uint32_t sequence;
	bool dirty;
	enum ef800_virtual_display_merge merge;
};

void ef800_virtual_display_init(struct ef800_virtual_display *display);
void ef800_virtual_display_clear(struct ef800_virtual_display *display);
int ef800_virtual_display_set_pixel(struct ef800_virtual_display *display,
					    uint16_t x, uint16_t y, bool value);
int ef800_virtual_display_write_line(struct ef800_virtual_display *display,
					     uint8_t row, const char *text);
bool ef800_virtual_display_is_dirty(
	const struct ef800_virtual_display *display);
uint32_t ef800_virtual_display_sequence(
	const struct ef800_virtual_display *display);
int ef800_virtual_display_snapshot(const struct ef800_virtual_display *display,
					   uint8_t *frame, size_t frame_size,
					   uint32_t *sequence);
void ef800_virtual_display_acknowledge(
	struct ef800_virtual_display *display, uint32_t sequence);
void ef800_virtual_display_set_merge(
	struct ef800_virtual_display *display,
	enum ef800_virtual_display_merge merge);

#endif
