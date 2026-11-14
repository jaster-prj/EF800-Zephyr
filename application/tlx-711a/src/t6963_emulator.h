/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Emulate T6963 controller commands, memory, and display state. */
#ifndef TLX711A_T6963_EMULATOR_H
#define TLX711A_T6963_EMULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TLX711A_WIDTH 240U
#define TLX711A_HEIGHT 64U
#define TLX711A_ROW_BYTES ((TLX711A_WIDTH + 7U) / 8U)
#define TLX711A_FRAME_SIZE (TLX711A_HEIGHT * TLX711A_ROW_BYTES)

struct t6963_emulator_config {
	size_t memory_size;
	uint16_t hardware_columns;
	uint16_t text_rows;
	uint8_t font_width;
	uint8_t text_area;
	uint8_t graphics_area;
	bool dual_scan;
};

struct t6963_emulator {
	struct t6963_emulator_config config;
	uint8_t *memory;
	size_t memory_size;
	uint8_t *framebuffer;
	size_t framebuffer_size;
	uint16_t framebuffer_width;
	uint16_t framebuffer_height;
	uint16_t address;
	uint16_t text_home;
	uint16_t graphics_home;
	uint8_t text_area;
	uint8_t graphics_area;
	uint8_t font_width;
	uint8_t offset;
	uint8_t mode;
	uint8_t display_mode;
	uint8_t cursor_x;
	uint8_t cursor_y;
	uint8_t cursor_pattern;
	uint8_t pending_data[2];
	uint8_t pending_count;
	int8_t address_step;
	uint8_t auto_mode;
	bool data_read_pending;
	uint8_t read_latch;
	bool read_latch_valid;
	bool screen_error;
	bool blink_phase;
	bool dirty;
	uint16_t dirty_x0;
	uint16_t dirty_y0;
	uint16_t dirty_x1;
	uint16_t dirty_y1;
	bool framebuffer_changed;
};

struct t6963_memory_stats {
	uint16_t text_nonzero;
	uint16_t text_first_address;
	uint16_t text_last_address;
	uint16_t graphics_nonzero;
	uint16_t graphics_first_address;
	uint16_t graphics_last_address;
};

/** @brief Initialize emulator memory, framebuffer, and controller geometry.
 * @param emu Emulator state.
 * @param config Controller memory and display layout.
 * @param memory Backing controller memory buffer.
 * @param memory_size Backing memory capacity.
 * @param framebuffer Pixel buffer.
 * @param framebuffer_size Pixel buffer capacity.
 * @return 0 on init; negative errno if sizes/configuration are invalid.
 */
int t6963_emulator_init_config(struct t6963_emulator *emu,
		const struct t6963_emulator_config *config,
		uint8_t *memory, size_t memory_size,
		uint8_t *framebuffer, size_t framebuffer_size);

/** @brief Initialize an emulator with default controller configuration.
 * @param emu Emulator state.
 * @return None.
 */
void t6963_emulator_init(struct t6963_emulator *emu);

/** @brief Execute one controller command byte.
 * @param emu Emulator state.
 * @param command Command byte.
 * @return None.
 */
void t6963_emulator_command(struct t6963_emulator *emu, uint8_t command);

/** @brief Supply one data byte to the current controller command.
 * @param emu Emulator state.
 * @param data Data byte.
 * @return None.
 */
void t6963_emulator_data(struct t6963_emulator *emu, uint8_t data);

/** @brief Read controller status flags.
 * @param emu Emulator state.
 * @return Encoded status byte.
 */
uint8_t t6963_emulator_read_status(const struct t6963_emulator *emu);

/** @brief Read the next controller data byte.
 * @param emu Emulator state.
 * @return Data byte or controller-defined default when no read is pending.
 */
uint8_t t6963_emulator_read_data(struct t6963_emulator *emu);

/** @brief Copy the current pixel frame to caller storage.
 * @param emu Emulator state.
 * @param frame Destination buffer.
 * @param capacity Destination capacity.
 * @return None; copy is bounded by available framebuffer and capacity.
 */
void t6963_emulator_frame(struct t6963_emulator *emu,
		uint8_t *frame, size_t capacity);

/** @brief Check whether framebuffer contents changed since acknowledgment.
 * @param emu Emulator state.
 * @return True when changed.
 */
bool t6963_emulator_frame_changed(const struct t6963_emulator *emu);

/** @brief Clear the framebuffer-changed indicator.
 * @param emu Emulator state.
 * @return None.
 */
void t6963_emulator_clear_frame_changed(struct t6963_emulator *emu);

/** @brief Set the controller's cursor blink visibility phase.
 * @param emu Emulator state.
 * @param phase True when cursor should be visible.
 * @return None.
 */
void t6963_emulator_set_blink_phase(struct t6963_emulator *emu, bool phase);

/** @brief Set the emulated character font width.
 * @param emu Emulator state.
 * @param width Font width in pixels.
 * @return None.
 */
void t6963_emulator_set_font_width(struct t6963_emulator *emu, uint8_t width);

/** @brief Summarize nonzero text/graphics memory locations.
 * @param emu Emulator state.
 * @param stats Receives memory statistics.
 * @return None.
 */
void t6963_emulator_memory_stats(const struct t6963_emulator *emu,
		struct t6963_memory_stats *stats);

#endif
