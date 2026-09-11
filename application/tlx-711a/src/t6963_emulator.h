/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TLX711A_T6963_EMULATOR_H
#define TLX711A_T6963_EMULATOR_H

#include <stdbool.h>
#include <stdint.h>

#define TLX711A_WIDTH 240U
#define TLX711A_HEIGHT 64U
#define TLX711A_ROW_BYTES (TLX711A_WIDTH / 8U)
#define TLX711A_FRAME_SIZE (TLX711A_HEIGHT * TLX711A_ROW_BYTES)

struct t6963_emulator {
	uint8_t memory[8192];
	uint16_t address;
	uint16_t text_home;
	uint8_t text_area;
	uint16_t graphics_home;
	uint8_t graphics_area;
	uint8_t font_width;
	uint8_t offset;
	uint8_t mode;
	uint8_t display_mode;
	uint8_t pending_data[2];
	uint8_t pending_count;
	bool auto_write;
	int8_t address_step;
	bool read_increment;
	bool data_read_pending;
};

struct t6963_memory_stats {
	uint16_t text_nonzero;
	uint16_t text_first_address;
	uint16_t text_last_address;
	uint16_t graphics_nonzero;
	uint16_t graphics_first_address;
	uint16_t graphics_last_address;
};

void t6963_emulator_init(struct t6963_emulator *emu);
void t6963_emulator_command(struct t6963_emulator *emu, uint8_t command);
void t6963_emulator_data(struct t6963_emulator *emu, uint8_t data);
uint8_t t6963_emulator_read_status(const struct t6963_emulator *emu);
uint8_t t6963_emulator_read_data(struct t6963_emulator *emu);
void t6963_emulator_frame(const struct t6963_emulator *emu,
				 uint8_t frame[TLX711A_FRAME_SIZE]);
void t6963_emulator_set_font_width(struct t6963_emulator *emu, uint8_t width);
void t6963_emulator_memory_stats(const struct t6963_emulator *emu,
				 struct t6963_memory_stats *stats);

#endif
