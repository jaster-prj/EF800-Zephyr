/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Capture and expose rendered LCD frames to the virtual host. */
#ifndef VIRTUAL_EF800_DISPLAY_H
#define VIRTUAL_EF800_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VIRTUAL_EF800_DISPLAY_WIDTH 240U
#define VIRTUAL_EF800_DISPLAY_HEIGHT 64U
#define VIRTUAL_EF800_DISPLAY_ROW_BYTES 30U
#define VIRTUAL_EF800_DISPLAY_FRAME_SIZE 1920U
#define VIRTUAL_EF800_DISPLAY_TEXT_ROWS 8U
#define VIRTUAL_EF800_DISPLAY_TEXT_COLUMNS 40U

enum virtual_ef800_display_merge {
	VIRTUAL_EF800_DISPLAY_MERGE_OR = 0,
	VIRTUAL_EF800_DISPLAY_MERGE_XOR,
	VIRTUAL_EF800_DISPLAY_MERGE_AND,
	VIRTUAL_EF800_DISPLAY_MERGE_INVERT,
};

struct virtual_ef800_display {
	uint8_t frame[VIRTUAL_EF800_DISPLAY_FRAME_SIZE];
	char text[VIRTUAL_EF800_DISPLAY_TEXT_ROWS]
		[VIRTUAL_EF800_DISPLAY_TEXT_COLUMNS + 1U];
	uint32_t sequence;
	bool dirty;
	enum virtual_ef800_display_merge merge;
};

/** @brief Initialize a blank frame and text plane.
 * @param display Display state to initialize.
 * @return None.
 */
void virtual_ef800_display_init(struct virtual_ef800_display *display);

/** @brief Clear frame and text state and mark it changed.
 * @param display Display state to clear.
 * @return None.
 */
void virtual_ef800_display_clear(struct virtual_ef800_display *display);

/** @brief Set or clear one pixel in the virtual frame.
 * @param display Display state.
 * @param x Horizontal pixel coordinate.
 * @param y Vertical pixel coordinate.
 * @param value True to set, false to clear.
 * @return 0 on update; negative errno when coordinates are out of range.
 */
int virtual_ef800_display_set_pixel(struct virtual_ef800_display *display,
					    uint16_t x, uint16_t y, bool value);

/** @brief Replace one fixed-width text row and update the display sequence.
 * @param display Display state.
 * @param row Text row index.
 * @param text Row text, padded/truncated to display width.
 * @return 0 on update; negative errno for invalid row or state.
 */
int virtual_ef800_display_write_line(struct virtual_ef800_display *display,
					     uint8_t row, const char *text);

/** @brief Check whether the frame has unacknowledged changes.
 * @param display Display state.
 * @return True when changed since the last acknowledgment.
 */
bool virtual_ef800_display_is_dirty(
	const struct virtual_ef800_display *display);

/** @brief Get the current frame sequence number.
 * @param display Display state.
 * @return Current sequence, or zero for NULL.
 */
uint32_t virtual_ef800_display_sequence(
	const struct virtual_ef800_display *display);

/** @brief Copy the current framebuffer and sequence to caller storage.
 * @param display Display state.
 * @param frame Destination framebuffer.
 * @param frame_size Destination capacity.
 * @param sequence Receives copied frame sequence.
 * @return 0 on copy; negative errno if arguments/capacity are invalid.
 */
int virtual_ef800_display_snapshot(const struct virtual_ef800_display *display,
					   uint8_t *frame, size_t frame_size,
					   uint32_t *sequence);

/** @brief Clear dirty state when the acknowledged sequence is still current.
 * @param display Display state.
 * @param sequence Sequence successfully consumed by the host.
 * @return None.
 */
void virtual_ef800_display_acknowledge(
	struct virtual_ef800_display *display, uint32_t sequence);

/** @brief Select the pixel merge operation used by virtual display writes.
 * @param display Display state.
 * @param merge Merge operation.
 * @return None.
 */
void virtual_ef800_display_set_merge(
	struct virtual_ef800_display *display,
	enum virtual_ef800_display_merge merge);

#endif
