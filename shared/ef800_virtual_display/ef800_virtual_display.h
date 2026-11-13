/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Capture and expose rendered LCD frames to the virtual host. */
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

/** @brief Initialize a blank frame and text plane.
 * @param display Display state to initialize.
 * @return None.
 */
void ef800_virtual_display_init(struct ef800_virtual_display *display);

/** @brief Clear frame and text state and mark it changed.
 * @param display Display state to clear.
 * @return None.
 */
void ef800_virtual_display_clear(struct ef800_virtual_display *display);

/** @brief Set or clear one pixel in the virtual frame.
 * @param display Display state.
 * @param x Horizontal pixel coordinate.
 * @param y Vertical pixel coordinate.
 * @param value True to set, false to clear.
 * @return 0 on update; negative errno when coordinates are out of range.
 */
int ef800_virtual_display_set_pixel(struct ef800_virtual_display *display,
					    uint16_t x, uint16_t y, bool value);

/** @brief Replace one fixed-width text row and update the display sequence.
 * @param display Display state.
 * @param row Text row index.
 * @param text Row text, padded/truncated to display width.
 * @return 0 on update; negative errno for invalid row or state.
 */
int ef800_virtual_display_write_line(struct ef800_virtual_display *display,
					     uint8_t row, const char *text);

/** @brief Check whether the frame has unacknowledged changes.
 * @param display Display state.
 * @return True when changed since the last acknowledgment.
 */
bool ef800_virtual_display_is_dirty(
	const struct ef800_virtual_display *display);

/** @brief Get the current frame sequence number.
 * @param display Display state.
 * @return Current sequence, or zero for NULL.
 */
uint32_t ef800_virtual_display_sequence(
	const struct ef800_virtual_display *display);

/** @brief Copy the current framebuffer and sequence to caller storage.
 * @param display Display state.
 * @param frame Destination framebuffer.
 * @param frame_size Destination capacity.
 * @param sequence Receives copied frame sequence.
 * @return 0 on copy; negative errno if arguments/capacity are invalid.
 */
int ef800_virtual_display_snapshot(const struct ef800_virtual_display *display,
					   uint8_t *frame, size_t frame_size,
					   uint32_t *sequence);

/** @brief Clear dirty state when the acknowledged sequence is still current.
 * @param display Display state.
 * @param sequence Sequence successfully consumed by the host.
 * @return None.
 */
void ef800_virtual_display_acknowledge(
	struct ef800_virtual_display *display, uint32_t sequence);

/** @brief Select the pixel merge operation used by virtual display writes.
 * @param display Display state.
 * @param merge Merge operation.
 * @return None.
 */
void ef800_virtual_display_set_merge(
	struct ef800_virtual_display *display,
	enum ef800_virtual_display_merge merge);

#endif
